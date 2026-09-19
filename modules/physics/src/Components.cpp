#include <sonnet/physics/Components.h>

#include <sonnet/world/World.h>

namespace sonnet::physics {

void registerComponents(world::World &world) {
  if (world.findComponent("RigidBody") != nullptr) {
    return;
  }
  // Constants are reflected from the enumerators' names.
  world.ecs().component<BodyType>("BodyType");
  world.registerComponent<RigidBody>("RigidBody")
      .member<BodyType>("type")
      .member<float>("mass")
      .member<float>("friction")
      .member<float>("restitution")
      .member<float>("linearDamping")
      .member<float>("angularDamping")
      .member<float>("gravityScale");
  world.registerComponent<BoxCollider>("BoxCollider").member<glm::vec3>("halfExtents").member<glm::vec3>("offset");
  world.registerComponent<SphereCollider>("SphereCollider").member<float>("radius").member<glm::vec3>("offset");
  world.registerComponent<CapsuleCollider>("CapsuleCollider")
      .member<float>("radius")
      .member<float>("halfHeight")
      .member<glm::vec3>("offset");
  world.registerComponent<MeshCollider>("MeshCollider").member<core::Uuid>("mesh");
}

} // namespace sonnet::physics
