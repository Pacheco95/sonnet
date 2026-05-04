# Tasks: 3D Scene Editor

**Input**: Design documents from `/specs/006-3d-scene-editor/`
**Prerequisites**: plan.md ✅, spec.md ✅, research.md ✅, data-model.md ✅, quickstart.md ✅

**Tests**: Unit tests included per plan.md — SonnetSceneTests, SonnetPrimitivesTests, and editor panel tests with mock IRendererBackend.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Exact file paths are included in descriptions

---

## Phase 1: Setup (Build System Wiring)

**Purpose**: Wire the two new CMake modules and test executables into the build before any source is written.

- [ ] T001 Modify CMakeLists.txt (root) to add add_subdirectory(src/scene) and add_subdirectory(src/primitives) before the editor subdirectory
- [ ] T002 [P] Create src/scene/CMakeLists.txt defining SonnetScene as a static library with GLM as a PUBLIC dependency and sonnet_set_compile_options() applied
- [ ] T003 [P] Create src/primitives/CMakeLists.txt defining SonnetPrimitives as a static library linking SonnetRenderer (for CPUMesh types) and SonnetScene, with sonnet_set_compile_options() applied
- [ ] T004 Modify src/editor/CMakeLists.txt to link SonnetScene and SonnetPrimitives
- [ ] T005 Modify tests/unit/CMakeLists.txt to add SonnetSceneTests (TransformTests.cpp, SceneTests.cpp) and SonnetPrimitivesTests (MeshPrimitivesTests.cpp) Catch2 test executables
- [ ] T006 Modify src/renderer_vulkan/CMakeLists.txt to register 7 new shaders via compile_shader(): forward_lit.vert, forward_lit.frag, picking.vert, picking.frag, outline_mask.frag, outline_comp.vert, outline_comp.frag

**Checkpoint**: `cmake -S . -B build` configures without errors; all new targets appear in the build graph.

---

## Phase 2: Foundational (SonnetScene + SonnetPrimitives + Renderer Types)

**Purpose**: Pure data/math modules and renderer interface extensions that ALL user stories depend on. No GPU required — fully unit-testable.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

### Renderer Public Types

- [ ] T007 [P] Create src/renderer/include/sonnet/renderer/CPUMesh.hpp defining Vertex (position: glm::vec3 location 0, texCoord: glm::vec2 location 2, normal: glm::vec3 location 3) and CPUMesh (vector<Vertex> vertices, vector<uint32_t> indices)
- [ ] T008 [P] Create src/renderer/include/sonnet/renderer/SceneRenderTypes.hpp defining DrawItem (meshHandle, modelMatrix, objectId, isSelected), DirectionalLightDesc (direction, color, intensity), and SceneRenderDesc (viewMatrix, projMatrix, cameraPosition, viewportSize, optional<DirectionalLightDesc>, vector<DrawItem>, outlineColor) per data-model.md
- [ ] T009 Modify src/renderer/include/sonnet/renderer/IRendererBackend.hpp to add uploadMesh(CPUMesh&) → uint64_t, releaseMesh(uint64_t), renderScene(SceneRenderDesc&), and pick(glm::ivec2) → int32_t with default no-op implementations returning 0

### SonnetScene Module

- [ ] T010 [P] Create src/scene/include/sonnet/scene/Transform.hpp declaring Transform with localPosition/localRotation/localScale, non-owning parent pointer, non-owning children vector, mutable modelMatrix cache, and all key operations: setParent(Transform*, bool keepWorldTransform), setWorldPosition/setWorldRotation, getWorldPosition/getWorldRotation, getModelMatrix, forward()/up()/right()
- [ ] T011 [P] Create src/scene/include/sonnet/scene/GameObject.hpp declaring PrimitiveType enum class (Cube, Sphere, Cylinder, Plane, Capsule), LightData struct (color: glm::vec3 white, intensity: float 1.0, direction: glm::vec3 (0,-1,0)), and GameObject (name: string, enabled: bool, transform: Transform, light: optional<LightData>, primitiveType: optional<PrimitiveType>)
- [ ] T012 Create src/scene/include/sonnet/scene/Scene.hpp and src/scene/src/Scene.cpp declaring/implementing Scene with vector<unique_ptr<GameObject>> storage, createObject(string name, GameObject* parent) → GameObject&, destroyObject(GameObject*), and objects() const accessor
- [ ] T013 Implement src/scene/src/Transform.cpp: setParent() with circular-ancestor guard (walk parent chain; silently return if cycle detected), keepWorldTransform re-computation of local offset, setWorldPosition/Rotation computing required local transform, getModelMatrix() lazy recompute on dirty flag

