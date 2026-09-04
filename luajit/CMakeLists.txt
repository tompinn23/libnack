##############################################################################
# CMakeLists.txt for LuaJIT
#
# Drop this file in the root of a LuaJIT checkout (next to src/ and dynasm/):
#
#   cmake -S LuaJIT -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   cmake --install build --prefix /usr/local
#
# It replicates the bootstrap that src/Makefile and src/msvcbuild.bat perform:
#
#   1. build minilua (host)                 - a stripped Lua to run DynASM
#   2. minilua dynasm.lua -> buildvm_arch.h - assemble the VM description
#   3. build buildvm (host)                 - compiled with target arch macros
#   4. buildvm -> lj_vm.S + lj_*def.h       - the generated VM and defs
#   5. compile the core against those
#
# Supported: Linux/*BSD (GCC/Clang), macOS (Clang), Windows (MSVC).
# For MinGW use the upstream Makefile instead.
##############################################################################

cmake_minimum_required(VERSION 3.16)
project(LuaJIT C)

include(CMakeDependentOption)
include(CheckCCompilerFlag)
include(GNUInstallDirs)

set(LUAJIT_MAJVER 2)
set(LUAJIT_MINVER 1)
set(LUAJIT_ABIVER 5.1)
set(LUAJIT_VERSION_DIR "luajit-${LUAJIT_MAJVER}.${LUAJIT_MINVER}")

