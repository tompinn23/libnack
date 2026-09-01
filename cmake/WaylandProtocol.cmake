# Generates the client glue for a Wayland protocol XML file and appends the
# resulting .c to `out_var`. The header lands in the build directory, which is
# on the target's private include path.
function(nack_wayland_protocol out_var xml)
    if(NOT EXISTS "${xml}")
        message(FATAL_ERROR "libnack: wayland protocol not found: ${xml}")
    endif()

    get_filename_component(basename "${xml}" NAME_WE)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${basename}-client-protocol.h")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${basename}-protocol.c")

    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${header}"
        DEPENDS "${xml}"
        COMMENT "wayland-scanner client-header ${basename}"
        VERBATIM)

    add_custom_command(
        OUTPUT "${source}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${source}"
        DEPENDS "${xml}" "${header}"
        COMMENT "wayland-scanner private-code ${basename}"
        VERBATIM)

    set(${out_var} ${${out_var}} "${header}" "${source}" PARENT_SCOPE)
endfunction()
