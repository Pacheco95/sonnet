# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Sonnet is a C++23 3D game engine (Vulkan 1.4 only, SDL3, flecs, Slang, Dear ImGui) with an editor and a generic player, targeting Windows, Linux, macOS, Android and iOS. It is the third iteration of the engine and was restarted docs-first: the repository currently contains the design documents and no code. The build scaffolding is milestone M0 in `docs/roadmap.md`.

The docs are the source of truth. Read the relevant one before a non-trivial change, and update it in the same change:

| Before touching | Read |
|---|---|
| Anything | `docs/architecture.md` (module map, dependency rule), `docs/conventions.md` |
| `rhi`, `renderer`, shaders | `docs/rendering.md` |
| `assets`, file formats | `docs/assets.md` |
| CMake, vcpkg, CI | `docs/build.md` |
| A cross-module decision | `docs/decisions/` (ADRs and the template) |

Accepted ADRs are settled. Do not relitigate them in code; a change of direction is a new ADR that supersedes the old one, and an accepted ADR is only ever edited to change its status. ADR-0008 (clustered forward rendering) is still Proposed and has to be decided before M3.

## Commands

The build does not exist yet. This is the intended workflow from `docs/build.md`; update this section in the M0 commit that creates the presets.

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-debug             # also windows-debug, macos-debug, linux-asan, linux-coverage
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
ctest --preset linux-debug -R core_tests            # one module's tests
# one case or tag: run the <module>_tests binary directly, e.g. core_tests "[handle]"
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic
python3 tools/check_docs.py            # after editing any Markdown: links, anchors, cross-doc consistency
```

Machine-specific notes (tool locations, checkouts of the previous iterations to adapt patterns from) live in `CLAUDE.local.md`, which is gitignored.

## Architecture in one page

- **Modules in dependency order**: `core → platform → rhi → renderer → assets → world → physics / scripting / audio → ui → editor`. A module links only modules earlier in the dependency order; `modules/CMakeLists.txt` adds them in this order and that order is the canonical statement of the architecture. `apps/editor` and `apps/player` are executables; `apps/samples/*` are project folders.
- **Interfaces live with their owner.** There is no `api` module. `platform` holds `IWindow` and the SDL3 implementation; `rhi` holds the device interfaces and the Vulkan implementation. Upward communication uses interfaces, callbacks and data handed down, never a dependency.
- **One `#if`-switched site**: the `rhi` device factory. The documented exception is `ui`, which may include Vulkan implementation headers for Dear ImGui's backend.
- **The engine does not own `main()`.** It implements SDL3's callback model so desktop and mobile share one lifecycle. Frame ordering lives in the app, not in modules.
- **ECS**: flecs. Hierarchy is `ChildOf`, prefabs and nested scenes are `IsA`, components are plain structs registered with flecs reflection, which drives the inspector, scene JSON and scripting bindings. flecs types appear unwrapped in `world` headers by decision.
- **Handles**: `core::Handle<Tag>` is a 32-bit index plus 32-bit generation; components store handles, owners resolve them.
- **Editor versus player**: the player never links `editor`, nor `ui` in release. Play mode snapshots the world on play and restores it on stop.
- **Rendering**: vk-bootstrap creates, Vulkan-HPP RAII wrappers own, `vkb::destroy_*` is never called, RAII members are declared in reverse destruction order. The render graph is an engine module. Shaders are Slang only, compiled by `slangc` at build time and at runtime in the editor for hot reload; Slang reflection is the only shader reflection source. Set 0 is the bindless set, set 1 is push descriptors, push constants carry per-draw indices. Reversed-Z, negative viewport height for the Y flip, counter-clockwise front faces.
- **Assets**: UUID in a `.meta` sidecar next to each source file; references are by UUID, never by path. Source assets are imported by the editor, cooked assets are what the player loads.

## Conventions that are easy to get wrong

Full rules are in `docs/conventions.md`; these are the ones that differ from common habits.

- Commits follow Conventional Commits with the module name as scope: `feat(rhi): adopt vk-bootstrap handles into RAII wrappers`. Breaking changes carry `!` and a `BREAKING CHANGE:` footer. Before 1.0.0 each milestone bumps MINOR.
- Logging goes only through the `SONNET_LOG_*` macros, which attach file, line and function; calling spdlog directly is not allowed. `Error` values and `core::Exception` capture `std::source_location` at creation, so a reported error shows where it was created rather than where it was caught.
- Init and resource creation may throw; the per-frame path never throws; recoverable operations return `std::expected<T, Error>`.
- No compatibility shims, feature flags or future-proofing ahead of need. Third-party code is never edited in place; patches go through vcpkg overlay ports. vcpkg first, `FetchContent` only for libraries without a port, never both for one library.
- Two naming rules differ from common habit: `m_` applies only to members of classes with behaviour, plain data structs have no prefix; and implementations are named after their technology (`SdlWindow`, `VulkanDevice`) behind an `I`-prefixed interface.
- Every module has `tests/` built through `sonnet_add_module_test`; a bug fix comes with the test that would have caught it.
