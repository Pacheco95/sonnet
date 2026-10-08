#pragma once

#include <sonnet/physics/Components.h>

#include <sonnet/core/Math.h>
#include <sonnet/renderer/SceneView.h>

#include <flecs.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
}

namespace sonnet::core {
class JobSystem;
}

namespace sonnet::world {
class World;
}

namespace sonnet::physics {

struct PhysicsDesc {
  glm::vec3 gravity{0.0f, -9.81f, 0.0f};
  std::uint32_t maxBodies{16384};
};

struct RaycastHit {
  flecs::entity entity;
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f};
  float distance{0.0f};
};

enum class ContactEventKind : std::uint8_t {
  ContactBegin, // two solid bodies started touching
  ContactEnd,   // ... stopped touching
  TriggerEnter, // a body started overlapping a Trigger
  TriggerExit,  // ... stopped overlapping it
};

// One thing that happened between two entities during the last fixed step (ADR-0022). The pair is
// ordered by entity id, so `first` is the lower. `point` and `normal` describe a ContactBegin: a
// world-space point on the surface and the direction from `first` towards `second`; they are zero
// for the other kinds. An entity may have been destroyed or disabled since, so consumers check.
struct ContactEvent {
  ContactEventKind kind{ContactEventKind::ContactBegin};
  flecs::entity first;
  flecs::entity second;
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f};
};

// Rigid bodies for a world's entities (ADR-0009). Bodies are created by the physics step, in the
// FixedUpdate phase of play mode, for every enabled entity with a collider, and destroyed when
// the entity, its colliders or its rigid body go away or change; edit mode has none. Static and
// kinematic bodies follow their entity's world transform; dynamic bodies write theirs back into
// the local Transform, interpolated between the last two steps each frame, and a Transform
// written by anyone else teleports the body. Queries and forces take entities; an entity without
// a body is ignored, and one that should have a body gets it at once.
class IPhysicsWorld {
public:
  virtual ~IPhysicsWorld() = default;

  // The nearest body along the ray within maxDistance, skipping `ignore`'s body, which a ray cast
  // from inside an entity's own collider would otherwise hit first; `direction` need not be
  // normalised.
  [[nodiscard]] virtual std::optional<RaycastHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
                                                          flecs::entity ignore = {}) = 0;
  // Dynamic bodies only; an impulse changes the velocity at once, a force acts over the next step.
  virtual void addImpulse(flecs::entity entity, glm::vec3 impulse) = 0;
  virtual void addForce(flecs::entity entity, glm::vec3 force) = 0;
  [[nodiscard]] virtual glm::vec3 linearVelocity(flecs::entity entity) = 0;
  virtual void setLinearVelocity(flecs::entity entity, glm::vec3 velocity) = 0;
  [[nodiscard]] virtual glm::vec3 angularVelocity(flecs::entity entity) = 0;
  virtual void setAngularVelocity(flecs::entity entity, glm::vec3 velocity) = 0;

  // The events of the last fixed step, sorted by entity ids so their order does not depend on how
  // Jolt's worker threads were scheduled, and replaced by the next step. A pair is reported once
  // when it begins and once when it ends, however many shapes of a compound body are touching; a
  // body that falls asleep keeps its contacts. At most `MaxContactEvents` are kept per step.
  [[nodiscard]] virtual std::span<const ContactEvent> events() const = 0;
  // How many events the last step dropped for being past the cap.
  [[nodiscard]] virtual std::uint32_t droppedEvents() const = 0;
  static constexpr std::size_t MaxContactEvents = 1024;

  [[nodiscard]] virtual std::uint32_t bodyCount() const = 0;
  // Every collider's outline at its entity's world transform, coloured by body type; drawn in
  // edit mode too, where there are no bodies yet.
  virtual void debugLines(std::vector<renderer::DebugLine> &lines) = 0;
};

// Registers the components and the physics systems on `world`, which has to outlive the result;
// mesh colliders are resolved through `assets`, and Jolt's jobs run on `jobs` (ADR-0013), which
// has to outlive it too. A `jobs` with no workers steps on the calling thread. The Jolt
// implementation.
[[nodiscard]] std::unique_ptr<IPhysicsWorld> createPhysicsWorld(world::World &world, assets::AssetDatabase &assets,
                                                                core::JobSystem &jobs, const PhysicsDesc &desc = {});

} // namespace sonnet::physics
