# Data Model: 3D Scene Editor

**Branch**: `006-3d-scene-editor` | **Date**: 2026-05-03

## Module: SonnetScene

### Transform (`src/scene/include/sonnet/scene/Transform.hpp`)

Owns local position/rotation/scale and a pointer to its parent. Computes the world-space model matrix lazily via a dirty flag.

```
Transform
├── localPosition: glm::vec3          default (0, 0, 0)
├── localRotation: glm::quat          default identity
├── localScale:    glm::vec3          default (1, 1, 1)
├── parent:        Transform*         nullable; non-owning
├── children:      vector<Transform*> non-owning
└── modelMatrix:   glm::mat4          mutable cache; recomputed on dirty

Key operations:
  setParent(Transform*, bool keepWorldTransform)
  setWorldPosition(glm::vec3)   → computes required local offset
  setWorldRotation(glm::quat)   → computes required local rotation
  getWorldPosition() → glm::vec3
  getWorldRotation() → glm::quat
  getModelMatrix()   → const glm::mat4& (lazy)
  forward() / up() / right() → glm::vec3 (world-space)
```

**Invariants**:
- A Transform may not be its own ancestor (circular parenting is silently rejected in `setParent`)
- When `setParent(..., keepWorldTransform=true)`, the new local transform is computed so that world position/rotation/scale are preserved

---

### LightData (`inside GameObject.hpp`)

Embedded struct; used only when `GameObject::light` is set.

```
LightData
├── color:     glm::vec3   default (1, 1, 1) white
├── intensity: float       default 1.0
└── direction: glm::vec3   default (0, -1, 0) pointing down
                           (driven by Transform rotation in the editor)
```

---

### PrimitiveType (`inside GameObject.hpp`)

Scoped enum identifying the mesh shape for GPU mesh lookup.

```
enum class PrimitiveType { Cube, Sphere, Cylinder, Plane, Capsule }
```

---

### GameObject (`src/scene/include/sonnet/scene/GameObject.hpp`)

A named scene node with a Transform and optional component data.

```
GameObject
├── name:          string             auto-generated; mutable
├── enabled:       bool               default true
├── transform:     Transform          owned
├── light:         optional<LightData>    set for directional light object
└── primitiveType: optional<PrimitiveType> set for primitive objects

Note: GPU mesh handles are NOT stored here. They live in SceneContext.
```

---

### Scene (`src/scene/include/sonnet/scene/Scene.hpp`)

Flat owner of all GameObjects.

```
Scene
└── objects: vector<unique_ptr<GameObject>>

Key operations:
  createObject(string name, GameObject* parent) → GameObject&
  destroyObject(GameObject*)
  objects() → const vector<unique_ptr<GameObject>>&
```

---

## Module: SonnetPrimitives

### CPUMesh output format (defined in SonnetRenderer)

All generators produce a `CPUMesh` with the interleaved vertex layout shared across all shaders:

```
Vertex
├── position: glm::vec3   shader location 0
├── texCoord: glm::vec2   shader location 2
└── normal:   glm::vec3   shader location 3

CPUMesh
├── vertices: vector<Vertex>
└── indices:  vector<uint32_t>
```

### Generators (`src/primitives/include/sonnet/primitives/MeshPrimitives.hpp`)

| Function | Shape | Vertex count (default params) | Notes |
|---|---|---|---|
| `makeBox(glm::vec3 size)` | Cube | 24 | 4 verts/face, flat normals |
| `makeUVSphere(int segX, int segY)` | Sphere | (segX+1)×(segY+1) | Smooth normals |
| `makeCylinder(float r, float h, int seg)` | Cylinder | 2 + 2×seg + 2×(seg+1) | Flat cap normals, smooth side normals |
| `makePlane(glm::vec2 size)` | Quad plane | 4 | Facing +Y, normal (0,1,0) |
| `makeCapsule(float r, float h, int seg)` | Capsule | ~(seg×seg/2)×2 + 2×(seg+1) | Top/bottom hemispheres + cylinder body |

---

## Module: SonnetRenderer (new public types)

### CPUMesh (`src/renderer/include/sonnet/renderer/CPUMesh.hpp`)

As defined above. No GPU types.

### SceneRenderDesc (`src/renderer/include/sonnet/renderer/SceneRenderTypes.hpp`)

