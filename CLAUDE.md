# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Sonnet is a C++23 3D game engine (Vulkan 1.4 only, SDL3, flecs, Slang, Dear ImGui) with an editor and a generic player, targeting Windows, Linux, macOS, Android and iOS. It is the third iteration of the engine and was restarted docs-first; code lands milestone by milestone following `docs/roadmap.md`. M0 (build, `core`, `platform`, `rhi`, triangle), M1 (editor shell: `ui`, `renderer` with the render graph, `editor`), M2 (`world` with flecs, scenes and prefabs; the editor's hierarchy and inspector, gizmos, picking, undo/redo, play mode, projects; the basic sample), M3 (`assets` with the database, glTF and image import, KTX2 cooking and hot reload; the clustered forward renderer with PBR, shadows, IBL and post-processing; the editor's asset browser, material editing and shader hot reload), M4 (`physics` on Jolt, Lua `scripting` with sol2, the world's fixed timestep, play mode running both, the playground sample), M5 (`audio` on miniaudio, skeletal animation in `world`, GPU skinning) M6 (`runtime` with the generic player, the cooked bundle, `sonnet_cook` and the editor's export), M7 (GPU culling in a compute pass feeding indirect draws batched per pipeline and mesh) and M8 (the job system and asynchronous loading) are done; M9 (the shared mobile work and Android export) is next, then M10 (iOS export).

The docs are the source of truth. Read the relevant one before a non-trivial change, and update it in the same change:

| Before touching | Read |
|---|---|
| Anything | `docs/architecture.md` (module map, dependency rule), `docs/conventions.md` |
| `core` | `docs/core.md` |
| `platform` | `docs/platform.md` |
| `rhi`, `renderer`, shaders | `docs/rendering.md` |
| `world`, scene and prefab files | `docs/world.md` |
| `ui`, `editor`, `apps/editor` | `docs/editor.md` |
| `assets`, file formats | `docs/assets.md` |
| `physics` | `docs/physics.md` |
| `audio` | `docs/audio.md` |
| `runtime`, `apps/player`, `apps/cook`, export | `docs/player.md` |
| `scripting`, the Lua API | `docs/scripting.md` |
| CMake, vcpkg, CI | `docs/build.md` |
| A cross-module decision | `docs/decisions/` (ADRs and the template) |

Accepted ADRs are settled. Do not relitigate them in code; a change of direction is a new ADR that supersedes the old one, and an accepted ADR is only ever edited to change its status.

## Commands

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-debug             # also windows-debug, macos-debug, linux-asan, linux-coverage
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
ctest --preset linux-debug -R core_tests            # one module's tests
# one case or tag: run the <module>_tests binary directly, e.g. core_tests "[handle]"
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic   # right-drag the viewport, WASD/QE; W/E/R gizmos, Ctrl+P play
./build/linux-debug/apps/editor/sonnet_editor --help                # the capture flags below, the shading terms, the exit codes
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ctest --preset linux-debug -R rhi_tests   # what CI runs: Lavapipe
python3 tools/check_docs.py            # after editing any Markdown: links, anchors, cross-doc consistency
python3 tools/check_version.py         # vcpkg.json must mirror project(sonnet VERSION ...)
git ls-files '*.h' '*.cpp' | xargs clang-format --dry-run --Werror   # CI rejects unformatted code
sh tools/install_hooks.sh              # once per clone: commit-msg hook for Conventional Commits
```

## Seeing what the engine draws

Do not screen-capture the editor: macOS needs a permission an agent cannot grant, and a headless machine has no screen. The editor writes its own screenshots and quits, exiting 0 once the files exist and 1 with the reason in the log ([docs/editor.md](docs/editor.md#screenshots)):

```bash
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic \
  --scene scenes/playground.scene.json --play 3 --select Ball --shading-term final \
  --screenshot shots/view.png --screenshot-window shots/window.png
