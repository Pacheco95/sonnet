# Implementation Plan: Editor UI Layout

**Branch**: `005-editor-ui-layout` | **Date**: 2026-05-02 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `specs/005-editor-ui-layout/spec.md`

## Summary

Introduce a professional, game-engine-style dockable editor UI over the existing SDL3+Vulkan window. A new `SonnetEditor` CMake static library wraps Dear ImGui (docking branch) with `PRIVATE` linkage, exposing only abstract `IEditor`, `IPanel`, and `ILayoutManager` interfaces. The existing colored-triangle renderer is moved to an offscreen Vulkan framebuffer and displayed as an `ImGui::Image` inside a permanent Viewport panel. Three additional panels are added: Log (live engine output), Scene Hierarchy (placeholder), and Inspector (placeholder). Panels are fully rearrangeable, tab-groupable, and floatable via ImGui's built-in dockspace. Named layouts are persisted as individual `.ini` files and managed via a **Layout** top-menu entry.

## Technical Context

**Language/Version**: C++23 (`-std=c++23`, `CMAKE_CXX_STANDARD 23`)  
**Primary Dependencies**: SDL3 3.4.4 (FetchContent), Vulkan SDK (find_package), VulkanHpp v1.4.341, spdlog v1.17.0, Catch2 v3.14.0, **Dear ImGui docking branch** (FetchContent — see research.md Decision 1 for pinned tag)  
**Storage**: Local filesystem — `layouts/*.ini` + `layouts/active.txt` adjacent to the engine executable  
**Testing**: Catch2 v3.14.0 via `catch_discover_tests()`; all tests pass headlessly without display or GPU  
**Target Platform**: Linux desktop (primary); macOS and Windows (stretch targets, validated by ImGui + SDL3 + Vulkan portability)  
**Project Type**: Desktop application (game engine editor)  
**Performance Goals**: ≥60 FPS stable for ≥60 seconds; ≤100 MB RAM; ≤100 MB VRAM; ≤5% steady-state CPU; clean exit within 1 second  
**Constraints**: No new user-installed prerequisites beyond those already required; all new code passes `-Wall -Wextra -Wpedantic -Werror`  
**Scale/Scope**: Single-user desktop; up to ~10,000 log entries in ring buffer; unlimited named layouts (file-per-layout)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|---|---|---|
| I. Modularity | ✅ Pass | `SonnetEditor` is a new CMake static library. ImGui is `PRIVATE`. No ImGui/Vulkan types in public headers — `RendererNativeHandles` uses `uint64_t` opaque fields. Dependency graph is acyclic (see research.md Decision 5). |
| II. Usability | ✅ Pass | Editor init failure emits `SONNET_LOG_ERROR` + returns `false` (SDL_AppInit returns `SDL_APP_FAILURE`). Every log line carries file/line via `SONNET_LOG_*` macros. `quickstart.md` updated. `README.md` will be updated by implementation task. |
| III. Stability | ✅ Pass | ImGui pinned to a specific docking-branch release tag (no `docking` branch name). `cmake_minimum_required` declared in `src/editor/CMakeLists.txt`. |
| IV. Predictability | ✅ Pass | `sonnet_set_compile_options()` applied to `SonnetEditor` target. ImGui FetchContent target excluded. ImGui headers added with `SYSTEM` keyword. SDL3 callback lifecycle (`SDL_MAIN_USE_CALLBACKS`) unchanged. |
| V. Testability | ✅ Pass | `IEditor`, `IPanel`, `ILayoutManager` abstract interfaces with `I`-prefix. Mock implementations for all unit tests. `LayoutManager` and `LogPanel` tested without display/GPU. `ImGuiEditor` (concrete) tested only via integration test with mock renderer/window. |
| VI. Portability | ✅ Pass | ImGui provides SDL3 and Vulkan backends for Linux/macOS/Windows. No platform-specific code above `SonnetWindow`. `quickstart.md` updated. |
| VII. Internationalization | ⚠️ Exception | Multiple hardcoded user-visible strings (see Complexity Tracking). |
| Quality Standards | ✅ Pass | Performance baselines apply; SC-005 (≥100 msg/s responsiveness) and SC-007 (viewport not broken by panel moves) are new acceptance criteria aligned with constitution baselines. |