set(LJ_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(LJ_DASM "${CMAKE_CURRENT_SOURCE_DIR}/dynasm")
set(LJ_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")

if(NOT EXISTS "${LJ_SRC}/lj_arch.h")
  message(FATAL_ERROR "Put this CMakeLists.txt in the LuaJIT root, next to src/")
endif()

file(MAKE_DIRECTORY "${LJ_GEN}" "${LJ_GEN}/host" "${LJ_GEN}/jit")

##############################################################################
# Options. These mirror the XCFLAGS block in src/Makefile.
##############################################################################

option(BUILD_SHARED_LIBS          "Build LuaJIT as a shared library" OFF)
option(LUAJIT_BUILD_EXE           "Build the luajit command line interpreter" ON)
option(LUAJIT_DISABLE_FFI         "Permanently disable the FFI extension" OFF)
option(LUAJIT_DISABLE_JIT         "Disable the JIT compiler (interpreter only)" OFF)
option(LUAJIT_DISABLE_GC64        "Disable GC64 mode for x64" OFF)
option(LUAJIT_ENABLE_LUA52COMPAT  "Enable some upwards-compatible Lua 5.2 features" OFF)
option(LUAJIT_USE_SYSMALLOC       "Use the system allocator instead of LuaJIT's" OFF)
option(LUAJIT_USE_VALGRIND        "Run under Valgrind (needs LUAJIT_USE_SYSMALLOC)" OFF)
option(LUAJIT_USE_GDBJIT          "Enable the GDB JIT API" OFF)
option(LUAJIT_USE_APICHECK        "Turn on assertions for the Lua/C API" OFF)
option(LUAJIT_USE_ASSERT          "Turn on assertions for the whole LuaJIT VM" OFF)
set(LUAJIT_NUMMODE "0" CACHE STRING "Number mode: 0=default, 1=single, 2=dual")
set_property(CACHE LUAJIT_NUMMODE PROPERTY STRINGS 0 1 2)

# These become -D flags on *both* the arch probe and the compile, because they
# change what lj_arch.h reports (LJ_HASJIT, LJ_HASFFI, LJ_FR2, ...) and
# therefore which DynASM flags and which .dasc file are correct.
set(LJ_XCFLAGS "")
foreach(_opt LUAJIT_DISABLE_FFI LUAJIT_DISABLE_JIT LUAJIT_DISABLE_GC64
             LUAJIT_ENABLE_LUA52COMPAT LUAJIT_USE_SYSMALLOC LUAJIT_USE_VALGRIND
             LUAJIT_USE_GDBJIT LUAJIT_USE_APICHECK LUAJIT_USE_ASSERT)
  if(${_opt})
    list(APPEND LJ_XCFLAGS "-D${_opt}")
  endif()
endforeach()
if(NOT LUAJIT_NUMMODE STREQUAL "0")
  list(APPEND LJ_XCFLAGS "-DLUAJIT_NUMMODE=${LUAJIT_NUMMODE}")
endif()

# Leftovers from a previous `make` would win over our generated copies, because
# LuaJIT uses quoted includes that resolve next to the including file first.
foreach(_stale luajit.h lj_bcdef.h lj_ffdef.h lj_libdef.h lj_recdef.h
               lj_folddef.h host/buildvm_arch.h)
  if(EXISTS "${LJ_SRC}/${_stale}")
    message(WARNING "Stale generated file ${LJ_SRC}/${_stale} from a previous "
                    "Makefile build will shadow the CMake-generated one. "
                    "Run 'make clean' in src/ first.")
  endif()
endforeach()

##############################################################################
# Target architecture probe.
#
# The Makefile preprocesses lj_arch.h and greps the macro dump. We do the same,
# which keeps this correct for every target LuaJIT supports rather than
# hard-coding a list.
##############################################################################

if(MSVC)
  # cl.exe has no -dM, so derive the same facts the way msvcbuild.bat does.
  set(_dasm_defs WIN ENDIAN_LE FPU)
  if(NOT LUAJIT_DISABLE_JIT)
    list(APPEND _dasm_defs JIT)
  endif()
  if(NOT LUAJIT_DISABLE_FFI)
    list(APPEND _dasm_defs FFI)
  endif()
  set(LJ_BUILDVM_DEFS "")

  if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM64|arm64)$")
    set(LJ_ARCH arm64)
    set(LJ_DASM_ARCH arm64)
    list(APPEND _dasm_defs P64)
    set(LJ_BUILDVM_DEFS "-DLUAJIT_TARGET=LUAJIT_ARCH_ARM64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LJ_ARCH x64)
    list(APPEND _dasm_defs P64)
    if(LUAJIT_DISABLE_GC64)
      set(LJ_DASM_ARCH x86)
    else()
      set(LJ_DASM_ARCH x64)
    endif()
  else()
    set(LJ_ARCH x86)
    set(LJ_DASM_ARCH x86)
  endif()

  # msvcbuild.bat passes -LN; keep the generated header identical to it.
  set(LJ_DASM_FLAGS -LN)
  foreach(_d ${_dasm_defs})
    list(APPEND LJ_DASM_FLAGS -D ${_d})
  endforeach()

else()

  execute_process(
    COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS} ${LJ_XCFLAGS} -E -dM lj_arch.h
    WORKING_DIRECTORY "${LJ_SRC}"
    OUTPUT_VARIABLE LJ_TESTARCH
    ERROR_VARIABLE  LJ_TESTARCH_ERR
    RESULT_VARIABLE LJ_TESTARCH_RES)
  if(NOT LJ_TESTARCH_RES EQUAL 0)
    message(FATAL_ERROR "Failed to probe lj_arch.h:\n${LJ_TESTARCH_ERR}")
  endif()

  # Helper: does the macro dump contain "#define <name> <value>"?
  macro(lj_has _out _name _value)
    if(LJ_TESTARCH MATCHES "#define[ \t]+${_name}[ \t]+${_value}\n")
      set(${_out} TRUE)
    else()
      set(${_out} FALSE)
    endif()
  endmacro()

  set(LJ_BUILDVM_DEFS "")
  set(LJ_DASM_FLAGS "")

  lj_has(_is_x64   LJ_TARGET_X64   1)
  lj_has(_is_x86   LJ_TARGET_X86   1)
  lj_has(_is_arm   LJ_TARGET_ARM   1)
  lj_has(_is_arm64 LJ_TARGET_ARM64 1)
  lj_has(_is_ppc   LJ_TARGET_PPC   1)
  lj_has(_is_mips  LJ_TARGET_MIPS  1)

  if(_is_x64)
    set(LJ_ARCH x64)
  elseif(_is_x86)
    set(LJ_ARCH x86)
  elseif(_is_arm)
    set(LJ_ARCH arm)
  elseif(_is_arm64)
    set(LJ_ARCH arm64)
    if(LJ_TESTARCH MATCHES "__AARCH64EB__")
      list(APPEND LJ_BUILDVM_DEFS "-D__AARCH64EB__=1")
    endif()
  elseif(_is_ppc)
    set(LJ_ARCH ppc)
    lj_has(_le LJ_LE 1)
    if(_le)
      list(APPEND LJ_BUILDVM_DEFS "-DLJ_ARCH_ENDIAN=LUAJIT_LE")
    else()
      list(APPEND LJ_BUILDVM_DEFS "-DLJ_ARCH_ENDIAN=LUAJIT_BE")
    endif()
  elseif(_is_mips)
    lj_has(_is_mips64 LJ_TARGET_MIPS64 1)
    if(_is_mips64)
      set(LJ_ARCH mips64)
    else()
      set(LJ_ARCH mips)
    endif()
    if(LJ_TESTARCH MATCHES "MIPSEL")
      list(APPEND LJ_BUILDVM_DEFS "-D__MIPSEL__=1")
    endif()
  else()
    message(FATAL_ERROR "Unsupported target architecture")
  endif()

  list(APPEND LJ_BUILDVM_DEFS "-DLUAJIT_TARGET=LUAJIT_ARCH_${LJ_ARCH}")

  lj_has(_le      LJ_LE            1)
  lj_has(_p64     LJ_ARCH_BITS     64)
  lj_has(_hasjit  LJ_HASJIT        1)
  lj_has(_hasffi  LJ_HASFFI        1)
  lj_has(_dualnum LJ_DUALNUM       1)
  lj_has(_hasfpu  LJ_ARCH_HASFPU   1)
  lj_has(_softfp  LJ_ABI_SOFTFP    1)
  lj_has(_nounwind LJ_NO_UNWIND    1)
  lj_has(_pauth   LJ_ABI_PAUTH     1)
  lj_has(_btrack  LJ_ABI_BRANCH_TRACK 1)
  lj_has(_sstack  LJ_ABI_SHADOW_STACK 1)

  if(_le)
    list(APPEND LJ_DASM_FLAGS -D ENDIAN_LE)
  else()
    list(APPEND LJ_DASM_FLAGS -D ENDIAN_BE)
  endif()
  if(_p64)
    list(APPEND LJ_DASM_FLAGS -D P64)
  endif()
  if(_hasjit)
    list(APPEND LJ_DASM_FLAGS -D JIT)
  endif()
  if(_hasffi)
    list(APPEND LJ_DASM_FLAGS -D FFI)
  endif()
  if(_dualnum)
    list(APPEND LJ_DASM_FLAGS -D DUALNUM)
  endif()
  if(_hasfpu)
    list(APPEND LJ_DASM_FLAGS -D FPU)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ARCH_HASFPU=1")
  else()
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ARCH_HASFPU=0")
  endif()
  if(_softfp)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ABI_SOFTFP=1")
  else()
    list(APPEND LJ_DASM_FLAGS -D HFABI)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ABI_SOFTFP=0")
  endif()
  if(_nounwind)
    list(APPEND LJ_DASM_FLAGS -D NO_UNWIND)
    list(APPEND LJ_BUILDVM_DEFS "-DLUAJIT_NO_UNWIND")
  endif()
  if(_pauth)
    list(APPEND LJ_DASM_FLAGS -D PAUTH)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ABI_PAUTH=1")
  endif()
  if(_btrack)
    list(APPEND LJ_DASM_FLAGS -D BRANCH_TRACK)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ABI_BRANCH_TRACK=1")
  endif()
  if(_sstack)
    list(APPEND LJ_DASM_FLAGS -D SHADOW_STACK)
    list(APPEND LJ_BUILDVM_DEFS "-DLJ_ABI_SHADOW_STACK=1")
  endif()

  # -D VER=<n> for architectures that have revisions (armv7, ppc, mips r6...).
  # Absent on x86/x64, where the Makefile passes an empty VER too.
  set(_ver "")
  if(LJ_TESTARCH MATCHES "#define[ \t]+LJ_ARCH_VERSION[ \t]+([0-9]+)")
    set(_ver "${CMAKE_MATCH_1}")
  endif()
  list(APPEND LJ_DASM_FLAGS -D "VER=${_ver}")

  if(LJ_ARCH STREQUAL "mips" OR LJ_ARCH STREQUAL "mips64")
    if(LJ_TESTARCH MATCHES "LJ_TARGET_MIPSR6")
      list(APPEND LJ_DASM_FLAGS -D MIPSR6)
    endif()
  elseif(LJ_ARCH STREQUAL "ppc")
    lj_has(_sqrt LJ_ARCH_SQRT 1)
    lj_has(_round LJ_ARCH_ROUND 1)
    lj_has(_ppc32on64 LJ_ARCH_PPC32ON64 1)
    if(_sqrt)
      list(APPEND LJ_DASM_FLAGS -D SQRT)
    endif()
    if(_round)
      list(APPEND LJ_DASM_FLAGS -D ROUND)
    endif()
    if(_ppc32on64)
      list(APPEND LJ_DASM_FLAGS -D GPR64)
    endif()
  elseif(LJ_ARCH STREQUAL "arm" AND APPLE)
    list(APPEND LJ_DASM_FLAGS -D IOS)
  endif()

  # x64 without GC64 uses the 32-bit VM description with P64 set.
  set(LJ_DASM_ARCH "${LJ_ARCH}")
  lj_has(_fr2 LJ_FR2 1)
  if(LJ_ARCH STREQUAL "x64" AND NOT _fr2)
    set(LJ_DASM_ARCH x86)
  endif()

endif()

# How buildvm should emit the VM: assembly for ELF/Mach-O, a linkable object
# for COFF (there is no MSVC inline assembler for x64).
if(WIN32)
  set(LJ_VM_MODE peobj)
  set(LJ_VM_OUT  "${LJ_GEN}/lj_vm.obj")
elseif(APPLE)
  set(LJ_VM_MODE machasm)
  set(LJ_VM_OUT  "${LJ_GEN}/lj_vm.S")
else()
  set(LJ_VM_MODE elfasm)
  set(LJ_VM_OUT  "${LJ_GEN}/lj_vm.S")
endif()

if(NOT LJ_VM_MODE STREQUAL "peobj")
  enable_language(ASM)
endif()

message(STATUS "LuaJIT target architecture: ${LJ_ARCH} (DynASM: ${LJ_DASM_ARCH}, VM: ${LJ_VM_MODE})")

##############################################################################
# Host tools.
#
# minilua and buildvm must run on the *build* machine. When cross-compiling we
# recurse into this same CMakeLists with the default (host) toolchain and use
# the executables it produces.
##############################################################################

if(CMAKE_CROSSCOMPILING AND NOT LUAJIT_HOST_TOOLS)
  set(LUAJIT_NATIVE_MINILUA "" CACHE FILEPATH "Prebuilt host minilua")
  set(LUAJIT_NATIVE_BUILDVM "" CACHE FILEPATH "Prebuilt host buildvm")
endif()

if(LUAJIT_NATIVE_MINILUA AND LUAJIT_NATIVE_BUILDVM)

  # The caller built the host tools separately and pointed us at them.
  set(MINILUA_CMD "${LUAJIT_NATIVE_MINILUA}")
  set(BUILDVM_CMD "${LUAJIT_NATIVE_BUILDVM}")
  set(MINILUA_DEP "")
  set(BUILDVM_DEP "")

elseif(CMAKE_CROSSCOMPILING)

  message(FATAL_ERROR
    "Cross-compiling: build the host tools first with a native configure of "
    "this same tree, then pass -DLUAJIT_NATIVE_MINILUA=<path/to/minilua> and "
    "-DLUAJIT_NATIVE_BUILDVM=<path/to/buildvm>. buildvm must be configured "
    "with the *target* options, since it bakes in the target ABI.")

else()

  add_executable(minilua "${LJ_SRC}/host/minilua.c")
  target_include_directories(minilua PRIVATE "${LJ_SRC}")
  if(UNIX)
    target_link_libraries(minilua PRIVATE m)
  endif()
  if(MSVC)
    target_compile_definitions(minilua PRIVATE _CRT_SECURE_NO_DEPRECATE)
  endif()

  add_executable(buildvm
    "${LJ_SRC}/host/buildvm.c"
    "${LJ_SRC}/host/buildvm_asm.c"
    "${LJ_SRC}/host/buildvm_peobj.c"
    "${LJ_SRC}/host/buildvm_lib.c"
    "${LJ_SRC}/host/buildvm_fold.c")
  # buildvm_arch.h lives in gen/host/, and buildvm.c includes it unqualified.
  target_include_directories(buildvm PRIVATE
    "${LJ_GEN}/host" "${LJ_GEN}" "${LJ_SRC}" "${LJ_DASM}")
  # buildvm is a host binary that must know the *target* ABI.
  target_compile_options(buildvm PRIVATE ${LJ_BUILDVM_DEFS} ${LJ_XCFLAGS})
  if(MSVC)
    target_compile_definitions(buildvm PRIVATE _CRT_SECURE_NO_DEPRECATE)
  endif()

  set(MINILUA_CMD $<TARGET_FILE:minilua>)
  set(BUILDVM_CMD $<TARGET_FILE:buildvm>)
  set(MINILUA_DEP minilua)
  set(BUILDVM_DEP buildvm)

endif()

##############################################################################
# Step 1+2: luajit.h (rolling version) and buildvm_arch.h (DynASM).
##############################################################################

# The rolling release version is the commit timestamp, or .relver in a tarball.
find_package(Git QUIET)
set(LJ_RELVER "")
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" show -s --format=%ct
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE LJ_RELVER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT LJ_RELVER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.relver")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.relver" LJ_RELVER)
  string(STRIP "${LJ_RELVER}" LJ_RELVER)
