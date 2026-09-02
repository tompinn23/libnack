#
# {fmt}, header-only, for the C++ header's formatting.
#
# Nothing is compiled here: FMT_HEADER_ONLY makes fmt entirely inline, so this
# is an interface target that carries the include path and that macro to
# whoever includes <nack/nack.hpp>. The C library does not use it and does not
# link it - a C program still needs no C++ runtime at all.
#
add_library(nack_fmt INTERFACE)

target_include_directories(nack_fmt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(nack_fmt INTERFACE FMT_HEADER_ONLY=1)

#
# {fmt} static_asserts that MSVC is building with /utf-8, because without it
# MSVC reads the source in the system code page and its Unicode handling is
# wrong. GCC and Clang are UTF-8 already and need nothing. INTERFACE, because
# it is whoever includes the header that has to be compiled that way.
#
#
# /Zc:__cplusplus makes MSVC report the standard it is actually compiling.
# Without it __cplusplus is stuck at 199711L for backwards compatibility, and
# {fmt} reads that to decide whether consteval format checking is available -
# so a C++20 build would silently fall back to checking at run time, and the
# cpp_format_rejected test would have nothing to catch.
#
if(MSVC)
    target_compile_options(nack_fmt INTERFACE /utf-8 /Zc:__cplusplus)
endif()
