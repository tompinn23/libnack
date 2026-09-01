cmake_minimum_required(VERSION 3.21)

project(libnack
    VERSION 0.2.0
    DESCRIPTION "A cell console for roguelikes"
    LANGUAGES C)

if(APPLE)
    enable_language(OBJC)
endif()

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

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------

set(NACK_SOURCES
    src/nack.c
    src/nack_keys.c
    src/common/nack_proc_cache.c
    src/console/nack_api.c
    src/console/nack_console.c
    src/console/nack_png.c
    src/console/nack_render.c
    src/console/nack_tileset.c
    src/console/nack_gfx.c
    src/console/nack_gfx_gl.c
    src/console/nack_gl.c)

# macOS gets both renderers and picks between them at run time: Metal first,
# because Apple deprecated OpenGL and capped it at 4.1, but OpenGL still there
# to fall back on. Everywhere else there is only OpenGL.
if(APPLE AND NOT NACK_MACOS_OPENGL_ONLY)
    list(APPEND NACK_SOURCES src/console/nack_gfx_metal.m)
endif()

set(NACK_LIBRARIES "")
set(NACK_DEFINITIONS "")

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    list(APPEND NACK_SOURCES src/common/nack_egl.c src/common/nack_xkb_keys.c)
    list(APPEND NACK_LIBRARIES PkgConfig::EGL ${CMAKE_DL_LIBS})

    if(NACK_ENABLE_XCB)
        pkg_check_modules(XCB REQUIRED IMPORTED_TARGET
            xcb xcb-xkb xcb-cursor xkbcommon xkbcommon-x11)
        list(APPEND NACK_SOURCES
            src/xcb/nack_xcb.c
            src/xcb/nack_xcb_clipboard.c)
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
            src/wayland/nack_wayland.c
            src/wayland/nack_wayland_input.c
            src/wayland/nack_wayland_clipboard.c
            src/wayland/nack_wayland_decor.c
            ${NACK_WAYLAND_GENERATED})
        list(APPEND NACK_LIBRARIES PkgConfig::WAYLAND)
        list(APPEND NACK_DEFINITIONS NACK_HAS_WAYLAND=1)
    endif()

    if(NOT NACK_ENABLE_XCB AND NOT NACK_ENABLE_WAYLAND)
        message(FATAL_ERROR "libnack: enable at least one of NACK_ENABLE_XCB / NACK_ENABLE_WAYLAND")
    endif()

elseif(WIN32)
    list(APPEND NACK_SOURCES
        src/win32/nack_win32.c
        src/win32/nack_wgl.c)
    list(APPEND NACK_LIBRARIES user32 gdi32 shell32 opengl32)
    if(NACK_WIN32_USE_SDK_HEADERS)
        list(APPEND NACK_DEFINITIONS NACK_WIN32_USE_SDK_HEADERS=1)
    endif()

elseif(APPLE)
    list(APPEND NACK_SOURCES
        src/cocoa/nack_cocoa.m
        src/cocoa/nack_nsgl.m)
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
            ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nack_objects PRIVATE ${NACK_LIBRARIES})
target_compile_definitions(nack_objects PRIVATE ${NACK_DEFINITIONS})

set_target_properties(nack_objects PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON          # POSIX interfaces (poll, pipe) need the GNU dialect
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set_target_properties(nack_objects PROPERTIES OBJC_STANDARD 11)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nack_objects PRIVATE
        -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
        -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(nack_objects PRIVATE /W4 /wd4100)
    target_compile_definitions(nack_objects PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NACK_BUILD_SHARED)
    add_library(nack SHARED $<TARGET_OBJECTS:nack_objects>)
    target_compile_definitions(nack_objects PUBLIC NACK_SHARED PRIVATE NACK_BUILD)
    target_compile_definitions(nack PUBLIC NACK_SHARED)
    set_target_properties(nack_objects PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
else()
    add_library(nack STATIC $<TARGET_OBJECTS:nack_objects>)
endif()

add_library(nack::nack ALIAS nack)

target_include_directories(nack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(nack PUBLIC ${NACK_LIBRARIES})

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
    add_executable(nack_smoke tests/console_smoke.c $<TARGET_OBJECTS:nack_objects>)
    target_link_libraries(nack_smoke PRIVATE ${NACK_LIBRARIES})
    target_include_directories(nack_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR})
    set_target_properties(nack_smoke PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
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

    add_executable(nack_png_test tests/png_test.c src/console/nack_png.c)
    target_include_directories(nack_png_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set_target_properties(nack_png_test PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)

    #
    # The PNG fixtures are generated rather than checked in: a decoder tested
    # against images whose expected output came from the same decoder proves
    # nothing, so the pixels come from Python's zlib instead. Without an
    # interpreter the test finds no fixtures and reports itself skipped.
    #
    set(NACK_PNG_FIXTURES ${CMAKE_CURRENT_BINARY_DIR}/png-fixtures)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_custom_command(
            OUTPUT ${NACK_PNG_FIXTURES}/expected.bin
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkpng.py
                    ${NACK_PNG_FIXTURES}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkpng.py
            COMMENT "Generating PNG decoder fixtures")
        add_custom_target(nack_png_fixtures
            DEPENDS ${NACK_PNG_FIXTURES}/expected.bin)
        add_dependencies(nack_png_test nack_png_fixtures)
    else()
        message(STATUS
            "No Python 3 interpreter: the PNG decoder test will be skipped")
    endif()

    add_test(NAME png COMMAND nack_png_test ${NACK_PNG_FIXTURES})
    set_tests_properties(png PROPERTIES SKIP_RETURN_CODE 77)
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

include(GNUInstallDirs)

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
