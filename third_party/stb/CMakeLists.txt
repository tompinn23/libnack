#
# stb_image: one public-domain header that decodes both PNG and JPEG.
#
# stb_image.c is libnack's, not upstream's - it exists to compile the
# implementation once and to pin the configuration. See VENDORING.md.
#
add_library(nack_stb_image OBJECT stb_image.c)

target_include_directories(nack_stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(nack_stb_image PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

# Third-party code is not ours to clean up, and its warnings would bury the
# ones that are.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(nack_stb_image PRIVATE -w)
elseif(MSVC)
    target_compile_options(nack_stb_image PRIVATE /w)
    target_compile_definitions(nack_stb_image PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