Passed to `IRendererBackend::renderScene()` each frame.

```
DrawItem
├── meshHandle:   uint64_t    from uploadMesh(); 0 = skip
├── modelMatrix:  glm::mat4
├── objectId:     uint32_t    1-based; used for picking color encoding
└── isSelected:   bool        renders outline mask if true

DirectionalLightDesc
├── direction:  glm::vec3
├── color:      glm::vec3
└── intensity:  float

SceneRenderDesc
├── viewMatrix:     glm::mat4
├── projMatrix:     glm::mat4
├── cameraPosition: glm::vec3
├── viewportSize:   glm::ivec2
├── dirLight:       optional<DirectionalLightDesc>
├── objects:        vector<DrawItem>
└── outlineColor:   glm::vec3   default (1, 0.5, 0) orange
```

---

## Module: SonnetEditor (internal types, not in public headers)

### SceneContext (`src/editor/src/SceneContext.hpp`)

Owns the runtime scene state and bridges `SonnetScene` types to `IRendererBackend`.

```
SceneContext
├── scene:          world::Scene             owned
├── backend:        IRendererBackend&        non-owning ref
├── meshHandles:    map<PrimitiveType, uint64_t>  shared handles per type
│                                            (all Cubes share one GPU mesh)
├── selectedObject: GameObject*              nullable; non-owning
└── directionalLightObject: GameObject*     nullable; non-owning; at most one

Key operations:
  addPrimitive(PrimitiveType) → GameObject&
  addDirectionalLight()       → GameObject* (null if one already exists)
  selectObject(GameObject*)
  deselectAll()
  setParent(GameObject* child, GameObject* newParent)   (null newParent = unparent)
  buildRenderDesc(glm::mat4 view, glm::mat4 proj, ...) → SceneRenderDesc
```

**Note on mesh handle sharing**: Because all cubes are identical geometry, a single GPU mesh upload serves all Cube objects — the per-object model matrix distinguishes them. This avoids re-uploading the same mesh N times. `meshHandles` is populated lazily on first use of each PrimitiveType.

### FlyCamera (`src/editor/src/FlyCamera.hpp`)

Stateful fly camera controller driven by ImGui input queries inside `ViewportPanel::draw()`.

```
FlyCamera
├── position: glm::vec3    default (0, 2, 6)
├── yaw:      float        default -90°
├── pitch:    float        default 0°, clamped [-89°, 89°]
├── speed:    float        default 5.0 units/s
└── sensitivity: float     default 0.15°/pixel

Key operations:
  update(float dt, ImGuiIO& io)  → advances position/orientation
  viewMatrix()  → glm::mat4
  position()    → const glm::vec3&
```

Fly mode activates when `ImGui::IsMouseDown(ImGuiMouseButton_Right)` inside the viewport window, matching FR-004. `ImGui::GetIO().DeltaTime` provides `dt` without changing the `IPanel::draw()` signature.

---

## Naming Convention for Auto-Generated Objects

| PrimitiveType | First instance | Nth instance (N ≥ 1) |
|---|---|---|
| Cube | "Cube" | "Cube1", "Cube2", … |
| Sphere | "Sphere" | "Sphere1", … |
| Cylinder | "Cylinder" | "Cylinder1", … |
| Plane | "Plane" | "Plane1", … |
| Capsule | "Capsule" | "Capsule1", … |
| Directional Light | "DirectionalLight" | N/A — at most one |

The counter per type is maintained by `SceneContext` and increments monotonically (no reuse on deletion — deletion is out of scope).

---

## Shader Uniform Layout

All forward-lit objects share a `CameraUBO` (set 0, binding 0) and `LightsUBO` (set 0, binding 1). The model matrix is passed as a push constant. The outline composite pass uses a separate sampler descriptor (set 0, binding 0 on its own layout).

```
CameraUBO (std140)
├── view:        mat4
├── proj:        mat4
└── cameraPos:   vec3 (+ 4 bytes padding)

LightsUBO (std140)
├── dirDirection:  vec4   (w unused; avoids alignment issues)
├── dirColor:      vec4   (w unused)
└── dirIntensity:  float  (+ padding)

Push constant (per draw, forward lit + picking pipelines)
└── model: mat4   (64 bytes)
```
