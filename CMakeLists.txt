cmake_minimum_required(VERSION 3.21)

project(libnack
    VERSION 0.1.0
    DESCRIPTION "Cross-platform windowing and OpenGL context library"
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
    src/common/nack_proc_cache.c)

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
endif()

# --------------------------------------------------------------------------
# Library
# --------------------------------------------------------------------------

if(NACK_BUILD_SHARED)
    add_library(nack SHARED ${NACK_SOURCES})
    target_compile_definitions(nack PUBLIC NACK_SHARED PRIVATE NACK_BUILD)
    set_target_properties(nack PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
else()
    add_library(nack STATIC ${NACK_SOURCES})
endif()

add_library(nack::nack ALIAS nack)

target_include_directories(nack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(nack PRIVATE ${NACK_LIBRARIES})
target_compile_definitions(nack PRIVATE ${NACK_DEFINITIONS})

set_target_properties(nack PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON          # POSIX interfaces (poll, pipe) need the GNU dialect
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set_target_properties(nack PROPERTIES OBJC_STANDARD 11)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nack PRIVATE
        -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes
        -Wno-unused-parameter)
elseif(MSVC)
    target_compile_options(nack PRIVATE /W4 /wd4100)
    target_compile_definitions(nack PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------

# The hand-rolled Win32 declarations are only trustworthy if they are checked
# against the real headers, so the check is a build target rather than a
# one-off exercise.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()

    add_executable(nack_smoke tests/smoke.c)
    target_link_libraries(nack_smoke PRIVATE nack)
    set_target_properties(nack_smoke PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
    add_test(NAME smoke COMMAND nack_smoke)

    if(WIN32)
        add_executable(nack_win32_smoke tests/win32_smoke.c)
        target_link_libraries(nack_win32_smoke PRIVATE nack)
        set_target_properties(nack_win32_smoke PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
        add_test(NAME win32_smoke COMMAND nack_win32_smoke)
    endif()
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