```

`--screenshot` is the viewport's scene and `--screenshot-window` the whole window with its panels. `--scene` is relative to the project, `--play` takes seconds, not steps (`--play 3` is 180 fixed 1/60 s steps, so a run repeats exactly), `--select` takes an entity path of names (`Parent/Child`) and outlines it, and `--shading-term` shows one lighting term (`albedo`, `normal`, `shadow-factor`, ...). Then read the PNG. A task for another machine's agent asks for these files rather than for screenshots of the screen. `sonnet_editor --help` lists every flag, and `tools/check_docs.py` fails if one is missing from the docs.

Machine-specific notes (tool locations, checkouts of the previous iterations to adapt patterns from) live in `CLAUDE.local.md`, which is gitignored.

## Architecture in one page

- **Modules in dependency order**: `core → platform → rhi → renderer → assets → world → physics → scripting → audio → runtime → ui → editor`. A module links only modules earlier in the dependency order; `modules/CMakeLists.txt` adds them in this order and that order is the canonical statement of the architecture. `apps/editor`, `apps/player` and `apps/cook` are executables; `apps/samples/*` are project folders.
- **Interfaces live with their owner.** There is no `api` module. `platform` holds `IWindow` and the SDL3 implementation; `rhi` holds the device interfaces and the Vulkan implementation. Upward communication uses interfaces, callbacks and data handed down, never a dependency.
- **One `#if`-switched site**: the `rhi` device factory. The documented exception is `ui`, which links `sonnet::rhi_vulkan` to include the Vulkan implementation headers for Dear ImGui's backend.
- **The engine does not own `main()`.** It implements SDL3's callback model so desktop and mobile share one lifecycle. Frame ordering lives in the app, not in modules.
- **ECS**: flecs. Hierarchy is `ChildOf`, prefabs and nested scenes are `IsA`, components are plain structs registered with flecs reflection, which drives the inspector, scene JSON and scripting bindings. flecs types appear unwrapped in `world` headers by decision. Scene entities carry a UUID `Identity`; the editor's selection and undo commands refer to entities by it, never by flecs id. `world` links `renderer` until `assets` exists.
- **Handles**: `core::Handle<Tag>` is a 32-bit index plus 32-bit generation; components store handles, owners resolve them.
- **Subsystems register into the world** (ADR-0009): `physics` and `scripting` register their components through `World::registerComponent` and their systems in the world's phases, marked as simulation so they run in play mode only. `FixedUpdate` runs at a fixed step from an accumulator; physics steps there and interpolates dynamic bodies' `Transform` in `PostUpdate`. Bodies follow the ECS, never the other way round: static and kinematic bodies take their entity's pose, and an outside write to a dynamic body's `Transform` teleports it. Scripts are `.lua` assets whose returned table is the class of a per-entity instance; components cross into Lua through flecs reflection, and Lua is compiled as C++ so errors unwind.
- **Editor versus player**: the player never links `editor`, nor `ui` in release. Play mode snapshots the world on play and restores it on stop; every editor edit is an `ICommand` on the `CommandStack`, and the inspector and gizmo edit live and push one command when the widget is released.
- **Rendering**: vk-bootstrap creates, Vulkan-HPP RAII wrappers own, `vkb::destroy_*` is never called, RAII members are declared in reverse destruction order. The render graph is an engine module. Shaders are Slang only, compiled by `slangc` at build time through `sonnet_add_shaders` and at runtime in the editor for hot reload; Slang reflection is the only shader reflection source. Set 0 is the bindless set, set 1 is push descriptors, push constants carry what varies per pass. Reversed-Z, negative viewport height for the Y flip, counter-clockwise front faces. Opaque draws are culled in a compute pass and submitted indirectly (ADR-0012), one instanced command per batch of draws sharing a pipeline, a front face, a mesh and a submesh; survivors append their object index to a visible list, and shaders read `visible[SV_StartInstanceLocation + SV_InstanceID]`. No draw count is used, so MoltenVK takes the same path (ADR-0016). Blended draws keep the direct path, naming their slot in the visible list's direct range as the first instance.
- **rhi per-frame rules**: `beginFrame`/`endFrame` bracket every frame; resource destruction is deferred to the frame slot's next reuse; acquire, submit and present go through raw dispatcher calls so the per-frame path never throws; `rhi_tests` fails on any validation message.
- **Assets**: UUID in a `.meta` sidecar next to each source file; references are by UUID, never by path. Source assets are imported by the editor, cooked assets are what the player loads. A cook writes one `.sbundle` (a CBOR index over binary payloads) and `AssetDatabase::openBundle` reads it back under the same API, so the player is the editor's play mode without the editing (ADR-0011).
- **Render graph**: rebuilt every frame; passes declare attachments and sampled images, the graph emits the barriers, pools transient images and times every pass. Upper-module tests use `rhi::createNullDevice()` and assert on its command trace. CMake app targets are `sonnet_<name>_app` (binary `sonnet_<name>`); `sonnet_add_engine_shaders(<target>)` puts the engine shaders next to a binary.
- **Patched ports**: `ports/<name>` overlay ports registered by `vcpkg-configuration.json`, each described in `ports/README.md`.

## Conventions that are easy to get wrong

Full rules are in `docs/conventions.md`; these are the ones that differ from common habits.

- Commits follow Conventional Commits with the module name as scope: `feat(rhi): adopt vk-bootstrap handles into RAII wrappers`. Breaking changes carry `!` and a `BREAKING CHANGE:` footer. Before 1.0.0 each milestone bumps MINOR.
- Logging goes only through the `SONNET_LOG_*` macros, which attach file, line and function; calling spdlog directly is not allowed. `Error` values and `core::Exception` capture `std::source_location` at creation, so a reported error shows where it was created rather than where it was caught.
- Init and resource creation may throw; the per-frame path never throws; recoverable operations return `std::expected<T, Error>`.
- No compatibility shims, feature flags or future-proofing ahead of need. Third-party code is never edited in place; patches go through vcpkg overlay ports. vcpkg first, `FetchContent` only for libraries without a port, never both for one library.
- Two naming rules differ from common habit: `m_` applies only to members of classes with behaviour, plain data structs have no prefix; and implementations are named after their technology (`SdlWindow`, `VulkanDevice`) behind an `I`-prefixed interface.
- Every module has `tests/` built through `sonnet_add_module_test`; a bug fix comes with the test that would have caught it.
