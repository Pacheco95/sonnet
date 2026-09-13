# sonnet_add_shaders(<target> SHADERS <file.slang> ...)
#
# Compiles each .slang file with slangc at build time into <target's output directory>/shaders/
# <name>.spv, one SPIR-V 1.6 module per file holding every [shader("...")] entry point under its
# own name (-fvk-use-entrypoint-name). Buffers use scalar block layout so structs are shared with
# C++ unchanged (docs/rendering.md, "Vulkan baseline"). Release builds therefore carry no shader
# compiler. The depfile makes edits to imported modules rebuild their users.
find_package(slang CONFIG REQUIRED)

function(sonnet_add_shaders TARGET)
  cmake_parse_arguments(ARG "" "" "SHADERS" ${ARGN})
  set(outputs)
  foreach(source IN LISTS ARG_SHADERS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(absolute "${source}" ABSOLUTE)
    get_filename_component(source_dir "${absolute}" DIRECTORY)
    set(output "$<TARGET_FILE_DIR:${TARGET}>/shaders/${name}.spv")
    # A stable path for the depfile and the output rule: the generator expression is resolved
    # by the command, the rule itself is keyed on this path under the binary directory.
    set(rule_output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${name}.spv")
    add_custom_command(
      OUTPUT "${rule_output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
      COMMAND "${SLANGC_EXECUTABLE}" "${absolute}"
              -target spirv -profile spirv_1_6 -fvk-use-entrypoint-name -fvk-use-scalar-layout
              -matrix-layout-column-major
              -I "${source_dir}" -I "${SONNET_ENGINE_SHADER_DIR}"
              $<IF:$<CONFIG:Debug>,-g2,-O2>
              -depfile "${rule_output}.d"
              -o "${rule_output}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${rule_output}" "${output}"
      DEPENDS "${absolute}"
      DEPFILE "${rule_output}.d"
      COMMENT "slangc ${name}.slang"
      VERBATIM)
    list(APPEND outputs "${rule_output}")
  endforeach()
  add_custom_target(${TARGET}_shaders DEPENDS ${outputs})
  add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()
