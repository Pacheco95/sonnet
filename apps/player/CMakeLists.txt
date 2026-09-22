# The generic runtime an exported game ships as (docs/player.md). It links no editor code and,
# in release, no Dear ImGui; the engine shaders go next to it and export copies them along.
sonnet_add_executable(player
  SOURCES
    main.cpp
  DEPENDS
    sonnet::runtime
)
sonnet_add_engine_shaders(sonnet_player_app)

if(SONNET_BUILD_EDITOR)
  # The export dialog looks for the player binary in the editor's directory (docs/editor.md#export).
  add_custom_command(TARGET sonnet_player_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:sonnet_player_app>
      $<TARGET_FILE_DIR:sonnet_editor_app>/$<TARGET_FILE_NAME:sonnet_player_app>
    COMMENT "Copying player binary next to editor for export"
  )
endif()
