# Quickstart: Editor UI Layout

**Branch**: `005-editor-ui-layout` | **Date**: 2026-05-02

## Prerequisites

This feature adds Dear ImGui as a build dependency. ImGui is fetched automatically by CMake via `FetchContent` — no manual installation is required.

All previously required prerequisites remain unchanged:

- CMake ≥ 4.2.1
- C++23-capable compiler (GCC 13+, Clang 17+, MSVC 19.36+)
- Vulkan SDK (for `find_package(Vulkan REQUIRED)`) — see `README.md` for installation
- SDL3 — fetched automatically via FetchContent
- spdlog — fetched automatically via FetchContent
- Catch2 — fetched automatically via FetchContent

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The first build will fetch and compile ImGui alongside existing dependencies. Expect a slightly longer initial build (~1–2 minutes extra).

## Run

```bash
./build/src/app/Sonnet
```

The editor window opens with:
- Central 3D viewport (renders the existing colored triangle)
- Bottom log/console panel showing engine startup messages
- Left placeholder panel (Scene Hierarchy)
- Right placeholder panel (Inspector)

## Layout Management

Layouts are saved to and loaded from a `layouts/` directory adjacent to the executable:

```
build/src/app/
├── Sonnet           ← executable
└── layouts/
    ├── active.txt   ← name of the last active layout
    └── *.ini        ← one file per saved named layout
```

To save a layout: **Layout → Save Layout…** from the menu bar.  
To load a layout: **Layout → Load Layout…** from the menu bar.  
To reset: **Layout → Reset to Default**.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

New tests added by this feature:
- `tests/unit/editor/LayoutManagerTest` — layout file I/O without display or GPU
- `tests/unit/editor/LogPanelTest` — log buffer filtering and ring cap without display
- `tests/unit/editor/EditorInterfaceTest` — mock IEditor lifecycle without display or GPU
- `tests/integration/EditorLifecycleTest` — mock-based editor init/shutdown lifecycle

All tests pass headlessly (no display or GPU required).
