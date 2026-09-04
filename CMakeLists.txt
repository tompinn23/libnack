cmake_minimum_required(VERSION 3.21)

#
# C is still here for two reasons: the vendored stb_image and the code
# wayland-scanner generates are C, and a couple of tests (image_test.c, the
# Win32 ABI check) are C on purpose - the rest of the library, its public
# header, and its tests and examples are C++.
#
project(libnack
    VERSION 0.2.0
    DESCRIPTION "A cell console for roguelikes"
    LANGUAGES C CXX)

# The Cocoa and Metal sources are .mm now, which is Objective-C++.
if(APPLE)
    enable_language(OBJCXX)
endif()

include(GNUInstallDirs)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------

include(CMakeDependentOption)

option(NACK_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
option(NACK_BUILD_SHARED   "Build libnack as a shared library" OFF)

cmake_dependent_option(NACK_ENABLE_XCB
    "Build the XCB backend" ON "UNIX;NOT APPLE" OFF)
cmake_dependent_option(NACK_ENABLE_WAYLAND
    "Build the Wayland backend" ON "UNIX;NOT APPLE" OFF)
option(NACK_MACOS_OPENGL_ONLY
    "Leave the Metal renderer out of the macOS build and always use OpenGL" OFF)

option(NACK_WIN32_USE_SDK_HEADERS
    "Use <windows.h> instead of libnack's hand-rolled Win32 declarations" OFF)

cmake_dependent_option(NACK_XCB_XLIB_FALLBACK
    "Allow falling back to an Xlib Display for EGL when EGL_EXT_platform_xcb is missing (needed for the NVIDIA driver)"
    ON "NACK_ENABLE_XCB" OFF)

option(NACK_FMT_EXTERNAL
    "Link against a fmt::fmt target the including project already provides (its own add_subdirectory or FetchContent), instead of vendoring a copy - same idea as spdlog's SPDLOG_FMT_EXTERNAL"
    OFF)

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------

set(NACK_SOURCES
    src/nack.cpp
    src/nack_keys.cpp
    src/nack_hpp_impl.cpp
    src/common/nack_proc_cache.cpp
    src/console/nack_api.cpp
    src/console/nack_console.cpp
    src/console/nack_image.cpp
    src/console/nack_render.cpp
    src/console/nack_tileset.cpp
    src/console/nack_gfx.cpp
    src/console/nack_gfx_gl.cpp
    src/console/nack_gl.cpp)

# macOS gets both renderers and picks between them at run time: Metal first,
# because Apple deprecated OpenGL and capped it at 4.1, but OpenGL still there
# to fall back on. Everywhere else there is only OpenGL.
if(APPLE AND NOT NACK_MACOS_OPENGL_ONLY)
    list(APPEND NACK_SOURCES src/console/nack_gfx_metal.mm)
endif()

#
# The vendored image decoder. Decoding is parsing untrusted input, so it is
# stb_image rather than something written here, and it builds from source
# in-tree so the library still needs nothing installed on the machine.
#
add_subdirectory(third_party/stb)

#
# {fmt}, for <nack/nack.hpp>'s formatting. Vendored by default (the real
# upstream project, under third_party/fmt) so building libnack needs nothing
# installed. FMT_INSTALL is turned on so it exports itself properly, which is
# what lets libnack's own install() below export nack without CMake
# complaining that nack's dependency on fmt::fmt cannot be expressed.
#
# A project that embeds libnack via add_subdirectory and already has its own
# fmt::fmt target - its own add_subdirectory or FetchContent, added before it
# adds libnack - can point libnack at that one instead with
# -DNACK_FMT_EXTERNAL=ON, the same idea as spdlog's SPDLOG_FMT_EXTERNAL: one
# copy of {fmt} in the final binary rather than two. install() is skipped
# entirely in that case (see the Install section below) - a target the
# including project defined itself cannot be expressed in an exported
# Targets file, and nothing embedded via add_subdirectory installs itself
# anyway.
#
if(NACK_FMT_EXTERNAL)
    if(NOT TARGET fmt::fmt)
        find_package(fmt REQUIRED)
    endif()
else()
    set(FMT_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/fmt)
endif()

set(NACK_LIBRARIES "")
# Its objects go straight into libnack rather than being a separate archive a
# consumer would have to link too: from the outside, decoding images is part of
# what the library does.
set(NACK_VENDORED_OBJECTS $<TARGET_OBJECTS:nack_stb_image>)
set(NACK_VENDORED_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
set(NACK_DEFINITIONS "")

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    list(APPEND NACK_SOURCES src/common/nack_egl.cpp src/common/nack_xkb_keys.cpp)
    list(APPEND NACK_LIBRARIES PkgConfig::EGL ${CMAKE_DL_LIBS})

    # For pthread_sigmask, which the Wayland selection write uses to keep a
    # SIGPIPE from killing the application. Current glibc has it in libc, but
    # not every libc or glibc version does.
    find_package(Threads REQUIRED)
    list(APPEND NACK_LIBRARIES Threads::Threads)

    if(NACK_ENABLE_XCB)
        pkg_check_modules(XCB REQUIRED IMPORTED_TARGET
            xcb xcb-xkb xcb-cursor xkbcommon xkbcommon-x11)
        list(APPEND NACK_SOURCES
            src/xcb/nack_xcb.cpp
            src/xcb/nack_xcb_clipboard.cpp)
        list(APPEND NACK_LIBRARIES PkgConfig::XCB)
        list(APPEND NACK_DEFINITIONS NACK_HAS_X11=1)

        if(NACK_XCB_XLIB_FALLBACK)
            pkg_check_modules(X11XCB IMPORTED_TARGET x11-xcb x11)
            if(X11XCB_FOUND)
                list(APPEND NACK_LIBRARIES PkgConfig::X11XCB)
                list(APPEND NACK_DEFINITIONS NACK_XCB_XLIB_FALLBACK=1)
            else()
                message(STATUS
                    "libnack: x11-xcb not found; building pure XCB "
                    "(EGL will require EGL_EXT_platform_xcb)")
            endif()
        endif()
    endif()

    if(NACK_ENABLE_WAYLAND)
        pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET
            wayland-client wayland-egl wayland-cursor xkbcommon)
        pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
        pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
        find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

        include(cmake/WaylandProtocol.cmake)

        set(NACK_WAYLAND_GENERATED "")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/unstable/primary-selection/primary-selection-unstable-v1.xml")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
        nack_wayland_protocol(NACK_WAYLAND_GENERATED
            "${WAYLAND_PROTOCOLS_DIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml")

        # Fractional scaling is newer than some distributions' wayland-protocols.
        set(NACK_FRACTIONAL_XML
            "${WAYLAND_PROTOCOLS_DIR}/staging/fractional-scale/fractional-scale-v1.xml")
        if(EXISTS "${NACK_FRACTIONAL_XML}")
            nack_wayland_protocol(NACK_WAYLAND_GENERATED "${NACK_FRACTIONAL_XML}")
            list(APPEND NACK_DEFINITIONS NACK_HAS_FRACTIONAL_SCALE=1)
        else()
            message(STATUS "libnack: fractional-scale-v1 unavailable; "
                           "Wayland will use integer buffer scale only")
        endif()

        list(APPEND NACK_SOURCES
            src/wayland/nack_wayland.cpp
            src/wayland/nack_wayland_input.cpp
            src/wayland/nack_wayland_clipboard.cpp
            src/wayland/nack_wayland_decor.cpp
            ${NACK_WAYLAND_GENERATED})
        list(APPEND NACK_LIBRARIES PkgConfig::WAYLAND)
        list(APPEND NACK_DEFINITIONS NACK_HAS_WAYLAND=1)
    endif()

    if(NOT NACK_ENABLE_XCB AND NOT NACK_ENABLE_WAYLAND)
        message(FATAL_ERROR "libnack: enable at least one of NACK_ENABLE_XCB / NACK_ENABLE_WAYLAND")
    endif()

elseif(WIN32)
    list(APPEND NACK_SOURCES
        src/win32/nack_win32.cpp
        src/win32/nack_wgl.cpp)
    list(APPEND NACK_LIBRARIES user32 gdi32 shell32 opengl32)
    if(NACK_WIN32_USE_SDK_HEADERS)
        list(APPEND NACK_DEFINITIONS NACK_WIN32_USE_SDK_HEADERS=1)
    endif()

elseif(APPLE)
    list(APPEND NACK_SOURCES
        src/cocoa/nack_cocoa.mm
        src/cocoa/nack_nsgl.mm)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    list(APPEND NACK_LIBRARIES
        ${COCOA_FRAMEWORK} ${QUARTZCORE_FRAMEWORK}
        ${OPENGL_FRAMEWORK} ${IOKIT_FRAMEWORK})
    if(NACK_MACOS_OPENGL_ONLY)
        list(APPEND NACK_DEFINITIONS NACK_MACOS_OPENGL_ONLY=1)
    else()
        find_library(METAL_FRAMEWORK Metal REQUIRED)
        list(APPEND NACK_LIBRARIES ${METAL_FRAMEWORK})
    endif()
endif()

# --------------------------------------------------------------------------
# Library
# --------------------------------------------------------------------------

#
# Compiled once as an object library so the tests can link the objects
# directly. A shared build hides internal symbols, and the smoke test needs one
# of them to read the framebuffer back; linking objects keeps that possible
# without exporting anything the API does not promise.
#
add_library(nack_objects OBJECT ${NACK_SOURCES})

target_include_directories(nack_objects
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_BINARY_DIR}
            ${NACK_VENDORED_INCLUDES})
