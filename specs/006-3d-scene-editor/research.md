# Research: 3D Scene Editor

**Branch**: `006-3d-scene-editor` | **Date**: 2026-05-03

## Decision 1: Scene Graph Architecture

**Decision**: Implement `SonnetScene` as a pure math/data CMake module with no engine rendering dependencies. The module exposes `Transform` (hierarchy via parent pointers, local/world position/rotation/scale, cached model matrix), `GameObject` (name, enabled flag, Transform, optional `LightData`, optional `PrimitiveType`), and `Scene` (flat `vector<unique_ptr<GameObject>>` with `createObject`, `destroyObject`). GLM is the only dependency.

**Rationale**: A pure data module is fully unit-testable without GPU or display. The separation mirrors the existing engine architecture (interfaces in `renderer/`, implementations in `renderer_vulkan/`) and follows the pattern established by the v2 reference (`modules/world/`). Keeping lighting and primitive type metadata in the scene graph (rather than in a separate ECS) is appropriate for the scope of this feature (≤100 objects, single light).

**Alternatives considered**:
- **Full ECS (Entity-Component-System)**: Maximum flexibility, but introduces a registration/query framework far beyond this feature's needs. Deferred to a future architecture feature.
- **Scene graph with GPU handles embedded**: Directly storing mesh handles in `GameObject` couples the scene to the renderer. Rejected to maintain the clean separation between data and GPU resources — the `SceneContext` in `SonnetEditor` maintains the `GameObject → meshHandle` mapping instead.

---

## Decision 2: Renderer Interface Extension Strategy

**Decision**: Extend `IRendererBackend` with scene rendering methods rather than introducing a separate `ISceneRenderer` interface. New methods: `uploadMesh(CPUMesh&) → uint64_t`, `releaseMesh(uint64_t)`, `renderScene(SceneRenderDesc&)`, `pick(glm::ivec2) → int32_t`. New types `CPUMesh` and `SceneRenderDesc` are added to `SonnetRenderer` public headers (using GLM only). Default no-op implementations return sentinel values, preserving backward compatibility.

**Rationale**: A separate `ISceneRenderer` interface would require a second factory function in `main.cpp`, additional wiring through `IEditor`, and more files — all for the sake of future flexibility that has no current use case (there is only one renderer backend). The `IRendererBackend` already passes through the editor and is the natural extension point. Adding scene methods here requires minimal wiring changes. The approach can be refactored into a separate interface when a second backend or a standalone scene renderer is needed.

**Alternatives considered**:
- **Separate `ISceneRenderer` interface**: Cleanest separation, but introduces a second interface hierarchy, a second factory, and additional pass-through wiring for no current benefit. Deferred.
- **Fold scene state into `VulkanBackend` without interface changes**: Would make the backend un-mockable for panel tests. Rejected to preserve testability (Principle V).

---

## Decision 3: Gizmo Implementation

**Decision**: Implement gizmos as 2D overlays drawn via ImDrawList. For each active gizmo, the 3D handle positions (axis endpoints, rotation ring sample points, scale corners) are projected from world space to screen space using the current view-projection matrix and the ImGui viewport origin. Gizmo handles are drawn as coloured lines and filled circles. Hit-testing is performed by computing the distance from the mouse to each projected axis line segment (translate/scale) or arc (rotate). The active axis is encoded as a bitmask (1=X, 2=Y, 4=Z).

**Rationale**: No additional Vulkan pipeline or render pass is required, reducing implementation complexity significantly. The ImDrawList approach is portable (it runs on any ImGui-supported backend), produces sharp UI-quality visuals at any resolution, and is exactly what the v2 reference uses in `ViewportRenderer::doGizmo()`. Gizmos are editor-only overlays — they do not need to interact with the depth buffer or receive lighting.

**Alternatives considered**:
- **3D mesh gizmos rendered in the scene pass**: Correct depth ordering with scene objects, but requires additional shader pipeline, mesh uploads for each gizmo type, and per-frame transform updates. The added complexity is not justified for an editor overlay.
- **Dear ImGuizmo library**: Mature third-party library. Rejected because it would be a new FetchContent dependency (Principle III requires pinning), and the custom implementation provides exactly the feature set required without pulling in a larger library.

---

## Decision 4: GPU Object Picking

**Decision**: Implement picking via a dedicated ID-color render pass. Before processing a left-click in the viewport, render all scene objects to a separate RGBA8 offscreen render target (same resolution as the scene) where each object's fragment color encodes its 1-based ID as `(R=id>>16, G=(id>>8)&0xFF, B=id&0xFF, A=255)`. After the pass, copy the pixel under the cursor from the render target image to a host-visible staging buffer using `vkCmdCopyImageToBuffer`, then `vkQueueWaitIdle()` to synchronize, read the pixel, and decode the ID. The directional light billboard (2D ImDrawList icon) is checked for intersection before the GPU picking result is used.

**Rationale**: GPU picking correctly handles all primitive shapes at any level of zoom. Synchronous readback via `vkQueueWaitIdle` is acceptable in an editor context where picking occurs on user click (not every frame). The same picking render target and pipeline are reused for the outline mask pass (different fragment shader, same vertex shader and geometry).

**Alternatives considered**:
- **CPU ray-AABB / ray-mesh intersection**: Simple and GPU-free, but requires maintaining CPU-side bounding volumes and does not generalize easily to non-convex shapes. Deferred to a future optimization if GPU picking proves too slow.
- **Asynchronous readback with fence**: More correct but requires staging buffer lifetime management across frames. Not justified at this scale (≤100 objects, picking not in the hot path).

---

## Decision 5: Mesh Primitives — Cylinder and Capsule

**Decision**: Implement `makeCylinder(float radius, float height, int segments)` and `makeCapsule(float radius, float height, int segments)` from scratch. Cylinder: top disk (fan triangulation), bottom disk (inverted fan), side quads (triangle strip pairs). Each face group uses flat normals on caps and smooth radial normals on the side. Capsule: shared cylinder body for the straight section, plus a top hemisphere and bottom hemisphere generated by splitting a UV sphere at the equator and adjusting Y positions.

**Rationale**: The v2 reference (`modules/primitives/`) provides `makeBox` and `makeUVSphere` but not cylinder or capsule. The implementation follows the same vertex layout (location 0 = position, location 2 = texCoord, location 3 = normal) and `CPUMesh` struct. Cylinder and capsule are standard parametric primitives with well-known closed-form vertex generation.

**Alternatives considered**:
- **Use a third-party geometry library**: Would be a new FetchContent dependency. The primitives are simple enough to implement directly in ~150 lines.
- **Approximate capsule as tapered cylinder**: Visually inferior; rejected.
- **Skip cylinder/capsule for now**: The spec explicitly requires both (FR-001). Cannot defer.