endif()
if(NOT LJ_RELVER MATCHES "^[0-9]+$")
  set(LJ_RELVER "ROLLING")
endif()
file(WRITE "${LJ_GEN}/luajit_relver.txt" "${LJ_RELVER}\n")

add_custom_command(
  OUTPUT  "${LJ_GEN}/luajit.h"
  COMMAND ${MINILUA_CMD} "${LJ_SRC}/host/genversion.lua"
          "${LJ_SRC}/luajit_rolling.h"
          "${LJ_GEN}/luajit_relver.txt"
          "${LJ_GEN}/luajit.h"
  DEPENDS ${MINILUA_DEP} "${LJ_SRC}/luajit_rolling.h" "${LJ_GEN}/luajit_relver.txt"
  COMMENT "VERSION   luajit.h"
  VERBATIM)

add_custom_command(
  OUTPUT  "${LJ_GEN}/host/buildvm_arch.h"
  COMMAND ${MINILUA_CMD} "${LJ_DASM}/dynasm.lua" ${LJ_DASM_FLAGS}
          -o "${LJ_GEN}/host/buildvm_arch.h" "vm_${LJ_DASM_ARCH}.dasc"
  DEPENDS ${MINILUA_DEP} "${LJ_SRC}/vm_${LJ_DASM_ARCH}.dasc"
          "${LJ_SRC}/lj_arch.h" "${LJ_SRC}/lua.h" "${LJ_SRC}/luaconf.h"
  WORKING_DIRECTORY "${LJ_SRC}"
  COMMENT "DYNASM    buildvm_arch.h"
  VERBATIM)