### SonnetPrimitives Module

- [ ] T014 [P] Create src/primitives/include/sonnet/primitives/MeshPrimitives.hpp declaring makeBox(glm::vec3 size) → CPUMesh, makeUVSphere(int segX, int segY) → CPUMesh, makeCylinder(float radius, float height, int segments) → CPUMesh, makePlane(glm::vec2 size) → CPUMesh, makeCapsule(float radius, float height, int segments) → CPUMesh
- [ ] T015 Implement src/primitives/src/MeshPrimitives.cpp: makeBox (24 verts, 4 per face, flat normals), makeUVSphere (smooth normals, (segX+1)×(segY+1) verts), makeCylinder (flat cap normals, smooth radial side normals, top/bottom fan + side quads), makePlane (4 verts, (0,1,0) normal, facing +Y), makeCapsule (hemisphere top + hemisphere bottom split from UV sphere + cylinder body joining them)

### Unit Tests (write before Phase 3; verify they fail with empty implementations)

- [ ] T016 [P] Create tests/unit/scene/TransformTests.cpp: test setParent(keepWorldTransform=true) preserves world position after reparenting, test circular-parent attempt is silently rejected and hierarchy unchanged, test getModelMatrix returns identity for default transform and correct matrix for a translated+rotated transform
- [ ] T017 [P] Create tests/unit/scene/SceneTests.cpp: test createObject adds a new GameObject to objects(), test destroyObject removes it, test that setting a circular parent via Transform::setParent is ignored
- [ ] T018 [P] Create tests/unit/primitives/MeshPrimitivesTests.cpp: test makeBox produces 24 vertices and all indices reference valid vertices, test makeUVSphere normals are unit-length, test makeCylinder top+bottom vertex counts match segment parameter, test makePlane produces 4 vertices with normals equal to (0,1,0), test makeCapsule indices are all within bounds

**Checkpoint**: `ctest --test-dir build --output-on-failure` passes SonnetSceneTests and SonnetPrimitivesTests.

---

## Phase 3: User Story 1 — Primitive Instantiation and Lighting (Priority: P1) 🎯 MVP

**Goal**: Add 3D primitive objects to the scene via the "Add" menu; a directional light illuminates them with Blinn-Phong shading. At most one directional light is allowed at a time.

**Independent Test**: Open editor → Add menu → Cube → cube appears rendered with directional lighting in the viewport.

### Implementation for User Story 1

