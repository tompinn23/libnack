#
# libjpeg-turbo, cut down to the decoder and the TurboJPEG API.
#
# Upstream's own build cannot be used from here: its CMakeLists.txt refuses
# add_subdirectory outright and tells integrators to use ExternalProject_Add,
# which would mean fetching and building at build time - the opposite of
# vendoring. So the source list and the configure-time checks below are
# transcribed from it. See VENDORING.md for what was taken and how to update.
#
# SIMD is deliberately off. It would pull in a NASM dependency on x86 for a
# speedup nothing here needs: tilesets are decoded once at startup. Everything
# SIMD in 3.x is behind #ifdef WITH_SIMD, so leaving it undefined is all it
# takes.
#
include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckIncludeFiles)
include(CheckTypeSize)

set(VERSION 3.1.2)
set(COPYRIGHT_YEAR "1991-2025")
set(LIBJPEG_TURBO_VERSION_NUMBER 3001002)
set(JPEG_LIB_VERSION 62)
set(BUILD "vendored-by-libnack")

# Arithmetic coding is patent-free now and some JPEGs in the wild use it, so
# the decoder keeps it; the encoder has no reason to.
set(D_ARITH_CODING_SUPPORTED 1)

check_type_size("size_t" SIZE_T)
check_type_size("unsigned long" UNSIGNED_LONG)

if(SIZE_T EQUAL UNSIGNED_LONG)
    check_c_source_compiles(
        "int main(int argc, char **argv) { unsigned long a = argc;  return __builtin_ctzl(a); }"
        HAVE_BUILTIN_CTZL)
endif()

set(NACK_JPEG_MSVC_LIKE 0)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(NACK_JPEG_MSVC_LIKE 1)
endif()
if(NACK_JPEG_MSVC_LIKE)
    check_include_files("intrin.h" HAVE_INTRIN_H)
endif()