target_link_libraries(nack_objects PUBLIC fmt::fmt PRIVATE ${NACK_LIBRARIES})
target_compile_definitions(nack_objects PRIVATE ${NACK_DEFINITIONS})

set_target_properties(nack_objects PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON        # POSIX interfaces (poll, pipe) need the GNU dialect
    # The wayland-scanner output in this target is still C.
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set_target_properties(nack_objects PROPERTIES OBJCXX_STANDARD 20)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wstrict-prototypes and -Wmissing-prototypes are C-only and would warn
    # on every C++ file, so they are gone with the C.
    #
    # -Wno-missing-field-initializers: the backend vtables and some Wayland
    # listeners deliberately leave members out - a backend that cannot focus a
    # window has no window_focus, and wl_surface_listener grows members with
    # every protocol release. C++ warns where C did not; C++20 designated
    # initialisers value-initialise the rest, which is exactly what is wanted.
    #
    target_compile_options(nack_objects PRIVATE
        -Wall -Wextra -Wshadow -Wno-unused-parameter
        -Wno-missing-field-initializers
        #
        # -Wpedantic on the C++ only. GCC and Clang accept several things in
        # C++ that the standard does not and MSVC therefore rejects - compound
        # literals above all, which is how NACK_RGB reached MSVC as an error
        # nobody could see here. This turns that class of failure into a local
        # one. Objective-C++ is left out: its own extensions are the point.
        #
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>)
elseif(MSVC)
    # /Zc:__cplusplus so __cplusplus is the standard being compiled rather
    # than 199711L, which is what MSVC reports without it.
    target_compile_options(nack_objects PRIVATE /W4 /wd4100 /Zc:__cplusplus)
    target_compile_definitions(nack_objects PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NACK_BUILD_SHARED)
    add_library(nack SHARED $<TARGET_OBJECTS:nack_objects>
                            ${NACK_VENDORED_OBJECTS})
    target_compile_definitions(nack_objects PUBLIC NACK_SHARED PRIVATE NACK_BUILD)
    target_compile_definitions(nack PUBLIC NACK_SHARED)
    set_target_properties(nack_objects PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
else()
    add_library(nack STATIC $<TARGET_OBJECTS:nack_objects>
                            ${NACK_VENDORED_OBJECTS})
endif()

add_library(nack::nack ALIAS nack)

target_include_directories(nack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(nack PUBLIC fmt::fmt ${NACK_LIBRARIES})

set_target_properties(nack PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON)

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

# The hand-rolled Win32 declarations are only trustworthy if they are checked
# against the real headers, so the check is a build target rather than a
# one-off exercise.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()

    # Links the objects rather than the library: the test reads the
    # framebuffer back through an internal hook to check that frames actually
    # rendered, and a shared build would have hidden that symbol.
    add_executable(nack_smoke tests/console_smoke.cpp
        $<TARGET_OBJECTS:nack_objects> ${NACK_VENDORED_OBJECTS})
    target_link_libraries(nack_smoke PRIVATE ${NACK_LIBRARIES} fmt::fmt)
    target_include_directories(nack_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_BINARY_DIR})
    set_target_properties(nack_smoke PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        # Without this, MSVC reports __cplusplus as 199711L and {fmt} quietly
        # falls back to run-time format checking instead of compile-time.
        target_compile_options(nack_smoke PRIVATE /Zc:__cplusplus)
    endif()
    #
    # X11 selections only take the interesting path when two processes are
    # involved: reading a selection this process owns is served from its own
    # copy and never reaches the server, so the INCR chunking on both sides is
    # unreachable from a single-process test. This peer is the second process.
    # It is not xclip: whether xclip uses INCR depends on the server's maximum
    # request length, and on a server with BIG-REQUESTS a 300K selection goes
    # over in one property, so the test would pass having exercised nothing.
    #
    if(NACK_ENABLE_XCB)
        add_executable(nack_x11_selection_peer tests/x11_selection_peer.c)
        target_link_libraries(nack_x11_selection_peer PRIVATE PkgConfig::XCB)
        set_target_properties(nack_x11_selection_peer PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
        add_dependencies(nack_smoke nack_x11_selection_peer)
        target_compile_definitions(nack_smoke PRIVATE
            NACK_X11_SELECTION_PEER="$<TARGET_FILE:nack_x11_selection_peer>")
    endif()

    add_test(NAME smoke COMMAND nack_smoke)
    # The test tells a machine with no display (exit 77) apart from a real
    # failure; only the first is allowed to pass silently.
    set_tests_properties(smoke PROPERTIES SKIP_RETURN_CODE 77)

    # The same test with the preferred renderer refusing to start. macOS is
    # the only platform with two renderers to choose between and neither the
    # author nor CI develops there, so this is what keeps the fallback honest.
    add_test(NAME smoke_renderer_fallback COMMAND nack_smoke)
    set_tests_properties(smoke_renderer_fallback PROPERTIES
        ENVIRONMENT "NACK_RENDERER=test-fail"
        SKIP_RETURN_CODE 77)

    #
    # A second, smaller smoke test that only exercises the public
    # <nack/nack.hpp> surface (no internal test hooks), so a regression in the
    # public API itself is caught even if console_smoke.cpp's deeper,
    # internals-reaching checks would have papered over it.
    #
    add_executable(nack_cpp_smoke tests/cpp_smoke.cpp
        $<TARGET_OBJECTS:nack_objects> ${NACK_VENDORED_OBJECTS})
    target_link_libraries(nack_cpp_smoke PRIVATE ${NACK_LIBRARIES} fmt::fmt)
    target_include_directories(nack_cpp_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR})
    set_target_properties(nack_cpp_smoke PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(nack_cpp_smoke PRIVATE
            -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        # /Zc:__cplusplus: see the nack_smoke target above.
        target_compile_options(nack_cpp_smoke PRIVATE /W4 /Zc:__cplusplus)
    endif()

    add_test(NAME cpp COMMAND nack_cpp_smoke)
    set_tests_properties(cpp PROPERTIES SKIP_RETURN_CODE 77)

    #
    # A format string that does not match its arguments must be rejected
    # where it is written. That cannot be asserted from inside a program
    # that has to compile, so the bad call lives in its own target which
    # is left out of the default build, and the test is that building it
    # fails. WILL_FAIL inverts the result.
    #
    # An OBJECT library rather than an executable, deliberately: an
    # executable with no main() fails to *link* whatever the format string
    # says, which would make this test pass for a reason that has nothing
    # to do with {fmt}. Only the compile is allowed to decide.
    #
    add_library(nack_cpp_bad_format OBJECT EXCLUDE_FROM_ALL
        tests/cpp_bad_format.cpp)
    target_link_libraries(nack_cpp_bad_format PRIVATE fmt::fmt)
    target_include_directories(nack_cpp_bad_format PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(nack_cpp_bad_format PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        # Without this, {fmt} falls back to run-time checking (see the
        # nack_smoke target above) and this test would pass while proving
        # nothing: the bad format string would compile clean and only fail,
        # if anything, when the program ran - which it never does here.
        target_compile_options(nack_cpp_bad_format PRIVATE /Zc:__cplusplus)
    endif()
    add_test(NAME cpp_format_rejected
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                --target nack_cpp_bad_format --config $<CONFIG>)
    set_tests_properties(cpp_format_rejected PROPERTIES WILL_FAIL TRUE)

    add_executable(nack_image_test tests/image_test.c src/console/nack_image.cpp
        ${NACK_VENDORED_OBJECTS})
    target_include_directories(nack_image_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${NACK_VENDORED_INCLUDES})
    set_target_properties(nack_image_test PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)

    #
    # The PNG fixtures are generated rather than checked in: a decoder tested
    # against images whose expected output came from the same decoder proves
    # nothing, so the pixels come from Python's zlib instead. Without an
    # interpreter the test finds no fixtures and reports itself skipped.
    #
    set(NACK_PNG_FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/image-fixtures)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_custom_command(
            OUTPUT ${NACK_PNG_FIXTURES}/expected.bin
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkpng.py
                    ${NACK_PNG_FIXTURES}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkpng.py
            COMMENT "Generating PNG decoder fixtures")
        add_custom_command(
            OUTPUT ${NACK_PNG_FIXTURES}/expected_jpeg.bin
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkjpeg.py
                    ${NACK_PNG_FIXTURES}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkjpeg.py
            COMMENT "Generating JPEG decoder fixtures")
        add_custom_target(nack_png_fixtures
            DEPENDS ${NACK_PNG_FIXTURES}/expected.bin
                    ${NACK_PNG_FIXTURES}/expected_jpeg.bin)
        add_dependencies(nack_image_test nack_png_fixtures)
    else()
        message(STATUS
            "No Python 3 interpreter: the image decoder test will be skipped")
    endif()

    add_test(NAME image COMMAND nack_image_test ${NACK_PNG_FIXTURES})
    set_tests_properties(image PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(WIN32 AND NOT NACK_WIN32_USE_SDK_HEADERS AND PROJECT_IS_TOP_LEVEL)
    add_executable(nack_win32_abi_check
        tests/win32_abi_main.c
        tests/win32_abi_sdk.c
        tests/win32_abi_nack.c)
    target_include_directories(nack_win32_abi_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    set_target_properties(nack_win32_abi_check PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
    add_test(NAME win32_abi COMMAND nack_win32_abi_check)
endif()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------

if(NACK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------

# Skipped under NACK_FMT_EXTERNAL: see the {fmt} comment above.
if(NOT NACK_FMT_EXTERNAL)
    install(TARGETS nack
        EXPORT nackTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/nack DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT nackTargets
        FILE nackTargets.cmake
        NAMESPACE nack::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nack)
endif()