- [ ] T019 [US1] Modify src/editor/include/sonnet/editor/IEditor.hpp to change render() to render(float dt) so panels receive delta-time for frame-rate-independent updates
- [ ] T020 [P] [US1] Create src/renderer_vulkan/src/shaders/forward_lit.vert: MVP transform (view/proj from CameraUBO set 0 binding 0, model from push constant), pass world-space position and corrected normal (mat3(transpose(inverse(model))) * normal) to fragment stage
- [ ] T021 [P] [US1] Create src/renderer_vulkan/src/shaders/forward_lit.frag: Blinn-Phong directional light using LightsUBO (set 0 binding 1) for dirDirection/dirColor/dirIntensity; add ambient term; output final lit color
- [ ] T022 [US1] Modify src/renderer_vulkan/src/VulkanBackend.hpp to add forward-lit VkPipeline, CameraUBO and LightsUBO VkBuffer/VkDeviceMemory, map<uint64_t, pair<VkBuffer,VkDeviceMemory>> for uploaded mesh vertex+index buffers, and uint64_t meshHandleCounter starting at 1
- [ ] T023 [US1] Implement uploadMesh() in src/renderer_vulkan/src/VulkanBackend.cpp: allocate host-visible VkBuffer for vertices and indices, copy data, store under auto-incremented handle; implement releaseMesh() to free and erase the entry
- [ ] T024 [US1] Implement renderScene() in src/renderer_vulkan/src/VulkanBackend.cpp: update CameraUBO (view, proj, cameraPos) and LightsUBO (dirLight or zeroed), bind forward-lit pipeline, for each DrawItem with meshHandle != 0 bind vertex+index buffer, push model matrix, call vkCmdDrawIndexed
- [ ] T025 [US1] Create src/editor/src/SceneContext.hpp defining SceneContext owning a Scene, referencing IRendererBackend&, holding map<PrimitiveType, uint64_t> meshHandles, GameObject* selectedObject, GameObject* directionalLightObject, and per-type name counters; declare all key operations
- [ ] T026 [US1] Implement src/editor/src/SceneContext.cpp: addPrimitive() generates name (type name + counter, first instance no suffix), lazily uploads mesh via MeshPrimitives + backend.uploadMesh(), creates and returns the new GameObject; addDirectionalLight() returns nullptr if directionalLightObject != nullptr; selectObject()/deselectAll(); setParent() delegates to Transform::setParent(keepWorldTransform=true); buildRenderDesc() builds SceneRenderDesc from scene objects
- [ ] T027 [US1] Modify src/editor/src/Editor.hpp to own SceneContext (constructed with the IRendererBackend reference) and declare drawAddMenu(SceneContext&)
- [ ] T028 [US1] Modify src/editor/src/Editor.cpp to instantiate SceneContext in the initializer list, implement drawAddMenu() with BeginMenu("Add") containing items for Cube/Sphere/Cylinder/Plane/Capsule calling addPrimitive(), and a "Directional Light" item that is BeginDisabled when directionalLightObject != nullptr with an explanatory tooltip on hover; update render(dt) to forward dt to panels
- [ ] T029 [US1] Modify src/editor/src/panels/ViewportPanel.hpp to store a SceneContext& reference (set via constructor or setter) and accept dt in draw(float dt)
- [ ] T030 [US1] Modify src/editor/src/panels/ViewportPanel.cpp to compute a default perspective proj matrix (60° FoV, viewport aspect ratio) and identity view matrix, call sceneContext.buildRenderDesc(view, proj, cameraPos, viewportSize) → renderDesc, then backend.renderScene(renderDesc) each frame

**Checkpoint**: Primitives and directional light can be added via the Add menu and are rendered with Blinn-Phong shading. At this point US1 is fully functional and independently testable.

---

## Phase 4: User Story 2 — Object Selection and Gizmo Manipulation (Priority: P2)

**Goal**: Click an object in the viewport to select it; a colored outline highlights it; 3D translate/rotate/scale gizmos appear; W/E/R switches gizmo mode; drag moves/rotates/scales the object; click empty space deselects.

**Independent Test**: Add a cube → click it in viewport → outline appears + translate gizmo shown → drag X-axis handle → cube moves along X.

### Implementation for User Story 2

