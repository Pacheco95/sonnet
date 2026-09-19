# Modules in dependency order. This order is the canonical statement of the architecture
# (docs/architecture.md): a module may link only modules listed above it.
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(rhi)
add_subdirectory(renderer)
add_subdirectory(assets)
add_subdirectory(world)
add_subdirectory(physics)
add_subdirectory(scripting)
# Editor-only modules; the player never links them (docs/architecture.md, "Editor and player").
if(SONNET_BUILD_EDITOR)
  add_subdirectory(ui)
  add_subdirectory(editor)
endif()