## Project Structure

### Documentation (this feature)

```text
specs/005-editor-ui-layout/
├── plan.md              ← this file
├── research.md          ← Phase 0 output (5 decisions)
├── data-model.md        ← Phase 1 output
├── quickstart.md        ← Phase 1 output
├── contracts/
│   └── editor-interfaces.md   ← Phase 1 output
└── tasks.md             ← Phase 2 output (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
cmake/
└── FetchDependencies.cmake    [MODIFIED] add ImGui FetchContent

src/
├── logging/                   [UNCHANGED]
├── window/                    [UNCHANGED]
├── renderer/
│   ├── CMakeLists.txt         [UNCHANGED]
│   └── include/sonnet/renderer/
│       └── IRendererBackend.hpp  [MODIFIED] add RendererNativeHandles struct + getNativeHandles()
├── renderer_vulkan/
│   ├── CMakeLists.txt         [UNCHANGED]
│   └── src/
│       ├── VulkanBackend.hpp  [MODIFIED] add offscreen framebuffer, getNativeHandles(), viewport texture
│       └── VulkanBackend.cpp  [MODIFIED] implement offscreen rendering + ImGui Vulkan descriptor
├── editor/                    [NEW — SonnetEditor library]
│   ├── CMakeLists.txt
│   ├── include/sonnet/editor/
│   │   ├── IEditor.hpp
│   │   ├── IPanel.hpp
│   │   ├── ILayoutManager.hpp
│   │   └── EditorFactory.hpp
│   └── src/
│       ├── Editor.hpp
│       ├── Editor.cpp
│       ├── LayoutManager.hpp
│       ├── LayoutManager.cpp
│       ├── log/
│       │   ├── EditorLogSink.hpp
│       │   └── EditorLogSink.cpp
│       └── panels/
│           ├── ViewportPanel.hpp
│           ├── ViewportPanel.cpp
│           ├── LogPanel.hpp
│           ├── LogPanel.cpp
│           ├── SceneHierarchyPanel.hpp
│           ├── SceneHierarchyPanel.cpp
│           ├── InspectorPanel.hpp
│           └── InspectorPanel.cpp
└── app/
    ├── CMakeLists.txt         [MODIFIED] link SonnetEditor
    └── main.cpp               [MODIFIED] integrate editor into SDL3 callbacks

tests/
├── integration/
│   ├── CMakeLists.txt         [MODIFIED]
│   └── EditorLifecycleTest.cpp  [NEW]
└── unit/
    ├── CMakeLists.txt         [MODIFIED]
    └── editor/
        ├── CMakeLists.txt     [NEW]
        ├── LayoutManagerTest.cpp  [NEW]
        ├── LogPanelTest.cpp   [NEW]
        └── EditorInterfaceTest.cpp  [NEW]

CMakeLists.txt  [MODIFIED] add add_subdirectory(src/editor)
```

**Structure Decision**: Single-project layout (Option 1 from template) extended with `src/editor/`. Follows the established pattern of one CMake static library per engine domain, each with its own `include/sonnet/<domain>/` public header tree.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| **VII. I18n** — Hardcoded string: `"Viewport"` (panel title) | Panel titles are displayed in the ImGui title bar; they are user-visible. At this stage the engine has no locale subsystem. | A locale/i18n system is disproportionate for an engine with fewer than 10 user-visible strings. This exception is a tracked debt: a localization feature MUST be planned once the editor has a meaningful string surface. Forward ref: create a `SonnetLocale` feature spec when panel/menu count exceeds 20 strings. |
| **VII. I18n** — Hardcoded string: `"Log"` (panel title) | Same rationale as above. | Same as above. |
| **VII. I18n** — Hardcoded string: `"Scene Hierarchy"` (panel title) | Same rationale as above. | Same as above. |
| **VII. I18n** — Hardcoded string: `"Inspector"` (panel title) | Same rationale as above. | Same as above. |
| **VII. I18n** — Hardcoded strings: Layout menu items (`"Layout"`, `"Save Layout…"`, `"Load Layout…"`, `"Reset to Default"`) | Menu bar strings are user-visible. Same early-stage rationale. | Same as above — all menu strings fall under the same deferred localization plan. |
