#include <sonnet/world/DrawList.h>

#include <sonnet/core/Profile.h>
#include <sonnet/renderer/Primitives.h>

namespace sonnet::world {

PrimitiveMeshes::PrimitiveMeshes(renderer::Renderer &renderer) : m_renderer(renderer) {
  m_meshes[static_cast<std::size_t>(Primitive::Box)] = renderer.createMesh(renderer::primitives::box(), "box");
  m_meshes[static_cast<std::size_t>(Primitive::Sphere)] = renderer.createMesh(renderer::primitives::sphere(), "sphere");
  m_meshes[static_cast<std::size_t>(Primitive::Plane)] = renderer.createMesh(renderer::primitives::plane(), "plane");
  m_meshes[static_cast<std::size_t>(Primitive::Cylinder)] =
      renderer.createMesh(renderer::primitives::cylinder(), "cylinder");
  m_meshes[static_cast<std::size_t>(Primitive::Capsule)] =
      renderer.createMesh(renderer::primitives::capsule(), "capsule");
}

PrimitiveMeshes::~PrimitiveMeshes() {
  for (const renderer::MeshHandle mesh : m_meshes) {
    m_renderer.destroyMesh(mesh);
  }
}

renderer::MeshHandle PrimitiveMeshes::mesh(Primitive primitive) const noexcept {
  const auto index = static_cast<std::size_t>(primitive);
  return index < m_meshes.size() ? m_meshes[index] : renderer::MeshHandle{};
}

void buildDrawList(const World &world, const PrimitiveMeshes &meshes, std::vector<renderer::DrawItem> &draws) {
  SONNET_ZONE();
  draws.clear();
  world.ecs().each([&](flecs::entity entity, const WorldTransform &transform, const MeshRenderer &renderer) {
    if (!renderer.visible || entity.has<Disabled>()) {
      return;
    }
    draws.push_back({.mesh = meshes.mesh(renderer.primitive),
                     .transform = transform.matrix,
                     .color = renderer.color,
                     .id = World::pickId(entity)});
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

} // namespace sonnet::world