if(TARGET buildvm)
  add_custom_target(buildvm_arch DEPENDS "${LJ_GEN}/host/buildvm_arch.h"
                                         "${LJ_GEN}/luajit.h")
  add_dependencies(buildvm buildvm_arch)
endif()

##############################################################################
# Step 3: everything buildvm generates.
##############################################################################

# Order matters: the bytecode and fast function numbering is positional.
set(LJ_LIB_C
  lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c
  lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c)
set(LJ_LIB_C_ABS "")
foreach(_f ${LJ_LIB_C})
  list(APPEND LJ_LIB_C_ABS "${LJ_SRC}/${_f}")
endforeach()

function(luajit_buildvm mode output)
  cmake_parse_arguments(A "" "" "INPUTS;DEPENDS" ${ARGN})
  get_filename_component(_name "${output}" NAME)
  add_custom_command(
    OUTPUT  "${output}"
    COMMAND ${BUILDVM_CMD} -m ${mode} -o "${output}" ${A_INPUTS}
    DEPENDS ${BUILDVM_DEP} ${A_DEPENDS}
    WORKING_DIRECTORY "${LJ_SRC}"
    COMMENT "BUILDVM   ${_name}"
    VERBATIM)
endfunction()

luajit_buildvm(${LJ_VM_MODE} "${LJ_VM_OUT}")
luajit_buildvm(bcdef   "${LJ_GEN}/lj_bcdef.h"    INPUTS ${LJ_LIB_C} DEPENDS ${LJ_LIB_C_ABS})
luajit_buildvm(ffdef   "${LJ_GEN}/lj_ffdef.h"    INPUTS ${LJ_LIB_C} DEPENDS ${LJ_LIB_C_ABS})
luajit_buildvm(libdef  "${LJ_GEN}/lj_libdef.h"   INPUTS ${LJ_LIB_C} DEPENDS ${LJ_LIB_C_ABS})
luajit_buildvm(recdef  "${LJ_GEN}/lj_recdef.h"   INPUTS ${LJ_LIB_C} DEPENDS ${LJ_LIB_C_ABS})
luajit_buildvm(vmdef   "${LJ_GEN}/jit/vmdef.lua" INPUTS ${LJ_LIB_C} DEPENDS ${LJ_LIB_C_ABS})
luajit_buildvm(folddef "${LJ_GEN}/lj_folddef.h"  INPUTS lj_opt_fold.c
                                                 DEPENDS "${LJ_SRC}/lj_opt_fold.c")

