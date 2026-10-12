# The hook sample's game module (ADR-0027): built when the engine is configured with
# -DSONNET_GAME=apps/samples/hook, and linked into the player and the editor it builds.
sonnet_add_game_module(hook
  SOURCES
    Hook.cpp
)
