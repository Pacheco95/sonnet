#include <sonnet/world/DrawList.h>

#include <sonnet/core/Profile.h>

namespace sonnet::world {

namespace {

// The entities with `Components` that are shown: an entity is hidden by its own Disabled tag or
// by an ancestor's, so hiding a part of an instance hides what is under it too (ADR-0028).
template <typename... Components> flecs::query<Components...> shown(const World &world) {
  return world.ecs().query_builder<Components...>().template without<Disabled>().self().up(flecs::ChildOf).build();
}

renderer::Camera placedCamera(const WorldTransform &transform, const Camera &camera) {
  const Transform placed = Transform::fromMatrix(transform.matrix);
  return {.position = placed.position,
          .rotation = placed.rotation,
          .fovY = camera.fovY,
          .nearPlane = camera.nearPlane,
          .projectionKind = camera.projectionKind,
          .orthoHeight = camera.orthoHeight,
          .farPlane = camera.farPlane,
          .exposure = camera.exposure,
          .bloomStrength = glm::clamp(camera.bloomStrength, 0.0f, 1.0f)};
}

} // namespace

namespace {

// One mesh renderer's items at `transform`. `entity`, when it is one, names the skinned or morphed instance, takes
// the pose and weights it has and writes its pick id; without it the mesh is drawn as it is.
void appendDraws(assets::AssetDatabase &assets, flecs::entity entity, const MeshRenderer &meshRenderer,
                 const glm::mat4 &transform, const glm::mat4 *previous, std::vector<renderer::DrawItem> &draws,
                 std::vector<glm::mat4> &joints, std::vector<float> &morphWeights) {
  // Requested rather than loaded: an asset that is not in memory yet imports on the job system
  // and draws on a later frame, instead of stalling this one (ADR-0013).
  const renderer::MeshHandle mesh = assets.requestMesh(meshRenderer.mesh);
  if (!mesh) {
    return; // missing, failed, or still importing: nothing drawn this frame
  }
  const assets::AssetInfo *info = assets.find(meshRenderer.mesh);
  const renderer::MaterialHandle override = assets.material(meshRenderer.material);
  const std::span<const renderer::Submesh> submeshes = assets.renderer().submeshes(mesh);
  // A skinned mesh's pose is shared by its submeshes; without one it draws in its bind pose.
  const auto firstJoint = static_cast<std::uint32_t>(joints.size());
  std::uint32_t jointCount = 0;
  if (const SkinPose *pose = entity ? entity.try_get<SkinPose>() : nullptr;
      pose != nullptr && entity.has<SkinnedMesh>()) {
    joints.insert(joints.end(), pose->joints.begin(), pose->joints.end());
    jointCount = static_cast<std::uint32_t>(pose->joints.size());
  }
  // Likewise the morph weights.
  const auto firstMorphWeight = static_cast<std::uint32_t>(morphWeights.size());
  std::uint32_t morphWeightCount = 0;
  if (const MorphWeights *morph = entity ? entity.try_get<MorphWeights>() : nullptr; morph != nullptr) {
    morphWeights.insert(morphWeights.end(), morph->weights.begin(), morph->weights.end());
    morphWeightCount = static_cast<std::uint32_t>(morph->weights.size());
  }
  std::optional<glm::mat4> previousTransform;
  if (previous != nullptr) {
    previousTransform = *previous;
  }
  for (std::uint32_t i = 0; i < submeshes.size(); ++i) {
    renderer::MaterialHandle material = override;
    if (!material && info != nullptr && submeshes[i].materialSlot < info->materials.size()) {
      material = assets.material(info->materials[submeshes[i].materialSlot]);
    }
    draws.push_back({.mesh = mesh,
                     .submesh = i,
                     .material = material,
                     .transform = transform,
                     .previousTransform = previousTransform,
                     .color = meshRenderer.color,
                     .id = entity ? World::pickId(entity) : 0,
                     .firstJoint = firstJoint,
                     .jointCount = jointCount,
                     .skinInstance = jointCount > 0 || morphWeightCount > 0 ? entity.id() : 0,
                     .firstMorphWeight = firstMorphWeight,
                     .morphWeightCount = morphWeightCount});
  }
}

// The renderers of `entity` and everything shown under it, placed relative to `parent`.
void appendSubtree(const World &world, assets::AssetDatabase &assets, flecs::entity entity, const glm::mat4 &parent,
                   std::vector<renderer::DrawItem> &draws, std::vector<glm::mat4> &joints,
                   std::vector<float> &morphWeights) {
  if (entity.has<Disabled>()) {
    return;
  }
  const Transform *local = entity.try_get<Transform>();
  const glm::mat4 matrix = local != nullptr ? parent * local->matrix() : parent;
  if (const MeshRenderer *meshRenderer = entity.try_get<MeshRenderer>();
      meshRenderer != nullptr && meshRenderer->visible) {
    appendDraws(assets, entity, *meshRenderer, matrix, nullptr, draws, joints, morphWeights);
  }
  for (const flecs::entity child : world.children(entity)) {
    appendSubtree(world, assets, child, matrix, draws, joints, morphWeights);
  }
}

} // namespace

void buildDrawList(const World &world, assets::AssetDatabase &assets, std::vector<renderer::DrawItem> &draws,
                   std::vector<glm::mat4> &joints, std::vector<float> &morphWeights) {
  SONNET_ZONE();
  draws.clear();
  joints.clear();
  morphWeights.clear();
  const auto meshes = shown<const WorldTransform, const MeshRenderer>(world);
  meshes.each([&](flecs::entity entity, const WorldTransform &transform, const MeshRenderer &meshRenderer) {
    if (!meshRenderer.visible) {
      return;
    }
    const PreviousWorldTransform *previous = entity.try_get<PreviousWorldTransform>();
    appendDraws(assets, entity, meshRenderer, transform.matrix, previous != nullptr ? &previous->matrix : nullptr,
                draws, joints, morphWeights);
  });
}

void buildDrawList(const World &world, assets::AssetDatabase &assets, flecs::entity root,
                   std::vector<renderer::DrawItem> &draws, std::vector<glm::mat4> &joints,
                   std::vector<float> &morphWeights) {
  SONNET_ZONE();
  draws.clear();
  joints.clear();
  morphWeights.clear();
  // The root stands at the origin whatever its own transform is; only what is under it places the parts.
  const Transform *rootTransform = root.try_get<Transform>();
  const glm::mat4 inverse = rootTransform != nullptr ? glm::inverse(rootTransform->matrix()) : glm::mat4{1.0f};
  appendSubtree(world, assets, root, inverse, draws, joints, morphWeights);
}

void buildDrawList(assets::AssetDatabase &assets, const core::Uuid &mesh, std::vector<renderer::DrawItem> &draws) {
  draws.clear();
  std::vector<glm::mat4> joints;
  std::vector<float> morphWeights;
  appendDraws(assets, {}, {.mesh = mesh, .color = {1.0f, 1.0f, 1.0f, 1.0f}}, glm::mat4{1.0f}, nullptr, draws, joints,
              morphWeights);
}

void buildParticleList(const World &world, assets::AssetDatabase &assets,
                       std::vector<renderer::ParticleEmitterItem> &emitters) {
  SONNET_ZONE();
  emitters.clear();
  const bool playing = world.isPlaying();
  const auto particles = shown<const WorldTransform, const ParticleEmitter>(world);
  particles.each([&](flecs::entity entity, const WorldTransform &transform, const ParticleEmitter &emitter) {
    emitters.push_back(
        {.key = entity.id(),
         .transform = transform.matrix,
         .id = World::pickId(entity),
         .maxParticles = emitter.maxParticles,
         .rate = emitter.rate,
         .burst = emitter.burst,
         .lifetime = {emitter.lifetimeMin, std::max(emitter.lifetimeMin, emitter.lifetimeMax)},
         .speed = {emitter.speedMin, std::max(emitter.speedMin, emitter.speedMax)},
         .coneAngle = emitter.coneAngle,
         .gravity = emitter.gravity,
         .drag = emitter.drag,
         .sizeStart = emitter.sizeStart,
         .sizeEnd = emitter.sizeEnd,
         .colorStart = emitter.colorStart,
         .colorEnd = emitter.colorEnd,
         .texture = emitter.texture.isNil() ? renderer::TextureHandle{} : assets.requestTexture(emitter.texture),
         .blend = emitter.blend == ParticleBlendMode::Additive ? renderer::ParticleBlend::Additive
                                                               : renderer::ParticleBlend::Alpha,
         .local = emitter.space == ParticleSpace::Local,
         .seed = emitter.seed,
         .simulate = playing && emitter.playing});
  });
}

void buildLightList(const World &world, std::vector<renderer::Light> &lights) {
  SONNET_ZONE();
  lights.clear();
  const auto points = shown<const WorldTransform, const PointLight>(world);
  points.each([&](const WorldTransform &transform, const PointLight &light) {
    lights.push_back(renderer::Light{.type = renderer::LightType::Point,
                                     .position = glm::vec3{transform.matrix[3]},
                                     .color = light.color,
                                     .intensity = light.intensity,
                                     .range = light.range,
                                     .castsShadows = light.castsShadows});
  });
  const auto spots = shown<const WorldTransform, const SpotLight>(world);
  spots.each([&](const WorldTransform &transform, const SpotLight &light) {
    lights.push_back(renderer::Light{
        .type = renderer::LightType::Spot,
        .position = glm::vec3{transform.matrix[3]},
        .color = light.color,
        .intensity = light.intensity,
        .range = light.range,
        .direction = glm::normalize(glm::mat3{transform.matrix} * glm::vec3{0.0f, 0.0f, -1.0f}),
        .innerAngle = light.innerAngle,
        .outerAngle = light.outerAngle,
        .castsShadows = light.castsShadows,
    });
  });
}

std::optional<renderer::DirectionalLight> sceneLight(const World &world) {
  std::optional<renderer::DirectionalLight> result;
  const auto suns = shown<const WorldTransform, const DirectionalLight>(world);
  suns.each([&](const WorldTransform &transform, const DirectionalLight &light) {
    if (result) {
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
  shown<const WorldTransform, const Camera>(world).each([&](const WorldTransform &transform, const Camera &camera) {
    if (!result) {
      result = placedCamera(transform, camera);
    }
  });
  return result;
}

void buildCameraList(const World &world, std::vector<renderer::Camera> &cameras) {
  cameras.clear();
  shown<const WorldTransform, const Camera>(world).each([&](const WorldTransform &transform, const Camera &camera) {
    cameras.push_back(placedCamera(transform, camera));
  });
}

renderer::Camera fallbackCamera() {
  return {.position = {6.0f, 4.0f, 8.0f},
          .rotation = glm::quatLookAt(glm::normalize(glm::vec3{0.0f, 0.5f, 0.0f} - glm::vec3{6.0f, 4.0f, 8.0f}),
                                      glm::vec3{0.0f, 1.0f, 0.0f})};
}

std::optional<SceneEnvironment> sceneEnvironment(const World &world, assets::AssetDatabase &assets) {
  std::optional<SceneEnvironment> result;
  shown<const Environment>(world).each([&](const Environment &environment) {
    if (result) {
      return;
    }
    if (const renderer::EnvironmentHandle handle = assets.environment(environment.map)) {
      result = SceneEnvironment{handle, environment.intensity};
    }
  });
  return result;
}

flecs::entity environmentEntity(const World &world) {
  return shown<const Environment>(world).first();
}

} // namespace sonnet::world