set(LJ_GENERATED
  "${LJ_GEN}/luajit.h"
  "${LJ_GEN}/lj_bcdef.h"
  "${LJ_GEN}/lj_ffdef.h"
  "${LJ_GEN}/lj_libdef.h"
  "${LJ_GEN}/lj_recdef.h"
  "${LJ_GEN}/lj_folddef.h"
  "${LJ_GEN}/jit/vmdef.lua"
  "${LJ_VM_OUT}")

##############################################################################
# Step 4: the library itself.
##############################################################################

set(LJ_CORE_C
  lj_assert.c lj_gc.c lj_err.c lj_char.c lj_bc.c lj_obj.c lj_buf.c
  lj_str.c lj_tab.c lj_func.c lj_udata.c lj_meta.c lj_debug.c
  lj_prng.c lj_state.c lj_dispatch.c lj_vmevent.c lj_vmmath.c
  lj_strscan.c lj_strfmt.c lj_strfmt_num.c lj_serialize.c
  lj_api.c lj_profile.c
  lj_lex.c lj_parse.c lj_bcread.c lj_bcwrite.c lj_load.c
  lj_ir.c lj_opt_mem.c lj_opt_fold.c lj_opt_narrow.c
  lj_opt_dce.c lj_opt_loop.c lj_opt_split.c lj_opt_sink.c
  lj_mcode.c lj_snap.c lj_record.c lj_crecord.c lj_ffrecord.c
  lj_asm.c lj_trace.c lj_gdbjit.c
  lj_ctype.c lj_cdata.c lj_cconv.c lj_ccall.c lj_ccallback.c
  lj_carith.c lj_clib.c lj_cparse.c
  lj_lib.c lj_alloc.c lib_aux.c
  ${LJ_LIB_C} lib_init.c)