- [ ] T031 [P] [US2] Create src/renderer_vulkan/src/shaders/picking.vert: identical vertex transform to forward_lit.vert (model push constant, CameraUBO set 0 binding 0)
- [ ] T032 [P] [US2] Create src/renderer_vulkan/src/shaders/picking.frag: encode objectId (push constant uint) as packed RGB output (R=id>>16, G=(id>>8)&0xFF, B=id&0xFF, A=255)
- [ ] T033 [P] [US2] Create src/renderer_vulkan/src/shaders/outline_mask.frag: output solid white (1,1,1,1) — reuses picking.vert for geometry stage
- [ ] T034 [P] [US2] Create src/renderer_vulkan/src/shaders/outline_comp.vert: emit fullscreen quad from 6 hardcoded NDC positions (no vertex buffer), pass UV to fragment stage
- [ ] T035 [P] [US2] Create src/renderer_vulkan/src/shaders/outline_comp.frag: sample mask texture (set 0 binding 0), perform 5×5 dilation to detect edges, blend outlineColor (push constant vec3) where edge detected, pass-through otherwise
- [ ] T036 [US2] Modify src/renderer_vulkan/src/VulkanBackend.hpp to add RGBA8 picking offscreen VkImage/VkImageView/VkFramebuffer, host-visible staging VkBuffer for readback, VkPipeline for picking, outline mask, and outline composite passes
- [ ] T037 [US2] Implement pick(glm::ivec2 pixel) in src/renderer_vulkan/src/VulkanBackend.cpp: render all DrawItems to picking RT using ID-color fragment shader (objectId as push constant per draw), vkCmdCopyImageToBuffer from picking RT to staging buffer, vkQueueWaitIdle, map staging buffer and decode RGB pixel at (pixel.x, pixel.y) to int32_t object ID (0 = miss)
- [ ] T038 [US2] Implement outline pass in src/renderer_vulkan/src/VulkanBackend.cpp renderScene(): when any DrawItem has isSelected=true, render it alone to mask RT using outline_mask pipeline, then fullscreen composite pass using outline_comp shaders blending orange outline color
- [ ] T039 [US2] Implement left-click picking in src/editor/src/panels/ViewportPanel.cpp: on ImGui::IsMouseClicked(Left) inside viewport window, call backend.pick(mousePos) → id; if id > 0 call sceneContext.selectObject(objectWithId(id)), else sceneContext.deselectAll()
- [ ] T040 [US2] Implement W/E/R gizmo mode switching in src/editor/src/panels/ViewportPanel.cpp: add GizmoMode enum (Translate, Rotate, Scale) member defaulting to Translate; handle ImGui::IsKeyPressed(W/E/R) when an object is selected and viewport is focused
- [ ] T041 [US2] Implement translate gizmo in src/editor/src/panels/ViewportPanel.cpp: project world-space X/Y/Z axis endpoints (object position ± unit axis × scale) to screen via VP matrix + ImGui window pos; draw colored lines and circle handles with ImDrawList; on mouse-down compute closest axis by point-to-segment distance; on drag compute world-space delta and call Transform::setWorldPosition
- [ ] T042 [US2] Implement rotate and scale gizmos in src/editor/src/panels/ViewportPanel.cpp: rotate draws arc sample points projected to screen per axis ring, detects drag and applies angle delta to Transform local rotation; scale draws axis lines with square handles, detects drag and applies scale delta (center handle = uniform scale on all axes)

**Checkpoint**: Click selects object; outline renders in orange; W/E/R switches gizmo mode; dragging any axis handle correctly moves, rotates, or scales the selected object; click on empty space deselects.

---

## Phase 5: User Story 3 — Fly Camera Navigation (Priority: P3)

**Goal**: Hold right mouse button to enter fly mode; WASD + Q/E moves camera through the scene; mouse look rotates the view; releasing right mouse exits fly mode and restores the cursor.

**Independent Test**: Hold right mouse button in the viewport, move mouse → camera rotates; press W → camera moves forward through empty scene.

### Implementation for User Story 3

