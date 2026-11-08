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

// What a sphere cast hit first. `point` is on the hit body's surface, `normal` the unit direction
// from it towards the sphere's centre at the hit (the push that would separate them), and
// `distance` how far the centre travelled along the normalised direction, 0 when the sphere
// started out overlapping the body.
struct SphereCastHit {
  flecs::entity entity;
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f, 1.0f, 0.0f};
  float distance{0.0f};
};

// What the last fixed step left of a character (ADR-0038). `velocity` is the world velocity the
// step moved it with, its support's included. `grounded` is standing on walkable ground,
// `sliding` on ground too steep to stand on; while either holds, `groundNormal` is the ground's
// normal, `support` the entity whose body is stood on (which may since have gone) and
// `supportVelocity` that body's velocity at the contact. In the air they are zero and none.
struct CharacterState {
  glm::vec3 velocity{0.0f};
  bool grounded{false};
  bool sliding{false};
  glm::vec3 groundNormal{0.0f};
  flecs::entity support;
  glm::vec3 supportVelocity{0.0f};
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
// a body is ignored, and one that should have a body gets it at once. Entities with a
// CharacterController are characters, stepped before the bodies and seen by queries and triggers
// through an inner body that reports the controller's entity.
class IPhysicsWorld {
public:
  virtual ~IPhysicsWorld() = default;

  // The nearest body along the ray within maxDistance, skipping `ignore`'s body (or character), which a ray cast
  // from inside an entity's own collider would otherwise hit first; `direction` need not be
  // normalised.
  [[nodiscard]] virtual std::optional<RaycastHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
                                                          flecs::entity ignore = {}) = 0;
  // The first solid body a sphere of `radius` touches while its centre moves from `origin` along
  // `direction` (which need not be normalised) for up to `maxDistance`. Triggers are not hit, and
  // neither is the body of any entity in `ignore` (a collider folded into a compound body ignores
  // that whole body). A sphere that starts out overlapping a body hits it at distance 0, the
  // deepest overlap first, with the normal that pushes the sphere out; a zero direction or a zero
  // maxDistance tests only that overlap. A non-positive radius, a negative maxDistance or any
  // value that is not finite hits nothing.
  [[nodiscard]] virtual std::optional<SphereCastHit> sphereCast(glm::vec3 origin, float radius, glm::vec3 direction,
                                                                float maxDistance,
                                                                std::span<const flecs::entity> ignore = {}) = 0;
  // Dynamic bodies only; an impulse changes the velocity at once, a force acts over the next step.
  virtual void addImpulse(flecs::entity entity, glm::vec3 impulse) = 0;
  virtual void addForce(flecs::entity entity, glm::vec3 force) = 0;
  [[nodiscard]] virtual glm::vec3 linearVelocity(flecs::entity entity) = 0;
  virtual void setLinearVelocity(flecs::entity entity, glm::vec3 velocity) = 0;
  [[nodiscard]] virtual glm::vec3 angularVelocity(flecs::entity entity) = 0;
  virtual void setAngularVelocity(flecs::entity entity, glm::vec3 velocity) = 0;

  // Characters (ADR-0038). Each acts on an enabled entity whose CharacterController is valid, in
  // play mode, creating its character on the spot when the step has not yet; on anything else
  // the commands return false and do nothing and `character` returns nothing.
  // The horizontal velocity the character walks with, in world m/s, held until changed. Its y must
  // be 0 (to 1e-4) and every component finite; otherwise it returns false and keeps the old one.
  virtual bool setCharacterMove(flecs::entity entity, glm::vec3 horizontalVelocity) = 0;
  // A one-shot jump consumed by the next fixed step whatever the ground: the vertical velocity
  // becomes the support's (when grounded) plus `verticalSpeed`, which must be finite and > 0.
  // Requests before a step do not add up; the last one wins.
  virtual bool jumpCharacter(flecs::entity entity, float verticalSpeed) = 0;
  // Puts the character's origin at a world position, facing `yaw` radians about +Y, and writes
  // the entity's local Transform at once. Clears the velocity, the move, a pending jump, the
  // inherited support velocity, the support and the interpolation.
  virtual bool teleportCharacter(flecs::entity entity, glm::vec3 position, float yaw) = 0;
  [[nodiscard]] virtual std::optional<CharacterState> character(flecs::entity entity) = 0;

  // The events of the last fixed step, sorted by entity ids so their order does not depend on how
  // Jolt's worker threads were scheduled, and replaced by the next step. A pair is reported once
  // when it begins and once when it ends, however many shapes of a compound body are touching; a
  // body that falls asleep keeps its contacts. At most `MaxContactEvents` are kept per step.
  [[nodiscard]] virtual std::span<const ContactEvent> events() const = 0;
  // How many events the last step dropped for being past the cap.
  [[nodiscard]] virtual std::uint32_t droppedEvents() const = 0;
  static constexpr std::size_t MaxContactEvents = 1024;

  // The rigid bodies that exist; characters and their inner bodies are not counted.
  [[nodiscard]] virtual std::uint32_t bodyCount() const = 0;
  // Every collider's outline at its entity's world transform, coloured by body type, and every
  // character controller's capsule in a colour of its own; drawn in edit mode too, where there
  // are no bodies yet.
  virtual void debugLines(std::vector<renderer::DebugLine> &lines) = 0;
};

// Registers the components and the physics systems on `world`, which has to outlive the result;
// mesh colliders are resolved through `assets`, and Jolt's jobs run on `jobs` (ADR-0013), which
// has to outlive it too. A `jobs` with no workers steps on the calling thread. The Jolt
// implementation.
[[nodiscard]] std::unique_ptr<IPhysicsWorld> createPhysicsWorld(world::World &world, assets::AssetDatabase &assets,
                                                                core::JobSystem &jobs, const PhysicsDesc &desc = {});

} // namespace sonnet::physics