set(LJ_CORE_C_ABS "")
foreach(_f ${LJ_CORE_C})
  list(APPEND LJ_CORE_C_ABS "${LJ_SRC}/${_f}")
endforeach()

if(LJ_VM_MODE STREQUAL "peobj")
  set_source_files_properties("${LJ_VM_OUT}" PROPERTIES
    GENERATED TRUE EXTERNAL_OBJECT TRUE)
else()
  set_source_files_properties("${LJ_VM_OUT}" PROPERTIES
    GENERATED TRUE LANGUAGE ASM)
endif()

add_library(libluajit ${LJ_CORE_C_ABS} ${LJ_VM_OUT} ${LJ_GENERATED})
add_library(luajit::luajit ALIAS libluajit)

# Generated headers are inputs, not sources to compile.
foreach(_g ${LJ_GENERATED})
  if(NOT "${_g}" STREQUAL "${LJ_VM_OUT}")
    set_source_files_properties("${_g}" PROPERTIES GENERATED TRUE HEADER_FILE_ONLY TRUE)
  endif()
endforeach()

target_include_directories(libluajit
  PUBLIC  $<BUILD_INTERFACE:${LJ_SRC}>
          $<BUILD_INTERFACE:${LJ_GEN}>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${LUAJIT_VERSION_DIR}>)
