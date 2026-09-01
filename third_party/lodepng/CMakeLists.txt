#
# LodePNG. One translation unit, no zlib: it carries its own inflate.
#
# Upstream ships it as lodepng.cpp, which is also valid C - the C++ API is
# behind LODEPNG_COMPILE_CPP, so the file compiles as C99 once renamed. It is
# renamed here rather than compiled as C++, because this project has no C++ in
# it. See VENDORING.md.
#
add_library(nack_lodepng OBJECT lodepng.c)

target_include_directories(nack_lodepng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#
# Only decoding is wanted. Dropping the encoder and the disk helpers removes
# code that would otherwise be reachable from nothing, and the C++ wrapper
# cannot compile here anyway. Error text stays: it is what a bad tileset
# reports back to the caller.
#
target_compile_definitions(nack_lodepng PRIVATE
    LODEPNG_NO_COMPILE_ENCODER
    LODEPNG_NO_COMPILE_DISK
    LODEPNG_NO_COMPILE_CPP)

set_target_properties(nack_lodepng PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

# Third-party code is not ours to clean up, and its warnings would bury the
# ones that are.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nack_lodepng PRIVATE -w)
elseif(MSVC)
    target_compile_options(nack_lodepng PRIVATE /w)
    target_compile_definitions(nack_lodepng PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
