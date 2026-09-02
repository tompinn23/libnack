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
