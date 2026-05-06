# Implementation Plan: 3D Scene Editor

**Branch**: `006-3d-scene-editor` | **Date**: 2026-05-03 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `specs/006-3d-scene-editor/spec.md`

## Summary

Introduce the first 3D scene editing capability in Sonnet. A new `SonnetScene` module provides a pure scene graph (Transform hierarchy, GameObject, LightComponent). A new `SonnetPrimitives` module generates CPU-side mesh data for five primitive shapes. The existing `IRendererBackend` interface is extended with scene rendering operations (mesh upload, forward-lit scene pass, GPU picking). `VulkanBackend` implements these using four new GLSL shader pipelines compiled at build time. The three stub editor panels (Viewport, Scene Hierarchy, Inspector) are fully implemented with fly camera, world-space gizmos, drag-drop parenting, and numeric transform editing. An "Add" menu in the menu bar is the entry point for adding primitives and the single directional light.

This is the project's first forward rendering pipeline. The reference implementation is at `/home/michael/repositories/v2/sonnet`; patterns for the fly camera (`apps/editor/ViewportRenderer.h`), shader code (`apps/editor/ViewportRenderer.cpp`), and scene graph (`modules/world/`) are adapted — not copied — for this project's SDL3+Vulkan stack.

## Technical Context

**Language/Version**: C++23 (`-std=c++23`, `CMAKE_CXX_STANDARD 23`)  
**Primary Dependencies**: SDL3 3.4.4, Vulkan SDK, VulkanHpp v1.4.341, Dear ImGui docking branch, GLM (shared utility), spdlog v1.17.0, Catch2 v3.14.0 — all pre-existing, no new FetchContent entries  
**Storage**: N/A (no scene persistence in this feature)  
**Testing**: Catch2 v3.14.0; `SonnetSceneTests` and `SonnetPrimitivesTests` are pure unit tests (no GPU); editor panel tests use mock `IRendererBackend`  
**Target Platform**: Linux desktop (primary); SDL3+Vulkan portability unchanged  
**Project Type**: Desktop application (game engine editor)  
**Performance Goals**: ≥60 FPS stable for ≥60 seconds with a scene of ≤100 objects and one directional light; all Quality Standards baselines apply  
**Constraints**: `ctest --output-on-failure` must pass headlessly; no new prerequisites beyond existing Vulkan SDK requirement; all new code under `-Wall -Wextra -Wpedantic -Werror`  
**Scale/Scope**: Single user, single scene, ≤100 primitives, exactly 0–1 directional lights, single selection

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Modularity | ✅ Pass | `SonnetScene` and `SonnetPrimitives` are independent CMake static libs with one-way deps. `SonnetScene` depends only on GLM. GLM is a shared utility (`PUBLIC`/`INTERFACE`). No Vulkan types appear in any public header — new `CPUMesh` and `SceneRenderDesc` types in `SonnetRenderer` public headers use only GLM. `VulkanBackend` extension keeps all Vulkan types in PRIVATE implementation files. `SonnetEditor` depends on `SonnetRenderer` interface only, never on `SonnetRendererVulkan`. |
| II. Usability | ✅ Pass | Shader pipeline creation failure → `SONNET_LOG_ERROR` + `false` propagated to init caller. Mesh upload failure returns sentinel handle `0`. No silent failures introduced. |
| III. Stability | ✅ Pass | Zero new FetchContent dependencies. All existing deps remain pinned to their GIT_TAGs. New `CMakeLists.txt` files for `SonnetScene` and `SonnetPrimitives` declare `cmake_minimum_required`. |
| IV. Predictability | ✅ Pass | `sonnet_set_compile_options()` applied to `SonnetScene`, `SonnetPrimitives`. New shaders use the existing `compile_shader()` CMake function. No raw-GL code introduced. SDL3 callback lifecycle unchanged. |
| V. Testability | ✅ Pass | `SonnetScene`: pure unit tests (Transform hierarchy, world-position preservation, scene ops). `SonnetPrimitives`: geometric unit tests (vertex count, index validity, normal sanity). Editor panels receive `IRendererBackend&` → mocked in tests (no GPU required). `SceneHierarchyPanel` and `InspectorPanel` testable with a real `Scene` object (no GPU). `ViewportPanel` tested with mock renderer. |
| VI. Portability | ✅ Pass | SDL3+Vulkan stack maintained throughout. No GLFW, no raw platform calls, no OpenGL. Gizmos rendered via ImDrawList (platform-neutral). |
| VII. Internationalization | ⚠️ Exception | Multiple hardcoded editor strings. See Complexity Tracking. |

## Project Structure

### Documentation (this feature)

