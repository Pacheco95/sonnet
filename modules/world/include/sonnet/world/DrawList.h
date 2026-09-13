#pragma once

#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>

#include <array>
#include <optional>
#include <vector>

namespace sonnet::world {

// One renderer mesh per Primitive, shared by every MeshRenderer.
class PrimitiveMeshes {
public:
  explicit PrimitiveMeshes(renderer::Renderer &renderer);
  ~PrimitiveMeshes();
  PrimitiveMeshes(const PrimitiveMeshes &) = delete;
  PrimitiveMeshes &operator=(const PrimitiveMeshes &) = delete;

  [[nodiscard]] renderer::MeshHandle mesh(Primitive primitive) const noexcept;

private:
  renderer::Renderer &m_renderer;
  std::array<renderer::MeshHandle, 5> m_meshes;
};

// What the world hands the renderer (docs/architecture.md, "Dependency rule"): draw items for
// every visible MeshRenderer with a world transform, tagged with the entity's pick id.
void buildDrawList(const World &world, const PrimitiveMeshes &meshes, std::vector<renderer::DrawItem> &draws);
// The first directional light, shining along its entity's -Z.
[[nodiscard]] std::optional<renderer::DirectionalLight> sceneLight(const World &world);
// The first camera, placed by its entity's world transform.
[[nodiscard]] std::optional<renderer::Camera> sceneCamera(const World &world);

} // namespace sonnet::world
