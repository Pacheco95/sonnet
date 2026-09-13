#include <sonnet/world/DrawList.h>

#include <sonnet/core/Profile.h>

namespace sonnet::world {

void buildDrawList(const World &world, assets::AssetDatabase &assets, std::vector<renderer::DrawItem> &draws) {
  SONNET_ZONE();
  draws.clear();
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const MeshRenderer &meshRenderer) {
    if (!meshRenderer.visible || entity.has<Disabled>()) {
      return;
    }
    const renderer::MeshHandle mesh = assets.mesh(meshRenderer.mesh);
    if (!mesh) {
      return; // missing or failed asset: logged by the database, nothing drawn
    }
    const assets::AssetInfo *info = assets.find(meshRenderer.mesh);
    const renderer::MaterialHandle override = assets.material(meshRenderer.material);
    const std::span<const renderer::Submesh> submeshes = assets.renderer().submeshes(mesh);
    for (std::uint32_t i = 0; i < submeshes.size(); ++i) {
      renderer::MaterialHandle material = override;
      if (!material && info != nullptr && submeshes[i].materialSlot < info->materials.size()) {
        material = assets.material(info->materials[submeshes[i].materialSlot]);
      }
      draws.push_back({.mesh = mesh,
                       .submesh = i,
                       .material = material,
                       .transform = transform.matrix,
                       .color = meshRenderer.color,
                       .id = World::pickId(entity)});
    }
  });
}

void buildLightList(const World &world, std::vector<renderer::Light> &lights) {
  SONNET_ZONE();
  lights.clear();
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const PointLight &light) {
    if (entity.has<Disabled>()) {
      return;
    }
    lights.push_back(renderer::Light{.type = renderer::LightType::Point,
                                     .position = glm::vec3{transform.matrix[3]},
                                     .color = light.color,
                                     .intensity = light.intensity,
                                     .range = light.range});
  });
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const SpotLight &light) {
    if (entity.has<Disabled>()) {
      return;
    }
    lights.push_back(renderer::Light{
        .type = renderer::LightType::Spot,
        .position = glm::vec3{transform.matrix[3]},
        .color = light.color,
        .intensity = light.intensity,
        .range = light.range,
        .direction = glm::normalize(glm::mat3{transform.matrix} * glm::vec3{0.0f, 0.0f, -1.0f}),
        .innerAngle = light.innerAngle,
        .outerAngle = light.outerAngle,
    });
  });
}

std::optional<renderer::DirectionalLight> sceneLight(const World &world) {
  std::optional<renderer::DirectionalLight> result;
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const DirectionalLight &light) {
    if (result || entity.has<Disabled>()) {
      return;
    }
    result = renderer::DirectionalLight{.direction =
                                            glm::normalize(glm::mat3{transform.matrix} * glm::vec3{0.0f, 0.0f, -1.0f}),
                                        .color = light.color,
                                        .intensity = light.intensity};
  });
  return result;
}

std::optional<renderer::Camera> sceneCamera(const World &world) {
  std::optional<renderer::Camera> result;
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const Camera &camera) {
    if (result || entity.has<Disabled>()) {
      return;
    }
    const Transform placed = Transform::fromMatrix(transform.matrix);
    result = renderer::Camera{
        .position = placed.position, .rotation = placed.rotation, .fovY = camera.fovY, .nearPlane = camera.nearPlane};
  });
  return result;
}

std::optional<SceneEnvironment> sceneEnvironment(const World &world, assets::AssetDatabase &assets) {
  std::optional<SceneEnvironment> result;
  world.ecs().each([&](flecs::entity entity, const Environment &environment) {
    if (result || entity.has<Disabled>()) {
      return;
    }
    if (const renderer::EnvironmentHandle handle = assets.environment(environment.map)) {
      result = SceneEnvironment{handle, environment.intensity, environment.exposure};
    }
  });
  return result;
}

} // namespace sonnet::world
