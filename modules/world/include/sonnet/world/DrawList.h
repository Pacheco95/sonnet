#pragma once

#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>

#include <optional>
#include <vector>

namespace sonnet::world {

// What the world hands the renderer (docs/architecture.md, "Dependency rule"): one draw item per
// submesh of every visible MeshRenderer with a world transform, tagged with the entity's pick
// id, meshes and materials resolved through the database every frame so a re-imported asset
// shows on the next one. A skinned mesh with a pose appends its joint matrices to `joints`, for
// SceneView::joints, and its items name their range and the entity as the skinned instance.
void buildDrawList(const World &world, assets::AssetDatabase &assets, std::vector<renderer::DrawItem> &draws,
                   std::vector<glm::mat4> &joints);
// Every enabled point and spot light, placed by its entity's world transform.
void buildLightList(const World &world, std::vector<renderer::Light> &lights);
// The first directional light, shining along its entity's -Z.
[[nodiscard]] std::optional<renderer::DirectionalLight> sceneLight(const World &world);
// The first camera, placed by its entity's world transform.
[[nodiscard]] std::optional<renderer::Camera> sceneCamera(const World &world);

struct SceneEnvironment {
  renderer::EnvironmentHandle environment;
  float intensity{1.0f};
  float exposure{1.0f};
};
// The first Environment component, its map loaded through the database; none without one or
// when the map failed to load.
[[nodiscard]] std::optional<SceneEnvironment> sceneEnvironment(const World &world, assets::AssetDatabase &assets);

} // namespace sonnet::world