```text
specs/006-3d-scene-editor/
├── plan.md              ← this file
├── research.md          ← Phase 0 output (5 decisions)
├── data-model.md        ← Phase 1 output
├── quickstart.md        ← Phase 1 output
└── tasks.md             ← Phase 2 output (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
src/
├── scene/                              [NEW — SonnetScene static lib]
│   ├── CMakeLists.txt
│   ├── include/sonnet/scene/
│   │   ├── Transform.hpp              scene node transform with hierarchy
│   │   ├── GameObject.hpp             named scene node with component data
│   │   └── Scene.hpp                  flat object store + buildRenderQueue helper
│   └── src/
│       ├── Transform.cpp
│       └── Scene.cpp
│
├── primitives/                         [NEW — SonnetPrimitives static lib]
│   ├── CMakeLists.txt
│   ├── include/sonnet/primitives/
│   │   └── MeshPrimitives.hpp         makeBox/makeSphere/makeCylinder/makePlane/makeCapsule
│   └── src/
│       └── MeshPrimitives.cpp
│
├── renderer/
│   ├── CMakeLists.txt                  [UNCHANGED]
│   └── include/sonnet/renderer/
│       ├── IRendererBackend.hpp        [MODIFIED] add uploadMesh/releaseMesh/renderScene/pick
│       ├── CPUMesh.hpp                 [NEW] Vertex struct + CPUMesh (positions/normals/UVs/indices)
│       └── SceneRenderTypes.hpp        [NEW] DrawItem, SceneRenderDesc, DirectionalLightDesc
│
├── renderer_vulkan/
│   ├── CMakeLists.txt                  [MODIFIED] add compile_shader calls for 6 new shaders
│   └── src/
│       ├── VulkanBackend.hpp           [MODIFIED] add scene pipeline/buffer/RT fields
│       ├── VulkanBackend.cpp           [MODIFIED] implement uploadMesh, renderScene, pick
│       └── shaders/
│           ├── forward_lit.vert        [NEW] model/view/proj + normal transform
│           ├── forward_lit.frag        [NEW] Blinn-Phong directional light
│           ├── picking.vert            [NEW] same as forward_lit.vert
│           ├── picking.frag            [NEW] outputs object ID as packed RGB color
│           ├── outline_mask.frag       [NEW] solid white mask (reuses picking.vert)
│           ├── outline_comp.vert       [NEW] fullscreen quad in NDC
│           └── outline_comp.frag       [NEW] 5×5 dilation edge detect, blends outline color
│
└── editor/
    ├── CMakeLists.txt                  [MODIFIED] link SonnetScene + SonnetPrimitives
    ├── include/sonnet/editor/
    │   ├── IEditor.hpp                 [MODIFIED] render(float dt) replaces render()
    │   └── IPanel.hpp                  [UNCHANGED]
    └── src/
        ├── SceneContext.hpp            [NEW] owns Scene; bridges scene to IRendererBackend
        ├── SceneContext.cpp            [NEW]
        ├── FlyCamera.hpp               [NEW] right-mouse + WASD + Q/E controller
        ├── Editor.hpp                  [MODIFIED] owns SceneContext; adds "Add" menu
        ├── Editor.cpp                  [MODIFIED]
        └── panels/
            ├── ViewportPanel.hpp       [MODIFIED] fly camera, picking, gizmo overlay
            ├── ViewportPanel.cpp       [MODIFIED]
            ├── SceneHierarchyPanel.hpp [MODIFIED] full hierarchy + drag-drop + context menu
            ├── SceneHierarchyPanel.cpp [MODIFIED]
            ├── InspectorPanel.hpp      [MODIFIED] transform fields + light config
            └── InspectorPanel.cpp      [MODIFIED]

tests/
├── unit/
│   ├── CMakeLists.txt                  [MODIFIED] add scene + primitives test executables
│   ├── scene/
│   │   ├── TransformTests.cpp          [NEW] hierarchy, world-pos preservation, getModelMatrix
│   │   └── SceneTests.cpp              [NEW] createObject, destroyObject, circular-parent block
│   ├── primitives/
│   │   └── MeshPrimitivesTests.cpp     [NEW] vertex counts, index validity, normal directions
│   └── editor/
│       ├── SceneHierarchyPanelTests.cpp [NEW] selection, parenting logic (mock renderer)
│       └── InspectorPanelTests.cpp      [NEW] transform apply, light property reflect
└── integration/
    ├── CMakeLists.txt                  [UNCHANGED — existing lifecycle test covers init path]
    └── AppLifecycleTest.cpp            [UNCHANGED]

CMakeLists.txt (root)                   [MODIFIED] add_subdirectory(src/scene) + src/primitives
```

**Structure Decision**: Single-project layout extending the existing module tree. Two new sibling modules (`scene/`, `primitives/`) added at the same level as `renderer/`, `editor/`. Dependency order: `scene → primitives → renderer_vulkan` and `scene → editor`.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| VII: Hardcoded UI strings (`"Add"`, `"Cube"`, `"Sphere"`, `"Cylinder"`, `"Plane"`, `"Capsule"`, `"Directional Light"`, `"Scene Hierarchy"`, `"Inspector"`, `"Viewport"`, `"Position"`, `"Rotation"`, `"Scale"`, `"X"`, `"Y"`, `"Z"`, `"Color"`, `"Intensity"`, `"Unparent"`, tooltip text, auto-generated object names) | All strings are developer-facing labels in an engine editor tool; no localization use case exists at this stage | A locale subsystem would be disproportionate overhead for ~20 developer-facing strings in a single-language editor tool; tracked here per constitution; revisit when a public-facing localizable surface exists |