- [ ] T043 [US3] Create src/editor/src/FlyCamera.hpp declaring FlyCamera with position: glm::vec3 (0,2,6), yaw: float -90°, pitch: float 0°, speed: float 5.0, sensitivity: float 0.15; declare update(float dt, const ImGuiIO& io), viewMatrix() → glm::mat4, position() → const glm::vec3&
- [ ] T044 [US3] Implement FlyCamera in src/editor/src/FlyCamera.hpp (header-only) or src/editor/src/FlyCamera.cpp: update() checks ImGui::IsMouseDown(ImGuiMouseButton_Right); if active, add mouse delta × sensitivity to yaw/pitch (clamp pitch to [-89°,89°]), compute forward from yaw/pitch, move position by WASD (W/S=±forward, A/D=±right, Q/E=∓/+worldUp) × speed × dt; viewMatrix() returns glm::lookAt(position, position+forward, up)
- [ ] T045 [US3] Add FlyCamera camera member to src/editor/src/panels/ViewportPanel.hpp
- [ ] T046 [US3] Wire FlyCamera in src/editor/src/panels/ViewportPanel.cpp: call camera.update(dt, ImGui::GetIO()) at the top of draw(); replace the default identity view matrix with camera.viewMatrix() and camera.position() in the SceneRenderDesc

**Checkpoint**: Right-mouse + WASD/Q/E fully navigates the scene; releasing right mouse restores normal cursor interaction.

---

## Phase 6: User Story 4 — Scene Hierarchy Visibility and Parenting (Priority: P4)

**Goal**: All scene objects listed in the hierarchy panel with indentation reflecting parent/child depth; click to select; drag onto another object to parent (world transform preserved); right-click → Unparent or drag to root level to unparent.

**Independent Test**: Add Cube and Sphere → both appear in hierarchy → drag Sphere onto Cube → Sphere indented under Cube and its world position is unchanged.

### Implementation for User Story 4

- [ ] T047 [US4] Modify src/editor/src/panels/SceneHierarchyPanel.hpp to hold a SceneContext& reference
- [ ] T048 [US4] Implement recursive object listing in src/editor/src/panels/SceneHierarchyPanel.cpp: for each root GameObject call a recursive drawNode() that renders ImGui::TreeNodeEx with the object's name, clicking the node calls sceneContext.selectObject(), highlights the selected row with a different background color, recursing into children with indent
- [ ] T049 [US4] Implement drag-drop parenting in src/editor/src/panels/SceneHierarchyPanel.cpp: on each tree node set ImGui SetDragDropPayload("SCENE_OBJECT", &objectPtr) and AcceptDragDropPayload; on successful drop call sceneContext.setParent(dragged, target) which internally calls Transform::setParent(keepWorldTransform=true); guard against dropping an object onto its own descendant (silently ignore)
- [ ] T050 [US4] Implement drag-to-root unparenting in src/editor/src/panels/SceneHierarchyPanel.cpp: accept drag-drop payload in the empty area at the top of the panel (before any tree nodes); call sceneContext.setParent(dragged, nullptr) which calls Transform::setParent(nullptr, keepWorldTransform=true)
- [ ] T051 [US4] Implement right-click context menu in src/editor/src/panels/SceneHierarchyPanel.cpp: ImGui::BeginPopupContextItem per node; show "Unparent" item only when the object has a parent; clicking it calls sceneContext.setParent(obj, nullptr)
- [ ] T052 [P] [US4] Create tests/unit/editor/SceneHierarchyPanelTests.cpp: test that clicking an object node sets SceneContext.selectedObject, test that drag-drop parenting calls SceneContext.setParent with keepWorldTransform=true and world position is preserved, test that dragging an ancestor onto its own descendant leaves the hierarchy unchanged (uses mock IRendererBackend)

**Checkpoint**: All objects listed in hierarchy with correct indentation; click selects; drag parents with world-transform preservation; Unparent available via context menu and drag-to-root.

---

## Phase 7: User Story 5 — Inspector Panel Editing (Priority: P5)

**Goal**: Selected object's position/rotation/scale shown as editable numeric fields; Enter or Tab confirms and immediately updates the viewport; panel shows empty state when nothing is selected.

**Independent Test**: Add Cube → select it → Inspector shows Position/Rotation/Scale → type "5" in X Position and press Enter → cube moves to X=5 in viewport immediately.

### Implementation for User Story 5

