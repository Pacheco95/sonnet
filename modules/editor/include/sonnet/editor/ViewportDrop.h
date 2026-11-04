#pragma once

#include <sonnet/world/World.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Camera.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>

#include <flecs.h>

#include <cstdint>
#include <optional>
#include <span>

namespace sonnet::editor {

// The pure pieces of dropping an asset on the viewport (docs/editor.md): what the drop does, what is under
// the cursor, where the dropped object lands and where it rests. None of them needs Dear ImGui or a GPU.

enum class DropAction : std::uint8_t {
  None,
  Instance,    // a model's prefab or a prefab file: an instance of it
  Mesh,        // an entity with a MeshRenderer of it
  Material,    // the MeshRenderer.material of the object under the cursor
  Script,      // one more Scripts slot on the object under the cursor
  Environment, // the scene's sky
};

// What dropping `uuid` does: Instance when a world prefab carries that identity, as on the hierarchy,
// otherwise the asset's type decides, and None for any other type or an unknown identity.
[[nodiscard]] DropAction dropAction(const world::World &world, const assets::AssetDatabase &assets,
                                    const core::Uuid &uuid);

// The nearest triangle under a ray: the entity it belongs to, the point, and the triangle's normal in
// world space, facing the ray.
struct SceneHit {
  flecs::entity entity;
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

// The nearest triangle the ray crosses among the shown meshes: those of visible MeshRenderers whose entity
// and ancestors are not Disabled, as the draw list takes them, and whose mesh is loaded. A mesh still
// importing is skipped rather than waited for. Skinned meshes are tested in their bind pose and morph
// targets are ignored. Every triangle of a mesh whose bounds the ray crosses is tested.
[[nodiscard]] std::optional<SceneHit> raycastScene(const world::World &world, assets::AssetDatabase &assets,
                                                   const renderer::Ray &ray);

// Where a dropped object lands, and the normal of the surface it landed on, facing the camera; no normal
// in the air.
struct Landing {
  glm::vec3 point{0.0f};
  std::optional<glm::vec3> normal;
};

// On `hit`; else where the ray meets the plane through the origin with `gridNormal`, the plane the grid
// shows; else `fallback` metres along the ray. With `snapStep`, the point is rounded to its multiples on
// the axes where `gridNormal` is zero, and keeps its coordinate along the third.
[[nodiscard]] Landing land(const renderer::Ray &ray, const std::optional<SceneHit> &hit, glm::vec3 gridNormal,
                           float fallback, std::optional<float> snapStep);

// Where an object made of `items` (their transforms relative to its origin) stands so that its meshes'
// bounds, posed by `pose * item.transform`, touch the landed-on surface: the landing point pushed out along
// the normal by the lowest corner. The landing point itself without a normal or without a loaded mesh.
[[nodiscard]] glm::vec3 restingPosition(const Landing &landing, const renderer::Renderer &renderer,
                                        std::span<const renderer::DrawItem> items, const glm::mat4 &pose);

} // namespace sonnet::editor
