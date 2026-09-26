# sonnet_add_shaders(<target> SHADERS <file.slang> ...)
#
# Compiles each .slang file with slangc at build time into <target's output directory>/shaders/
# <name>.spv, one SPIR-V 1.6 module per file holding every [shader("...")] entry point under its
# own name (-fvk-use-entrypoint-name). Buffers use scalar block layout so structs are shared with
# C++ unchanged (docs/rendering.md, "Vulkan baseline"). Release builds therefore carry no shader
# compiler. The depfile makes edits to imported modules rebuild their users.
# Only the editor imports the target library; all rendering binaries need this host tool.
# vcpkg puts the target triplet's tools in CMAKE_PROGRAM_PATH, and the host triplet's only when
# VCPKG_HOST_TRIPLET is set, which its toolchain never does itself. On Android the manifest installs
# shader-slang for the host triplet alone, so a cross build adds that directory here. It has to be
# CMAKE_PROGRAM_PATH rather than HINTS: the Vulkan SDK's environment exports CMAKE_PREFIX_PATH, which
# CMake searches before HINTS, and the SDK's slangc is not the version the manifest pins.
if(CMAKE_CROSSCOMPILING AND DEFINED VCPKG_INSTALLED_DIR)
  if(VCPKG_HOST_TRIPLET)
    set(_sonnet_host_slang "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/shader-slang")
  else()
    file(GLOB _sonnet_host_slang LIST_DIRECTORIES true "${VCPKG_INSTALLED_DIR}/*/tools/shader-slang")
  endif()
  list(APPEND CMAKE_PROGRAM_PATH ${_sonnet_host_slang})
  unset(_sonnet_host_slang)
endif()
find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)

# Engine shader modules import each other by name; every compilation sees this directory.
set(SONNET_ENGINE_SHADER_DIR "${CMAKE_SOURCE_DIR}/modules/renderer/shaders")

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

# sonnet_add_engine_shaders(<target>)
#
# Compiles the engine's shaders (modules/renderer/shaders, registered by the renderer module as
# the SONNET_ENGINE_SHADERS global property) next to <target>'s binary. Every executable and
# test that renders through the renderer calls it, because a static library has no binary
# directory of its own for the modules to land in.
function(sonnet_add_engine_shaders TARGET)
  get_property(shaders GLOBAL PROPERTY SONNET_ENGINE_SHADERS)
  if(NOT shaders)
    message(FATAL_ERROR "sonnet_add_engine_shaders(${TARGET}): the renderer module has not registered its shaders")
  endif()
  sonnet_add_shaders(${TARGET} SHADERS ${shaders})
endfunction()
