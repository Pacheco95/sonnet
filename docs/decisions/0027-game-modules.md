# ADR-0027: A game module is a static library the engine build links into a player and an editor

- **Status:** Accepted
- **Date:** 2026-10-12

## Context

[ADR-0007](0007-data-driven-game-structure.md) made a game a data-driven project run by a generic player, and named the way native code comes back: a project's own C++, linked into a player built for that project. [M15](../roadmap.md#m15-game-ui-and-the-game-module-hook) schedules it, desktop first, with a sample whose C++ component and system a Lua script reads, played in the editor and exported on the three desktops.

Constraints found in the code:

- The engine is thirteen static libraries (`sonnet_add_module` in `cmake/SonnetFunctions.cmake`), linked privately to their third-party dependencies, three of which come from overlay ports in `ports/` (`ktx`, `imgui`, `moltenvk`). Nothing is installed: there is no `install()` rule and no package configuration.
- A component is registered with reflection through `World::registerComponent`, and `physics`, `gui` and `scripting` register theirs that way, so the inspector, the serializer, undo, prefabs and Lua (`entity:get`, `entity:set`, through `scripting/src/LuaReflection.cpp`) handle any registered component without code of their own. A system is a flecs system in one of `World`'s phases; `World::addToSimulation` makes it run only while the world plays, as `SpinSystem` does.
- A scene that names a component nobody registered loads with a warning and without that component (`world/src/Scene.cpp`). An editor that opened such a scene and saved it would drop the data silently.
- The player (`apps/player`) and the editor (`modules/editor`) each build their own world and its systems: `runtime::Game` and `editor::Editor` construct `World`, `GuiSystem`, physics, animation, scripting and audio separately, and both load scenes through `world::loadScene`. `runtime` comes before `ui` and `editor` in the dependency order.
- `project.json` is version 1 (`assets::ProjectFileVersion`) and a newer one is refused ([#153](https://github.com/Pacheco95/sonnet/issues/153)). The bundle is version 5.
- The editor's export copies the player binary that sits next to the editor ([editor.md](../editor.md#export)). The Android APK and the iOS app bundle are built by CMake around a packaged bundle, from the same presets as everything else.
- flecs keeps a C++ type's component id in a static of a template, per binary. spdlog's registry, `core::Log`'s sinks and the job system are process singletons held in statics. A shared library linked against its own copy of the engine has its own copies of all of them.

## Decision

**A game module is one static library per project, with one entry point, `registerGame(runtime::GameRegistry &)`. The engine's build takes the project in, `-DSONNET_GAME=<project folder>`, and links the module into the player and the editor it builds. Nothing is loaded at run time, and the engine is not installed as a package.**

- *The module.* The project holds its C++ under `native/`, with a `native/CMakeLists.txt` that calls `sonnet_add_game_module(<name> SOURCES ...)`. That creates a static library linked to `sonnet::runtime` (and through it every module up to `runtime`, never `ui` or `editor`, which `tools/check_dependencies.py` checks) and defines the entry point the player and the editor call:

  ```cpp
  void registerGame(sonnet::runtime::GameRegistry &registry);
  ```

- *The registry.* `runtime::GameRegistry` is what a module may touch, and is created by whoever owns the world: `runtime::Game` and `editor::Editor`, in their constructors, after the engine's own systems and components are registered and before any scene loads, so a scene that names a module's component reads it.
  - `world()`: the `World`, for `registerComponent` (the components then reach the inspector, the serializer, prefabs and Lua through reflection, with nothing more) and for flecs systems in `World`'s phases, play-only through `addToSimulation` as the engine's own are.
  - `bindLua(std::function<void(sol::state_view)>)`: a function called on every Lua state the script runtime creates, before any script runs, for native functions and types that reflection does not cover. Components need none of it.
  - Nothing else in the first version. A module that wants the renderer, physics or audio waits until a sample asks.
- *The build.* `SONNET_GAME` is a cache variable of the engine's top-level `CMakeLists.txt`, empty by default. When set, the engine reads the project's `project.json` for the module's name, `add_subdirectory`s its `native/`, and links the module into `sonnet_player_app` and, when the editor is built, `sonnet_editor_app`, with a generated source that calls the module's `registerGame`. One build directory builds one game; without `SONNET_GAME` the player and the editor are the generic ones, unchanged. The presets, vcpkg's manifest and the overlay ports are the engine's, so a module builds wherever the engine does, Android and iOS included. Version and ABI checks are not needed: the module, the engine and the player are compiled in one tree by one compiler.
- *The project file.* `project.json` gains `"module": "<name>"`, and its version becomes 2. A version 1 file has no module and reads as before.
- *Refusal.* A player or an editor knows the one module it was linked with, or none. Opening a project or a bundle whose `module` is not the linked one is an error that names both (`project "Hook" needs the game module "hook"; this editor was built without one`), in `Project::open`'s callers and in the bundle's loader, so a generic editor never opens a module's scene and never drops its data on save. A binary with a module still opens a project without one, with the module's components registered and unused. The cook writes the module's name into the bundle's manifest, and the bundle's version becomes 6.
- *Export.* The editor's export copies the player beside it, which in a module build is the module's player: the code path does not change. The Android APK and the iOS app bundle are built in the same configure with `SONNET_GAME` set, as the generic ones are. M15 verifies the desktops; the phones get the same check later, since nothing in this design differs for them.
- *The sample.* `apps/samples/hook` is a small project with a C++ component (`Orbit { radius, speed }`) and a system that moves its entity, a Lua script that reads the component and shows it in a label, and a scene with both. A CI job on each desktop configures with `-DSONNET_GAME=apps/samples/hook`, builds, and runs a test that plays the scene in `runtime::Game` and in the editor and finds the same positions and label text; the generic jobs check that the player and the editor refuse the project.

Not in the first version: more than one module per project; loading a module into a prebuilt editor; hot reload of native code (a rebuild and a restart of the editor is the cost); a module reaching `ui`, `editor` or the editor's panels; installing the engine as a package for a project to build against; debugging support beyond the platform's own.

## Consequences

- A project with a module is developed in the engine's checkout, with its compiler, vcpkg and presets, the same set-up a contributor to the engine has ([build.md](../build.md)). Someone who writes C++ for a game has a compiler already; someone who does not is not affected, since the generic player and editor do not change.
- What plays in the editor is what ships, as ADR-0007 required: both binaries link the same library and call the same entry point.
- Nothing crosses a binary boundary, so flecs' per-binary type ids, the logging and job-system singletons, the C++ ABI between compilers and the symbol export rules of three platforms do not need answers.
- Changing the module's code means rebuilding and restarting the editor. This is slower than a hot reload and the same as working on the engine itself.
- One build directory serves one game, so working on two module projects is two build directories (a preset each, or `-DSONNET_GAME` on the command line).
- A scene saved by a module's editor names the module's components; the refusal turns "a generic editor dropped them" into an error before anything is read.
- `project.json` version 2 and bundle version 6 are format changes, made before the [1.0.0](../roadmap.md#100) freeze, as the roadmap asks of M15.
- The roadmap's hook section changes with this ADR: no `install(EXPORT)`, no `find_package(Sonnet)`, no ABI record and no shared library in the editor; the criterion "builds against the installed engine in CI" becomes "builds with `SONNET_GAME` in CI". The hook is therefore smaller than the roadmap sized it, and stays in M15 rather than splitting into M16.
- Distributing the engine as a binary SDK, for people who want native code without building the engine, is left for when someone asks; it would need the package and the shared-library questions this decision avoids.

## Alternatives considered

- **The engine installed as a package, the game a separate CMake project with `find_package(Sonnet)`** (the roadmap's plan). It needs an exported configuration for thirteen modules and their private third-party links, a `find_dependency` for each of about twenty vcpkg packages with the same versions and overlay ports in the game's manifest, multi-configuration packages, and the mobile toolchains in the consumer. That is a large surface to keep working for one benefit, building a game without the engine's source, which nobody needs yet.
- **The editor loads the module as a shared library** (the roadmap's plan for play mode). The module would need the engine's symbols from the editor's process: exported from an executable on Windows, within its limit of 65,535 exports, and resolved at load time on Linux and macOS. Its flecs type ids, logging sinks and job system would also be copies of the editor's unless the engine itself became shared libraries. It buys skipping the editor's rebuild, but without hot reload the editor restarts anyway.
- **One shared engine library and modules loaded at run time everywhere.** This solves the singleton problem, but it changes how all thirteen modules build and how the player is packaged on five platforms, for a feature that has one sample.
- **Native code as Lua C modules (`require` of a shared library).** Lua could load them on the desktops, but iOS forbids loading code at run time, and a C module cannot register components or systems with the world.
- **Several modules per project.** One is enough for a game, and a project that wants more can link libraries into its one module.