target_compile_options(libluajit PRIVATE ${LJ_XCFLAGS})
set_target_properties(libluajit PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_STANDARD 99
  C_VISIBILITY_PRESET default)

if(WIN32)
  set_target_properties(libluajit PROPERTIES OUTPUT_NAME lua${LUAJIT_MAJVER}${LUAJIT_MINVER})
  if(BUILD_SHARED_LIBS)
    # Mixed/static mode on Windows cannot load C modules, they bind to the DLL.
    target_compile_definitions(libluajit PRIVATE LUA_BUILD_AS_DLL)
    set_target_properties(libluajit PROPERTIES OUTPUT_NAME lua51 WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  endif()
else()
  set_target_properties(libluajit PROPERTIES
    OUTPUT_NAME "luajit-${LUAJIT_ABIVER}"
    VERSION   "${LUAJIT_MAJVER}.${LUAJIT_MINVER}.0"
    SOVERSION "${LUAJIT_MAJVER}")
endif()

if(MSVC)
  target_compile_definitions(libluajit PRIVATE
    _CRT_SECURE_NO_DEPRECATE
    "_CRT_STDIO_INLINE=__declspec(dllexport)__inline")
  if(LJ_ARCH STREQUAL "x86")
    target_compile_options(libluajit PRIVATE /arch:SSE2)
  endif()
else()
  # Matches CCOPT in src/Makefile. The VM keeps no frame pointer anyway, so
  # keeping one in the C part only costs speed; unwinding still works.
  target_compile_options(libluajit PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:-fomit-frame-pointer> -Wall)
  check_c_compiler_flag(-fno-stack-protector LJ_HAS_NO_SSP)
  if(LJ_HAS_NO_SSP)
    target_compile_options(libluajit PRIVATE -fno-stack-protector)
  endif()
  if(LJ_ARCH STREQUAL "x86")
    target_compile_options(libluajit PRIVATE -march=i686 -msse -msse2 -mfpmath=sse)
  endif()
endif()

# External unwinding: on ELF/Mach-O targets whose toolchain emits unwind tables,
# LuaJIT uses the system unwinder instead of its internal one.
if(APPLE)
  target_compile_definitions(libluajit PRIVATE LUAJIT_UNWIND_EXTERNAL)
elseif(UNIX AND NOT _nounwind)
  set(_probe "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/lj_unwind_probe")
  file(WRITE "${_probe}.c" "extern void b(void);int a(void){b();return 0;}\n")
  execute_process(
    COMMAND ${CMAKE_C_COMPILER} -c "${_probe}.c" -o "${_probe}.o"
    RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
  if(_r EQUAL 0 AND EXISTS "${_probe}.o")
    file(READ "${_probe}.o" _obj HEX)
    # "eh_frame" / "__unwind_info" in hex
    if(_obj MATCHES "65685f6672616d65" OR _obj MATCHES "5f5f756e77696e645f696e666f")
      target_compile_definitions(libluajit PRIVATE LUAJIT_UNWIND_EXTERNAL)
    endif()
  endif()
endif()

# Where the interpreter looks for Lua and C modules at runtime.
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
  target_compile_definitions(libluajit PRIVATE "LUA_ROOT=\"${CMAKE_INSTALL_PREFIX}\"")
endif()
if(NOT CMAKE_INSTALL_LIBDIR STREQUAL "lib")
  target_compile_definitions(libluajit PRIVATE "LUA_MULTILIB=\"${CMAKE_INSTALL_LIBDIR}\"")
endif()

if(UNIX)
  target_link_libraries(libluajit PUBLIC m ${CMAKE_DL_LIBS})
  if(APPLE AND BUILD_SHARED_LIBS)
    target_link_options(libluajit PRIVATE -undefined dynamic_lookup)
  endif()
endif()

##############################################################################
# Step 5: the interpreter.
##############################################################################

if(LUAJIT_BUILD_EXE)
  add_executable(luajit "${LJ_SRC}/luajit.c")
  target_link_libraries(luajit PRIVATE libluajit)
  if(MSVC)
    target_compile_definitions(luajit PRIVATE _CRT_SECURE_NO_DEPRECATE)
  endif()
  if(APPLE)
    # LuaJIT needs its code in the low 2GB on x86_64.
    if(NOT BUILD_SHARED_LIBS AND LJ_ARCH STREQUAL "x64")
      target_link_options(luajit PRIVATE -pagezero_size 10000 -image_base 100000000)
    endif()
  elseif(UNIX)
    # Export symbols so Lua C modules can resolve lua_* against the interpreter.
    set_target_properties(luajit PROPERTIES ENABLE_EXPORTS TRUE)
  endif()
endif()

##############################################################################
# Install.
##############################################################################

set(LJ_PUBLIC_HEADERS
  "${LJ_SRC}/lua.h" "${LJ_SRC}/luaconf.h" "${LJ_SRC}/lualib.h"
  "${LJ_SRC}/lauxlib.h" "${LJ_SRC}/lua.hpp" "${LJ_GEN}/luajit.h")

install(TARGETS libluajit
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(LUAJIT_BUILD_EXE)
  install(TARGETS luajit RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

install(FILES ${LJ_PUBLIC_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${LUAJIT_VERSION_DIR}")

# The jit.* Lua modules, including the generated vmdef.lua.
file(GLOB LJ_JIT_LUA "${LJ_SRC}/jit/*.lua")
install(FILES ${LJ_JIT_LUA} "${LJ_GEN}/jit/vmdef.lua"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/${LUAJIT_VERSION_DIR}/jit")
if(WIN32)
  # Windows package.path defaults to !\lua\?.lua, i.e. next to the exe.
  install(FILES ${LJ_JIT_LUA} "${LJ_GEN}/jit/vmdef.lua"
    DESTINATION "${CMAKE_INSTALL_BINDIR}/lua/jit")
endif()
