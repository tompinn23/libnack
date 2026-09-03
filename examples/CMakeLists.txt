function(nack_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE nack)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${name} PRIVATE /Zc:__cplusplus)
    endif()
endfunction()

nack_example(hello)
nack_example(roguelike)
