# Developer Quickstart: 3D Scene Editor

**Branch**: `006-3d-scene-editor` | **Date**: 2026-05-03

## Prerequisites

No new prerequisites beyond what is already documented in `README.md`. The Vulkan SDK is required (for `glslc` shader compilation). All other dependencies are fetched via CMake FetchContent.

## Build

```bash
# Configure (OpenGL backend not supported for this feature — Vulkan only)
cmake -S . -B build -DSONNET_RENDERER_BACKEND=Vulkan

# Build (shaders compiled automatically via glslc)
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

New test executables:
- `SonnetSceneTests` — Transform hierarchy, world-position preservation, scene operations
- `SonnetPrimitivesTests` — mesh vertex/index validity per primitive type
- Existing editor tests still pass (mock renderer used throughout)

## Using the Scene Editor

### Adding objects

Open the **Add** menu in the menu bar. Select a primitive type (Cube, Sphere, Cylinder, Plane, Capsule) or **Directional Light**. The object appears at the world origin (0, 0, 0) and is added to the **Scene Hierarchy** panel. If a directional light already exists, the "Directional Light" menu item is grayed out; hovering it shows a tooltip.

### Navigating the scene (Fly Camera)

**Hold the right mouse button** while the viewport is focused to enter fly-camera mode. The cursor is hidden.

| Input | Action |
|-------|--------|
| Mouse move | Look (yaw/pitch) |
| W / S | Move forward / backward |
| A / D | Strafe left / right |
| Q / E | Move down / up |

Release the right mouse button to exit fly mode.

### Selecting objects

- **Viewport**: Left-click on an object (or its directional-light billboard icon). A colored outline appears and gizmos activate.
- **Scene Hierarchy**: Left-click the object's row.
- **Deselect**: Left-click empty space in the viewport or hierarchy.

### Gizmo controls

| Key | Mode |
|-----|------|
| W | Translate (move) |
| E | Rotate |
| R | Scale |

Drag an axis handle to transform the selected object. All gizmos operate in **world space**. Center handle on scale gizmo = uniform scale.

### Inspector panel

When an object is selected the **Inspector** panel shows:

- **Position** (X, Y, Z) — world position
- **Rotation** (X, Y, Z) — Euler angles in degrees
- **Scale** (X, Y, Z) — local scale

Edit a field and press **Enter** to apply. Press **Tab** to apply and advance to the next field.

For the directional light, the inspector additionally shows:
- **Color** — color picker
- **Intensity** — numeric field

### Scene Hierarchy: parenting and unparenting

- **Parent**: Drag object B onto object A in the hierarchy panel. B becomes a child of A. B's world-space transform is preserved.
- **Unparent** (option 1): Drag the child object to the root level (above all other root items) in the hierarchy.
- **Unparent** (option 2): Right-click the child object in the hierarchy → **Unparent**.

Child objects move with their parent when the parent is transformed.

## Adding New Primitive Types (developer guide)

1. Add a new enumerator to `PrimitiveType` in `src/scene/include/sonnet/scene/GameObject.hpp`.
2. Add a generator function to `src/primitives/include/sonnet/primitives/MeshPrimitives.hpp` and implement it in `MeshPrimitives.cpp`.
3. Add a case to `SceneContext::addPrimitive()` in `src/editor/src/SceneContext.cpp`.
4. Add the menu item in `Editor::drawAddMenu()` in `src/editor/src/Editor.cpp`.
5. Add unit tests in `tests/unit/primitives/MeshPrimitivesTests.cpp`.

## Shader Development

New shaders live in `src/renderer_vulkan/src/shaders/`. Each `.vert`/`.frag` file must be registered in `src/renderer_vulkan/CMakeLists.txt` via `compile_shader(SonnetRendererVulkan <path>)`. The compiled `.spv` file is copied to `<exe-dir>/shaders/` at build time and loaded by `VulkanBackend` at runtime.

GLSL target version: `#version 460 core`. Vulkan descriptor sets use `layout(set=N, binding=M)`. Model matrix is always a push constant (`layout(push_constant)`).
