# ADR-0007: Games are data-driven projects run by a generic player

- **Status:** Accepted
- **Date:** 2026-09-12

## Context

"Export to multiple platforms" needs a definition. There are two shapes a game can take: C++ code compiled against the engine, where exporting means building the game for each target with that platform's compiler; or a data-driven project (scenes, assets, scripts) that a prebuilt player binary runs, where exporting means cooking the data and copying the right player. The first requires an iOS or Android toolchain every time a user exports; the second requires only that the engine ships a player per platform. The previous iteration's editor plan already leaned on a project manifest, JSON scenes and Lua scripts.

## Decision

A game is a project folder: `project.json`, scenes, assets, shaders and scripts. The player (`apps/player`) is a generic runtime, built once per platform, that opens a project or its cooked bundle and runs the start scene. Gameplay logic is expressed in scripts (Lua, M4) and data. Export cooks the project for the target and packages it with that platform's player. Native C++ game code is not supported in the first version; a game-module hook that lets a project link its own C++ into a custom player is listed under "Later" in the roadmap.

## Consequences

- Exporting never needs a compiler on the user's machine, and mobile export is a packaging task.
- Reflection and serialization must be complete, because everything a game is made of passes through them. This aligns with the editor-first goal.
- Scripting performance limits what a game can do until the C++ hook exists. Heavy systems (physics, animation, rendering) are engine code, so scripts orchestrate rather than compute.
- The editor and the player share the same loading path, so what runs in play mode is what ships.

## Alternatives considered

- **C++ game project linking the engine**: maximum flexibility, but export requires per-platform toolchains for every user and the editor would need to hot-reload native code. Deferred to the game-module hook.
- **Both from the start**: two integration paths to keep working; premature for a personal engine.
