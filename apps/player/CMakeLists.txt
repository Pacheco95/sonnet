# The generic runtime an exported game ships as (docs/player.md). It links no editor code and,
# in release, no Dear ImGui; the engine shaders go next to it and export copies them along.
sonnet_add_executable(player
  SOURCES
    main.cpp
  DEPENDS
    sonnet::runtime
)
sonnet_add_engine_shaders(sonnet_player_app)