- [ ] T053 [US5] Modify src/editor/src/panels/InspectorPanel.hpp to hold a SceneContext& reference
- [ ] T054 [US5] Implement transform display in src/editor/src/panels/InspectorPanel.cpp: when sceneContext.selectedObject != nullptr, show three ImGui::DragFloat3 fields (Position in world space, Rotation as Euler degrees X/Y/Z, Scale as local scale); on Enter (ImGuiInputTextFlags_EnterReturnsTrue) or Tab, apply position via Transform::setWorldPosition, rotation via Transform::setWorldRotation (convert degrees to quaternion), scale to Transform::localScale
- [ ] T055 [US5] Implement empty state in src/editor/src/panels/InspectorPanel.cpp: when sceneContext.selectedObject == nullptr, render a centered placeholder text "No object selected"; do not render any DragFloat3 fields
- [ ] T056 [P] [US5] Create tests/unit/editor/InspectorPanelTests.cpp: test that confirming a position field value calls Transform::setWorldPosition with the entered value on the selected object, test that no input widgets are rendered when selectedObject is null (uses mock IRendererBackend)

**Checkpoint**: Inspector shows position/rotation/scale for selected object; editing and pressing Enter/Tab updates the object's transform in the viewport; no fields shown when nothing selected.

---

## Phase 8: User Story 6 — Directional Light Configuration (Priority: P6)

**Goal**: Selecting the directional light shows color picker and intensity field in the Inspector below the transform fields; changes to color/intensity immediately update scene lighting; the billboard icon in the viewport is clickable to select the light.

**Independent Test**: Select "DirectionalLight" → Inspector shows Color + Intensity → change Color to (1,0,0) red → all scene objects immediately take on a red tint.

### Implementation for User Story 6

- [ ] T057 [US6] Extend src/editor/src/panels/InspectorPanel.cpp: after the transform fields, check if selectedObject has a non-null optional<LightData>; if so, render ImGui::Separator, ImGui::ColorEdit3 for LightData::color, ImGui::DragFloat for LightData::intensity (min 0); changes written directly to the LightData struct on the GameObject
- [ ] T058 [US6] Verify src/editor/src/SceneContext.cpp buildRenderDesc() correctly reads LightData from directionalLightObject (color, intensity, direction derived from Transform::forward()) into SceneRenderDesc::dirLight every frame so lighting changes take effect within one frame
- [ ] T059 [US6] Implement directional light billboard in src/editor/src/panels/ViewportPanel.cpp: project the directional light's world position (fixed e.g. (0,3,0)) to screen space using the current VP matrix; draw a small sun/star icon (circle + 8 line spokes) via ImDrawList at that screen position; on ImGui::IsMouseClicked(Left) check if mouse is within the billboard rect before GPU picking and if so call sceneContext.selectObject(directionalLightObject)

**Checkpoint**: Selecting the directional light shows full light config in Inspector; color/intensity changes update scene lighting immediately; billboard clickable in viewport.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final validation across all user stories.

- [ ] T060 Build with `cmake --build build` and confirm zero warnings under -Wall -Wextra -Wpedantic -Werror for all new and modified translation units
- [ ] T061 Run `ctest --test-dir build --output-on-failure` and confirm SonnetSceneTests, SonnetPrimitivesTests, SceneHierarchyPanelTests, and InspectorPanelTests all pass
- [ ] T062 Validate the quickstart.md primary workflow end-to-end: open editor → Add → Cube → select by viewport click → move with translate gizmo → Add a second Cube → drag second Cube onto first in hierarchy → world position preserved

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 — **BLOCKS all user stories**
- **US1 (Phase 3)**: Depends on Phase 2 — this is the MVP gate
- **US2 (Phase 4)**: Depends on US1 (needs renderScene, SceneContext.selectObject, and a rendered scene to pick from)
- **US3 (Phase 5)**: Depends on US1 (FlyCamera replaces the identity view matrix in ViewportPanel)
- **US4 (Phase 6)**: Depends on US1 (needs SceneContext with objects populated) — can run in parallel with US2/US3
- **US5 (Phase 7)**: Depends on US1 (needs SceneContext.selectedObject) — can run in parallel with US2/US3/US4
- **US6 (Phase 8)**: Depends on US1 (light in scene) + US5 (Inspector infrastructure)
- **Polish (Phase 9)**: Depends on all user story phases

