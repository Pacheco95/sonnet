#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>

#include <cstdint>

namespace sonnet::world {
class World;
}

namespace sonnet::physics {

// The physics components (docs/physics.md): plain structs registered with reflection in the
// world, so the inspector, scene files and scripts reach them like the core components. An entity
// with a collider is a body, static unless its RigidBody says otherwise; several colliders on one
// entity make one compound body. Shapes are in the entity's local space and follow its scale.

// 32-bit, the width reflection reads enum values with.
enum class BodyType : std::int32_t {
  Static,    // never moves by itself; its entity may still be moved, which teleports it
  Kinematic, // follows its entity's transform, pushing dynamic bodies out of the way
  Dynamic,   // simulated; writes its pose back into the entity's transform
};

struct RigidBody {
  BodyType type{BodyType::Dynamic};
  float mass{1.0f}; // kilograms
  float friction{0.5f};
  float restitution{0.0f};
  float linearDamping{0.05f};
  float angularDamping{0.05f};
  float gravityScale{1.0f};
};

struct BoxCollider {
  glm::vec3 halfExtents{0.5f, 0.5f, 0.5f};
  glm::vec3 offset{0.0f, 0.0f, 0.0f};
};

struct SphereCollider {
  float radius{0.5f};
  glm::vec3 offset{0.0f, 0.0f, 0.0f};
};

// Along the local Y axis: a cylinder of 2 * halfHeight capped by half spheres.
struct CapsuleCollider {
  float radius{0.5f};
  float halfHeight{0.5f};
  glm::vec3 offset{0.0f, 0.0f, 0.0f};
};

// The triangles of a mesh asset for static and kinematic bodies, their convex hull for dynamic
// ones. A nil mesh takes the entity's MeshRenderer mesh.
struct MeshCollider {
  core::Uuid mesh{};
};

// Registers the components above with the world's reflection under their scene-file names. Done
// by createPhysicsWorld; separate for tools that read scenes without simulating them. Idempotent.
void registerComponents(world::World &world);

} // namespace sonnet::physics
