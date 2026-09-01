function(nack_example name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nack)
    set_target_properties(${name} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON)
    if(UNIX AND NOT APPLE)
        target_link_libraries(${name} PRIVATE m)
    endif()
endfunction()

nack_example(hello)
nack_example(roguelike)