### User Story Dependencies

- **US1 (P1)**: After Phase 2 — no story dependencies (MVP)
- **US2 (P2)**: After US1 — selection needs a rendered scene
- **US3 (P3)**: After US1 — FlyCamera plugs into ViewportPanel's existing renderScene call
- **US4 (P4)**: After US1 — hierarchy needs SceneContext objects
- **US5 (P5)**: After US1 — Inspector needs SceneContext.selectedObject
- **US6 (P6)**: After US1 + US5 — light in scene and Inspector base must exist

### Parallel Opportunities

- Phase 1: T002 + T003 in parallel (different CMakeLists.txt files)
- Phase 2: T007 + T008 in parallel; T010 + T011 + T014 in parallel; T016 + T017 + T018 in parallel
- Phase 3 (US1): T020 + T021 shaders in parallel
- Phase 4 (US2): T031 + T032 + T033 + T034 + T035 shaders all in parallel
- US3 + US4 + US5 can proceed in parallel after US1 (touch separate panel files)

---

## Parallel Example: User Story 1

```bash
# Shaders in parallel:
Task T020: Create forward_lit.vert
Task T021: Create forward_lit.frag

# Then sequentially:
Task T022: Modify VulkanBackend.hpp (pipeline + UBO fields)
Task T023: Implement uploadMesh / releaseMesh
Task T024: Implement renderScene
Task T025: Create SceneContext.hpp
Task T026: Implement SceneContext.cpp
Task T027: Modify Editor.hpp
Task T028: Modify Editor.cpp (Add menu)
Task T029: Modify ViewportPanel.hpp
Task T030: Modify ViewportPanel.cpp (render loop)
```

## Parallel Example: User Story 2

```bash
# All 5 shaders in parallel:
Task T031: Create picking.vert
Task T032: Create picking.frag
Task T033: Create outline_mask.frag
Task T034: Create outline_comp.vert
Task T035: Create outline_comp.frag

# Then sequentially:
Task T036: Extend VulkanBackend.hpp (picking + outline pipelines)
Task T037: Implement pick()
Task T038: Implement outline pass in renderScene
Task T039: Left-click picking in ViewportPanel
Task T040: W/E/R gizmo mode switching
Task T041: Translate gizmo
Task T042: Rotate + scale gizmos
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational — **CRITICAL, blocks everything**
3. Complete Phase 3: US1 (Primitive Instantiation + Lighting)
4. **STOP and VALIDATE**: `cmake --build build && ctest --test-dir build --output-on-failure`; manually add a Cube and confirm it renders with lighting
5. **MVP is shippable here** — visible 3D scene with lit primitives

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. US1 → rendered primitives + lighting (MVP demo)
3. US2 → click to select + outline + gizmos (core editing loop)
4. US3 → fly camera (full spatial navigation)
5. US4 → hierarchy + parenting (scene organization)
6. US5 → inspector numeric editing (precise transforms)
7. US6 → light color/intensity configuration (visual quality)
8. Polish → zero warnings, all tests green, quickstart validated

---

## Notes

- [P] tasks = different files with no unmet dependencies — safe to parallelize
- [USn] label maps every task to its user story for traceability
- Shaders must be written before the VulkanBackend pipelines that load them
- Run `ctest` after each phase checkpoint to catch regressions early
- All new code compiled with `sonnet_set_compile_options()` — zero warnings expected under -Wall -Wextra -Wpedantic -Werror
- GPU mesh handle 0 is the sentinel "skip" value; uploadMesh() counter starts at 1
- All primitive instances of the same type share a single GPU mesh upload (model matrix distinguishes them)