# Whether >> on a negative value propagates the sign. Every mainstream
# platform says yes; the check has to run the code to find out, so a cross
# build takes the answer upstream takes.
if(UNIX)
    if(CMAKE_CROSSCOMPILING)
        set(RIGHT_SHIFT_IS_UNSIGNED 0)
    else()
        check_c_source_runs("
            #include <stdio.h>
            #include <stdlib.h>
            static int is_shifting_signed (long arg) {
              long res = arg >> 4;
              if (res == -0x7F7E80CL)
                return 1;
              return 0;
            }
            int main (void) {
              exit(is_shifting_signed(-0x7F7E80B1L));
            }" RIGHT_SHIFT_IS_UNSIGNED)
    endif()
endif()

if(NOT MSVC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    check_c_source_compiles(
        "extern const int table[1]; const int __attribute__((visibility(\"hidden\"))) table[1] = { 0 }; int main(void) { return table[0]; }"
        HIDDEN_WORKS)
    if(HIDDEN_WORKS)
        set(HIDDEN "__attribute__((visibility(\"hidden\")))")
    endif()
endif()

if(MSVC)
    set(NACK_JPEG_INLINE_OPTIONS "__forceinline;__inline;inline")
    set(THREAD_LOCAL "__declspec(thread)")
else()
    set(NACK_JPEG_INLINE_OPTIONS
        "__inline__ __attribute__((always_inline));inline __attribute__((always_inline));__inline__;inline")
    set(THREAD_LOCAL "__thread")
endif()
foreach(candidate ${NACK_JPEG_INLINE_OPTIONS})
    check_c_source_compiles(
        "${candidate} static int foo(void) { return 0; } int main(void) { return foo(); }"
        INLINE_WORKS)
    if(INLINE_WORKS)
        set(INLINE ${candidate})
        break()
    endif()
endforeach()
if(NOT INLINE_WORKS)
    message(FATAL_ERROR "cannot determine how to inline functions for libjpeg-turbo")
endif()

# Upstream stamps the package name into jconfigint.h from CMAKE_PROJECT_NAME,
# which here would be libnack's; keep its own.
set(CMAKE_PROJECT_NAME libjpeg-turbo)

configure_file(src/jconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/jconfig.h)
configure_file(src/jconfigint.h.in ${CMAKE_CURRENT_BINARY_DIR}/jconfigint.h)
configure_file(src/jversion.h.in ${CMAKE_CURRENT_BINARY_DIR}/jversion.h)

set(NACK_JPEG_SOURCES
    src/jaricom.c src/jcapimin.c src/jchuff.c src/jcicc.c src/jcinit.c
    src/jclhuff.c src/jcmarker.c src/jcmaster.c src/jcomapi.c src/jcparam.c
    src/jcphuff.c src/jctrans.c src/jdapimin.c src/jdarith.c
    src/jdatadst-tj.c src/jdatadst.c src/jdatasrc-tj.c src/jdatasrc.c
    src/jdhuff.c src/jdicc.c src/jdinput.c src/jdlhuff.c src/jdmarker.c
    src/jdmaster.c src/jdphuff.c src/jdtrans.c src/jerror.c src/jfdctflt.c
    src/jmemmgr.c src/jmemnobs.c src/jpeg_nbits.c src/transupp.c
    src/turbojpeg.c
    # tj3LoadImage/tj3SaveImage reach for these, so the TurboJPEG API does not
    # link without them even though nothing here reads a BMP or a PPM.
    src/rdbmp.c src/wrbmp.c)

# The precision wrappers: one translation unit per data precision, which is
# how 3.x supports 8-, 12- and 16-bit samples from one source tree. Listed
# rather than globbed, so a file appearing or vanishing in an update is a
# visible change rather than a silent one.
set(NACK_JPEG_WRAPPER_SOURCES
    src/wrapper/rdppm-8.c src/wrapper/rdppm-12.c src/wrapper/rdppm-16.c
    src/wrapper/wrppm-8.c src/wrapper/wrppm-12.c src/wrapper/wrppm-16.c
    src/wrapper/jcapistd-12.c src/wrapper/jcapistd-16.c
    src/wrapper/jcapistd-8.c src/wrapper/jccoefct-12.c
    src/wrapper/jccoefct-8.c src/wrapper/jccolor-12.c
    src/wrapper/jccolor-16.c src/wrapper/jccolor-8.c
    src/wrapper/jcdctmgr-12.c src/wrapper/jcdctmgr-8.c
    src/wrapper/jcdiffct-12.c src/wrapper/jcdiffct-16.c
    src/wrapper/jcdiffct-8.c src/wrapper/jclossls-12.c
    src/wrapper/jclossls-16.c src/wrapper/jclossls-8.c
    src/wrapper/jcmainct-12.c src/wrapper/jcmainct-16.c
    src/wrapper/jcmainct-8.c src/wrapper/jcprepct-12.c
    src/wrapper/jcprepct-16.c src/wrapper/jcprepct-8.c
    src/wrapper/jcsample-12.c src/wrapper/jcsample-16.c
    src/wrapper/jcsample-8.c src/wrapper/jdapistd-12.c
    src/wrapper/jdapistd-16.c src/wrapper/jdapistd-8.c
    src/wrapper/jdcoefct-12.c src/wrapper/jdcoefct-8.c
    src/wrapper/jdcolor-12.c src/wrapper/jdcolor-16.c
    src/wrapper/jdcolor-8.c src/wrapper/jddctmgr-12.c
    src/wrapper/jddctmgr-8.c src/wrapper/jddiffct-12.c
    src/wrapper/jddiffct-16.c src/wrapper/jddiffct-8.c
    src/wrapper/jdlossls-12.c src/wrapper/jdlossls-16.c
    src/wrapper/jdlossls-8.c src/wrapper/jdmainct-12.c
    src/wrapper/jdmainct-16.c src/wrapper/jdmainct-8.c
    src/wrapper/jdmerge-12.c src/wrapper/jdmerge-8.c
    src/wrapper/jdpostct-12.c src/wrapper/jdpostct-16.c
    src/wrapper/jdpostct-8.c src/wrapper/jdsample-12.c
    src/wrapper/jdsample-16.c src/wrapper/jdsample-8.c
    src/wrapper/jfdctfst-12.c src/wrapper/jfdctfst-8.c
    src/wrapper/jfdctint-12.c src/wrapper/jfdctint-8.c
    src/wrapper/jidctflt-12.c src/wrapper/jidctflt-8.c
    src/wrapper/jidctfst-12.c src/wrapper/jidctfst-8.c
    src/wrapper/jidctint-12.c src/wrapper/jidctint-8.c
    src/wrapper/jidctred-12.c src/wrapper/jidctred-8.c
    src/wrapper/jquant1-12.c src/wrapper/jquant1-8.c
    src/wrapper/jquant2-12.c src/wrapper/jquant2-8.c src/wrapper/jutils-12.c
    src/wrapper/jutils-16.c src/wrapper/jutils-8.c)

add_library(nack_jpeg OBJECT ${NACK_JPEG_SOURCES} ${NACK_JPEG_WRAPPER_SOURCES})

target_include_directories(nack_jpeg
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
#
# BMP_SUPPORTED and PPM_SUPPORTED are what upstream sets when building the
# TurboJPEG library: they gate the readers and writers tj3LoadImage and
# tj3SaveImage call into, so without them turbojpeg.c does not link. Nothing
# here loads a BMP or a PPM, but the API is one piece.
#
target_compile_definitions(nack_jpeg PRIVATE
    NO_GETENV NO_PUTENV BMP_SUPPORTED PPM_SUPPORTED)

set_target_properties(nack_jpeg PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

# Third-party code is not ours to clean up, and its warnings would bury the
# ones that are.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nack_jpeg PRIVATE -w)
elseif(MSVC)
    target_compile_options(nack_jpeg PRIVATE /w)
    target_compile_definitions(nack_jpeg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
