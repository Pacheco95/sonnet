#include <sonnet/physics/PhysicsWorld.h>

#include "ColliderLines.h"
#include "JoltJobSystem.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

// Jolt.h configures every other Jolt header and comes first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sonnet::physics {

namespace {

// ---- Jolt's process-wide state: registered by the first physics world, released by the last ----

int g_joltUsers = 0;
std::unique_ptr<JPH::Factory> g_factory;

// Declared as printf-like so the forwarding to vsnprintf is checked, not flagged.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void joltTrace(const char *format, ...) {
  std::array<char, 1024> buffer{};
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
  va_end(arguments);
  SONNET_LOG_DEBUG("jolt: {}", buffer.data());
}

#ifdef JPH_ENABLE_ASSERTS
bool joltAssertFailed(const char *expression, const char *message, const char *file, JPH::uint line) {
  SONNET_LOG_ERROR("jolt assertion \"{}\" failed at {}:{}: {}", expression, file, line,
                   message != nullptr ? message : "");
  return true; // break into the debugger
}
#endif

void acquireJolt() {
  if (g_joltUsers++ == 0) {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = joltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = joltAssertFailed;)
    g_factory = std::make_unique<JPH::Factory>();
    JPH::Factory::sInstance = g_factory.get();
    JPH::RegisterTypes();
  }
}

void releaseJolt() {
  if (--g_joltUsers == 0) {
    JPH::UnregisterTypes();
    JPH::Factory::sInstance = nullptr;
    g_factory.reset();
  }
}

// ---- Layers: static bodies only meet moving ones ----

namespace layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace layers

namespace broad_phase_layers {
constexpr JPH::BroadPhaseLayer NonMoving{0};
constexpr JPH::BroadPhaseLayer Moving{1};
} // namespace broad_phase_layers

class LayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
  [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
    return 2;
  }
  [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return layer == layers::NonMoving ? broad_phase_layers::NonMoving : broad_phase_layers::Moving;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  [[nodiscard]] const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == broad_phase_layers::NonMoving ? "NonMoving" : "Moving";
  }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
    return layer == layers::Moving || broadPhase == broad_phase_layers::Moving;
  }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a == layers::Moving || b == layers::Moving;
  }
};

// ---- Conversions ----

JPH::Vec3 toJolt(glm::vec3 v) {
  return {v.x, v.y, v.z};
}

JPH::Quat toJolt(glm::quat q) {
  return {q.x, q.y, q.z, q.w};
}

glm::vec3 fromJolt(JPH::Vec3Arg v) {
  return {v.GetX(), v.GetY(), v.GetZ()};
}

glm::quat fromJolt(JPH::QuatArg q) {
  return glm::quat{q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
}

// A world pose without scale, which is what a body has.
struct Pose {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

  bool operator==(const Pose &) const = default;
};

bool nearlyEqual(glm::vec3 a, glm::vec3 b) {
  return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3{1e-4f}));
}

constexpr glm::vec4 StaticColor{0.6f, 0.6f, 0.6f, 1.0f};
constexpr glm::vec4 KinematicColor{0.3f, 0.6f, 1.0f, 1.0f};
constexpr glm::vec4 DynamicColor{0.3f, 1.0f, 0.4f, 1.0f};
constexpr glm::vec4 SleepingColor{0.15f, 0.5f, 0.2f, 1.0f};

constexpr JPH::uint MaxBodyPairs = 65536;
constexpr JPH::uint MaxContactConstraints = 10240;
constexpr std::size_t TempAllocatorBytes = 10u << 20;

bool hasCollider(flecs::entity entity) {
  return entity.has<BoxCollider>() || entity.has<SphereCollider>() || entity.has<CapsuleCollider>() ||
         entity.has<MeshCollider>();
}

std::string describe(flecs::entity entity) {
  const world::Name *name = entity.try_get<world::Name>();
  return name != nullptr ? name->value : std::format("entity {}", entity.id());
}

// The entity whose body shapes are folded into: the entity itself when it has a RigidBody, else
// its nearest ancestor with one, else the entity (a lone collider is a static body of its own).
flecs::entity bodyRootOf(flecs::entity entity) {
  if (entity.has<RigidBody>()) {
    return entity;
  }
  for (flecs::entity parent = entity.parent(); parent; parent = parent.parent()) {
    if (parent.has<RigidBody>()) {
      return parent;
    }
  }
  return entity;
}

// A collider entity below a body root, folded into that root's body, with the pose it had in the
// root's local space when the shape was built (the product of the local transforms between).
struct Contributor {
  flecs::entity_t id{0};
  glm::mat4 relative{1.0f};
};

glm::mat4 relativeMatrix(flecs::entity entity, flecs::entity root) {
  glm::mat4 result{1.0f};
  for (flecs::entity current = entity; current && current != root; current = current.parent()) {
    const world::Transform *local = current.try_get<world::Transform>();
    if (local != nullptr) {
      result = local->matrix() * result;
    }
  }
  return result;
}

bool nearlyEqual(const glm::mat4 &a, const glm::mat4 &b) {
  for (int column = 0; column < 4; ++column) {
    if (!glm::all(glm::lessThanEqual(glm::abs(a[column] - b[column]), glm::vec4{1e-4f}))) {
      return false;
    }
  }
  return true;
}

// What the contact listener needs to know about a body once Jolt hands it only an id: Jolt
// reports removals by id, after the body may be gone.
struct BodyInfo {
  flecs::entity_t entity{0};
  bool sensor{false};
};

// One change in the number of touching shape pairs between two entities, as Jolt reported it.
struct RawContact {
  flecs::entity_t first{0};
  flecs::entity_t second{0};
  std::uint32_t firstBody{0};
  std::uint32_t secondBody{0};
  std::uint32_t firstShape{0};
  std::uint32_t secondShape{0};
  int delta{0};
  bool sensor{false};
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f};
};

bool before(const RawContact &a, const RawContact &b) {
  const auto key = [](const RawContact &c) {
    return std::tie(c.first, c.second, c.firstShape, c.secondShape, c.delta, c.point.x, c.point.y, c.point.z,
                    c.normal.x, c.normal.y, c.normal.z);
  };
  return key(a) < key(b);
}

// Jolt calls this from its worker threads while a step runs (ADR-0013), so it only records. The
// step's records are sorted and turned into events on the stepping thread afterwards.
class ContactRecorder final : public JPH::ContactListener {
public:
  explicit ContactRecorder(const std::unordered_map<std::uint32_t, BodyInfo> &bodies) : m_bodies(bodies) {
  }

  void OnContactAdded(const JPH::Body &first, const JPH::Body &second, const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override {
    const glm::vec3 point = manifold.mRelativeContactPointsOn1.empty()
                                ? fromJolt(first.GetPosition())
                                : fromJolt(JPH::Vec3{manifold.GetWorldSpaceContactPointOn1(0)});
    record(first.GetID(), second.GetID(), manifold.mSubShapeID1.GetValue(), manifold.mSubShapeID2.GetValue(), +1, point,
           fromJolt(manifold.mWorldSpaceNormal));
  }

  void OnContactRemoved(const JPH::SubShapeIDPair &pair) override {
    record(pair.GetBody1ID(), pair.GetBody2ID(), pair.GetSubShapeID1().GetValue(), pair.GetSubShapeID2().GetValue(), -1,
           glm::vec3{0.0f}, glm::vec3{0.0f});
  }

  // Takes what the step recorded.
  std::vector<RawContact> take() {
    const std::scoped_lock lock{m_mutex};
    return std::exchange(m_records, {});
  }

private:
  void record(JPH::BodyID firstId, JPH::BodyID secondId, std::uint32_t firstShape, std::uint32_t secondShape, int delta,
              glm::vec3 point, glm::vec3 normal) {
    const auto first = m_bodies.find(firstId.GetIndexAndSequenceNumber());
    const auto second = m_bodies.find(secondId.GetIndexAndSequenceNumber());
    if (first == m_bodies.end() || second == m_bodies.end()) {
      return;
    }
    RawContact contact{.first = first->second.entity,
                       .second = second->second.entity,
                       .firstBody = firstId.GetIndexAndSequenceNumber(),
                       .secondBody = secondId.GetIndexAndSequenceNumber(),
                       .firstShape = firstShape,
                       .secondShape = secondShape,
                       .delta = delta,
                       .sensor = first->second.sensor || second->second.sensor,
                       .point = point,
                       .normal = normal};
    // The pair is ordered by entity id, not by Jolt's body ids, which follow creation order.
    if (contact.first > contact.second) {
      std::swap(contact.first, contact.second);
      std::swap(contact.firstBody, contact.secondBody);
      std::swap(contact.firstShape, contact.secondShape);
      contact.normal = -contact.normal;
    }
    const std::scoped_lock lock{m_mutex};
    m_records.push_back(contact);
  }

  const std::unordered_map<std::uint32_t, BodyInfo> &m_bodies;
  std::mutex m_mutex;
  std::vector<RawContact> m_records;
};

class JoltPhysicsWorld final : public IPhysicsWorld {
public:
  JoltPhysicsWorld(world::World &world, assets::AssetDatabase &assets, core::JobSystem &jobs, const PhysicsDesc &desc)
      : m_world(world), m_assets(assets), m_recorder(m_bodyInfo) {
    acquireJolt();
    registerComponents(world);
    m_temp = std::make_unique<JPH::TempAllocatorImpl>(TempAllocatorBytes);
    // Jolt's jobs run on the engine's pool, never a second one of its own (ADR-0009, ADR-0013).
    m_jobs = std::make_unique<JoltJobSystem>(jobs, JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
    m_system = std::make_unique<JPH::PhysicsSystem>();
    m_system->Init(desc.maxBodies, 0, MaxBodyPairs, MaxContactConstraints, m_layers, m_objectVsBroadPhase,
                   m_objectPairs);
    m_system->SetGravity(toJolt(desc.gravity));
    m_system->SetContactListener(&m_recorder);

    flecs::world &ecs = world.ecs();
    m_colliders = ecs.query_builder<>("PhysicsColliders")
                      .with<BoxCollider>()
                      .or_()
                      .with<SphereCollider>()
                      .or_()
                      .with<CapsuleCollider>()
                      .or_()
                      .with<MeshCollider>()
                      .without<world::Disabled>()
                      .build();
    m_stepSystem = ecs.system("PhysicsStep").kind(world.phase(world::Phase::FixedUpdate)).run([this](flecs::iter &it) {
      step(it.delta_time());
    });
    world.addToSimulation(m_stepSystem);
    // After the frame's scripts, before the transform system: what is drawn is interpolated.
    m_interpolateSystem =
        ecs.system("PhysicsInterpolate").kind(world.phase(world::Phase::PostUpdate)).run([this](flecs::iter &) {
          interpolate();
        });
    world.addToSimulation(m_interpolateSystem);
    observeChanges<RigidBody>();
    observeChanges<BoxCollider>();
    observeChanges<SphereCollider>();
    observeChanges<CapsuleCollider>();
    observeChanges<MeshCollider>();
    m_observers.push_back(
        ecs.observer("PhysicsDisabled").with<world::Disabled>().event(flecs::OnAdd).each([this](flecs::entity entity) {
          invalidate(entity);
        }));
    m_observers.push_back(ecs.observer("PhysicsTrigger")
                              .with<Trigger>()
                              .event(flecs::OnAdd)
                              .event(flecs::OnRemove)
                              .each([this](flecs::entity entity) { invalidate(entity); }));
    SONNET_LOG_DEBUG("physics ready");
  }

  ~JoltPhysicsWorld() override {
    for (flecs::entity observer : m_observers) {
      observer.destruct();
    }
    m_stepSystem.destruct();
    m_interpolateSystem.destruct();
    m_colliders.destruct();
    while (!m_bodies.empty()) {
      destroyBody(m_bodies.begin()->first);
    }
    m_system.reset();
    m_jobs.reset();
    m_temp.reset();
    releaseJolt();
  }

  JoltPhysicsWorld(const JoltPhysicsWorld &) = delete;
  JoltPhysicsWorld &operator=(const JoltPhysicsWorld &) = delete;

  std::optional<RaycastHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
                                    flecs::entity ignore) override {
    const float length = glm::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f) {
      return std::nullopt;
    }
    const JPH::RRayCast ray{toJolt(origin), toJolt(direction / length * maxDistance)};
    JPH::RayCastResult hit;
    const auto ignored = ignore ? m_bodies.find(ignore.id()) : m_bodies.end();
    const JPH::IgnoreSingleBodyFilter filter{ignored != m_bodies.end() ? ignored->second.id : JPH::BodyID{}};
    if (!m_system->GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) {
      return std::nullopt;
    }
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    const JPH::BodyLockRead lock{m_system->GetBodyLockInterface(), hit.mBodyID};
    if (!lock.Succeeded()) {
      return std::nullopt;
    }
    const JPH::Body &body = lock.GetBody();
    return RaycastHit{.entity = flecs::entity{m_world.ecs(), body.GetUserData()},
                      .point = fromJolt(point),
                      .normal = fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point)),
                      .distance = hit.mFraction * maxDistance};
  }

  void addImpulse(flecs::entity entity, glm::vec3 impulse) override {
    if (const Body *body = dynamicBody(entity)) {
      bodies().AddImpulse(body->id, toJolt(impulse));
    }
  }

  void addForce(flecs::entity entity, glm::vec3 force) override {
    if (const Body *body = dynamicBody(entity)) {
      bodies().AddForce(body->id, toJolt(force));
    }
  }

  glm::vec3 linearVelocity(flecs::entity entity) override {
    const Body *body = bodyOf(entity);
    return body != nullptr ? fromJolt(bodies().GetLinearVelocity(body->id)) : glm::vec3{0.0f};
  }

  void setLinearVelocity(flecs::entity entity, glm::vec3 velocity) override {
    if (const Body *body = dynamicBody(entity)) {
      bodies().SetLinearVelocity(body->id, toJolt(velocity));
    }
  }

  glm::vec3 angularVelocity(flecs::entity entity) override {
    const Body *body = bodyOf(entity);
    return body != nullptr ? fromJolt(bodies().GetAngularVelocity(body->id)) : glm::vec3{0.0f};
  }

  void setAngularVelocity(flecs::entity entity, glm::vec3 velocity) override {
    if (const Body *body = dynamicBody(entity)) {
      bodies().SetAngularVelocity(body->id, toJolt(velocity));
    }
  }

  std::span<const ContactEvent> events() const override {
    return m_events;
  }

  std::uint32_t droppedEvents() const override {
    return m_dropped;
  }

  std::uint32_t bodyCount() const override {
    return static_cast<std::uint32_t>(
        std::ranges::count_if(m_bodies, [](const auto &entry) { return !entry.second.id.IsInvalid(); }));
  }

  void debugLines(std::vector<renderer::DebugLine> &lines) override {
    SONNET_ZONE();
    m_colliders.each([&](flecs::entity entity) {
      const world::WorldTransform *transform = entity.try_get<world::WorldTransform>();
      if (transform == nullptr) {
        return;
      }
      const RigidBody *rigidBody = entity.try_get<RigidBody>();
      const BodyType type = rigidBody != nullptr ? rigidBody->type : BodyType::Static;
      glm::vec4 color = type == BodyType::Static      ? StaticColor
                        : type == BodyType::Kinematic ? KinematicColor
                                                      : DynamicColor;
      if (const auto it = m_bodies.find(entity.id()); type == BodyType::Dynamic && it != m_bodies.end() &&
                                                      !it->second.id.IsInvalid() && !bodies().IsActive(it->second.id)) {
        color = SleepingColor;
      }
      appendColliderLines(entity, transform->matrix, colliderMesh(entity), color, lines);
    });
  }

private:
  struct Body {
    JPH::BodyID id; // invalid when the shape could not be built; not retried until a change
    BodyType type{BodyType::Static};
    glm::vec3 scale{1.0f}; // the world scale the shape was built with
    Pose pose;             // static and kinematic: the pose last pushed to Jolt
    // Dynamic: the world poses after the last two steps, and the local transform physics last
    // wrote, which tells an outside write apart.
    Pose previous;
    Pose current;
    world::Transform written;
    // Colliders of descendants folded into this body, by id, with the pose they were built at.
    std::vector<Contributor> contributors;
  };

  // The state of a pair of entities between steps: how many of their shape pairs touch.
  struct PairState {
    int count{0};
    bool sensor{false};
    bool dormant{false};       // not touching only because a body fell asleep
    std::uint64_t lastStep{0}; // the last step that reported this pair
    std::uint32_t firstBody{0};
    std::uint32_t secondBody{0};
  };
  struct PairKey {
    flecs::entity_t first{0};
    flecs::entity_t second{0};
    bool operator==(const PairKey &) const = default;
  };
  struct PairHash {
    std::size_t operator()(const PairKey &key) const noexcept {
      return std::hash<flecs::entity_t>{}(key.first) * 31 + std::hash<flecs::entity_t>{}(key.second);
    }
  };

  [[nodiscard]] JPH::BodyInterface &bodies() const {
    return m_system->GetBodyInterfaceNoLock();
  }

  template <typename Component> void observeChanges() {
    m_observers.push_back(m_world.ecs()
                              .observer<const Component>()
                              .event(flecs::OnSet)
                              .event(flecs::OnRemove)
                              .each([this](flecs::entity entity, const Component &) { invalidate(entity); }));
  }

  // A changed collider, body or tag drops the body it belongs to, whose shape the next step
  // builds again: the entity's own, and the compound body it is folded into.
  void invalidate(flecs::entity entity) {
    destroyBody(entity.id());
    if (const flecs::entity root = bodyRootOf(entity); root != entity) {
      destroyBody(root.id());
    }
  }

  // The colliders below `root` that are folded into its body: descendants with a collider and
  // no RigidBody, not behind a disabled entity or another body's root.
  void collectContributors(flecs::entity root, flecs::entity parent, std::vector<flecs::entity> &out) const {
    for (const flecs::entity child : m_world.children(parent)) {
      if (child.has<world::Disabled>() || child.has<RigidBody>() || child.has(flecs::Prefab)) {
        continue;
      }
      if (hasCollider(child)) {
        out.push_back(child);
      }
      collectContributors(root, child, out);
    }
  }

  // Whether no entity between `entity` and `root` is disabled.
  [[nodiscard]] static bool pathEnabled(flecs::entity entity, flecs::entity root) {
    for (flecs::entity current = entity; current && current != root; current = current.parent()) {
      if (current.has<world::Disabled>()) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool hasShape(flecs::entity root) const {
    if (hasCollider(root)) {
      return true;
    }
    std::vector<flecs::entity> contributors;
    collectContributors(root, root, contributors);
    return !contributors.empty();
  }

  // Whether the folded-in colliders are still the ones the body was built from, where they were.
  [[nodiscard]] bool compoundIntact(flecs::entity root, const Body &body) const {
    for (const Contributor &contributor : body.contributors) {
      if (!m_world.ecs().is_alive(contributor.id)) {
        return false;
      }
      const flecs::entity entity{m_world.ecs(), contributor.id};
      if (!hasCollider(entity) || bodyRootOf(entity) != root || !pathEnabled(entity, root) ||
          !nearlyEqual(relativeMatrix(entity, root), contributor.relative)) {
        return false;
      }
    }
    return true;
  }

  // The body an entity has, creating it when the entity should have one and does not yet. A
  // collider folded into a compound body answers with that body.
  const Body *bodyOf(flecs::entity entity) {
    if (!entity || !entity.is_alive()) {
      return nullptr;
    }
    entity = bodyRootOf(entity);
    auto it = m_bodies.find(entity.id());
    if (it == m_bodies.end()) {
      if (entity.has<world::Disabled>() || entity.has(flecs::Prefab) || !hasShape(entity)) {
        return nullptr;
      }
      createBody(entity);
      it = m_bodies.find(entity.id());
    }
    return it != m_bodies.end() && !it->second.id.IsInvalid() ? &it->second : nullptr;
  }

  const Body *dynamicBody(flecs::entity entity) {
    const Body *body = bodyOf(entity);
    return body != nullptr && body->type == BodyType::Dynamic ? body : nullptr;
  }

  const renderer::MeshData *colliderMesh(flecs::entity entity) {
    const MeshCollider *collider = entity.try_get<MeshCollider>();
    if (collider == nullptr) {
      return nullptr;
    }
    core::Uuid mesh = collider->mesh;
    if (mesh.isNil()) {
      const world::MeshRenderer *renderer = entity.try_get<world::MeshRenderer>();
      mesh = renderer != nullptr ? renderer->mesh : core::Uuid{};
    }
    return mesh.isNil() ? nullptr : m_assets.meshData(mesh);
  }

  struct Part {
    JPH::ShapeRefC shape;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  };

  // The shapes of `source`'s own colliders. At `root` they sit at their offsets in the root's
  // local space; for a folded-in descendant they are carried into it by `relative`, the
  // descendant's pose there, scale included.
  void addParts(flecs::entity source, BodyType type, const glm::mat4 *relative, std::vector<Part> &parts,
                std::string &error) {
    std::vector<std::pair<JPH::ShapeRefC, glm::vec3>> own;
    const auto add = [&](const JPH::ShapeSettings::ShapeResult &result, glm::vec3 offset) {
      if (result.HasError()) {
        error = result.GetError().c_str();
      } else {
        own.emplace_back(result.Get(), offset);
      }
    };
    if (const BoxCollider *box = source.try_get<BoxCollider>()) {
      const glm::vec3 half = glm::max(box->halfExtents, glm::vec3{0.001f});
      // Jolt rounds a box's edges by the convex radius, which has to fit inside it.
      const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({half.x, half.y, half.z}));
      add(JPH::BoxShapeSettings{toJolt(half), convexRadius}.Create(), box->offset);
    }
    if (const SphereCollider *sphere = source.try_get<SphereCollider>()) {
      add(JPH::SphereShapeSettings{std::max(sphere->radius, 0.001f)}.Create(), sphere->offset);
    }
    if (const CapsuleCollider *capsule = source.try_get<CapsuleCollider>()) {
      const float radius = std::max(capsule->radius, 0.001f);
      if (capsule->halfHeight > 0.0f) {
        add(JPH::CapsuleShapeSettings{capsule->halfHeight, radius}.Create(), capsule->offset);
      } else {
        add(JPH::SphereShapeSettings{radius}.Create(), capsule->offset);
      }
    }
    if (source.has<MeshCollider>()) {
      const renderer::MeshData *mesh = colliderMesh(source);
      if (mesh == nullptr || mesh->indices.empty()) {
        error = "the mesh collider has no mesh";
      } else if (type == BodyType::Dynamic) {
        // Jolt simulates triangle meshes only as static or kinematic bodies.
        JPH::Array<JPH::Vec3> points;
        points.reserve(mesh->vertices.size());
        for (const renderer::Vertex &vertex : mesh->vertices) {
          points.push_back(toJolt(vertex.position));
        }
        add(JPH::ConvexHullShapeSettings{points}.Create(), glm::vec3{0.0f});
      } else {
        JPH::VertexList vertices;
        vertices.reserve(mesh->vertices.size());
        for (const renderer::Vertex &vertex : mesh->vertices) {
          vertices.emplace_back(vertex.position.x, vertex.position.y, vertex.position.z);
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(mesh->indices.size() / 3);
        for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
          triangles.emplace_back(mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2], 0);
        }
        add(JPH::MeshShapeSettings{std::move(vertices), std::move(triangles)}.Create(), glm::vec3{0.0f});
      }
    }
    if (relative == nullptr) {
      for (auto &[shape, offset] : own) {
        parts.push_back({std::move(shape), offset, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}});
      }
      return;
    }
    const world::Transform pose = world::Transform::fromMatrix(*relative);
    for (auto &[shape, offset] : own) {
      JPH::ShapeRefC inner = std::move(shape);
      if (offset != glm::vec3{0.0f}) {
        inner = JPH::RotatedTranslatedShapeSettings{toJolt(offset), JPH::Quat::sIdentity(), inner}.Create().Get();
      }
      if (!nearlyEqual(pose.scale, glm::vec3{1.0f})) {
        const JPH::Shape::ShapeResult scaled = inner->ScaleShape(toJolt(pose.scale));
        if (scaled.HasError()) {
          error = scaled.GetError().c_str();
          return;
        }
        inner = scaled.Get();
      }
      parts.push_back({std::move(inner), pose.position, pose.rotation});
    }
  }

  // The body's colliders as one shape in the root's local space, scaled; null with the reason
  // logged. `contributors` receives the descendants whose colliders were folded in.
  JPH::ShapeRefC buildShape(flecs::entity entity, BodyType type, glm::vec3 scale,
                            std::vector<Contributor> &contributors) {
    std::vector<Part> parts;
    std::string error;
    addParts(entity, type, nullptr, parts, error);
    std::vector<flecs::entity> below;
    collectContributors(entity, entity, below);
    for (const flecs::entity child : below) {
      if (bodyRootOf(child) != entity) {
        continue; // behind another body's root
      }
      const glm::mat4 relative = relativeMatrix(child, entity);
      addParts(child, type, &relative, parts, error);
      contributors.push_back({child.id(), relative});
    }
    std::ranges::sort(contributors, {}, &Contributor::id);
    if (!error.empty() || parts.empty()) {
      SONNET_LOG_ERROR("{}: no physics body: {}", describe(entity), error.empty() ? "no collider" : error);
      return {};
    }

    JPH::ShapeRefC shape;
    if (parts.size() == 1) {
      shape = parts.front().shape;
      if (parts.front().position != glm::vec3{0.0f} || parts.front().rotation != glm::quat{1.0f, 0.0f, 0.0f, 0.0f}) {
        shape =
            JPH::RotatedTranslatedShapeSettings{toJolt(parts.front().position), toJolt(parts.front().rotation), shape}
                .Create()
                .Get();
      }
    } else {
      JPH::StaticCompoundShapeSettings compound;
      for (const Part &part : parts) {
        compound.AddShape(toJolt(part.position), toJolt(part.rotation), part.shape);
      }
      const JPH::ShapeSettings::ShapeResult result = compound.Create();
      if (result.HasError()) {
        SONNET_LOG_ERROR("{}: no physics body: {}", describe(entity), result.GetError().c_str());
        return {};
      }
      shape = result.Get();
    }
    if (!nearlyEqual(scale, glm::vec3{1.0f})) {
      const JPH::Shape::ShapeResult scaled = shape->ScaleShape(toJolt(scale));
      if (scaled.HasError()) {
        SONNET_LOG_ERROR("{}: no physics body: {}", describe(entity), scaled.GetError().c_str());
        return {};
      }
      shape = scaled.Get();
    }
    return shape;
  }

  void createBody(flecs::entity entity) {
    const RigidBody *rigidBody = entity.try_get<RigidBody>();
    const RigidBody settings = rigidBody != nullptr ? *rigidBody : RigidBody{.type = BodyType::Static};
    const world::Transform placed = world::Transform::fromMatrix(world::World::worldMatrix(entity));
    Body &body = m_bodies[entity.id()];
    body.type = settings.type;
    body.scale = placed.scale;
    body.pose = {placed.position, placed.rotation};
    body.previous = body.pose;
    body.current = body.pose;
    if (const world::Transform *local = entity.try_get<world::Transform>()) {
      body.written = *local;
    }

    const JPH::ShapeRefC shape = buildShape(entity, settings.type, placed.scale, body.contributors);
    if (shape == nullptr) {
      return;
    }
    const JPH::EMotionType motion = settings.type == BodyType::Static      ? JPH::EMotionType::Static
                                    : settings.type == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                                           : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings creation{shape, toJolt(placed.position), toJolt(placed.rotation), motion,
                                       settings.type == BodyType::Static ? layers::NonMoving : layers::Moving};
    creation.mFriction = settings.friction;
    creation.mRestitution = settings.restitution;
    creation.mLinearDamping = settings.linearDamping;
    creation.mAngularDamping = settings.angularDamping;
    creation.mGravityFactor = settings.gravityScale;
    creation.mUserData = entity.id();
    // A sensor reports overlaps and pushes nothing; it sees dynamic and kinematic bodies, and a
    // static one only while they are awake.
    creation.mIsSensor = entity.has<Trigger>();
    if (settings.type == BodyType::Dynamic && settings.mass > 0.0f) {
      creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      creation.mMassPropertiesOverride.mMass = settings.mass;
    }
    body.id = bodies().CreateAndAddBody(creation, settings.type == BodyType::Static ? JPH::EActivation::DontActivate
                                                                                    : JPH::EActivation::Activate);
    if (body.id.IsInvalid()) {
      SONNET_LOG_ERROR("{}: no physics body: the world's {} bodies are in use", describe(entity),
                       m_system->GetMaxBodies());
      return;
    }
    m_bodyInfo[body.id.GetIndexAndSequenceNumber()] = {entity.id(), creation.mIsSensor};
  }

  void destroyBody(flecs::entity_t entity) {
    const auto it = m_bodies.find(entity);
    if (it == m_bodies.end()) {
      return;
    }
    if (!it->second.id.IsInvalid()) {
      // Jolt reports the contacts of a removed body in the next step, by id; the entry stays
      // until then.
      m_retired.push_back(it->second.id.GetIndexAndSequenceNumber());
      bodies().RemoveBody(it->second.id);
      bodies().DestroyBody(it->second.id);
    }
    m_bodies.erase(it);
  }

  // Dynamic bodies: the local transform that puts the entity at `pose` under its parent, keeping
  // the local scale.
  static world::Transform localTransform(flecs::entity entity, const world::Transform &local, const Pose &pose) {
    world::Transform result = local;
    const flecs::entity parent = entity.parent();
    if (!parent) {
      result.position = pose.position;
      result.rotation = pose.rotation;
      return result;
    }
    const glm::mat4 matrix = glm::inverse(world::World::worldMatrix(parent)) *
                             (glm::translate(glm::mat4{1.0f}, pose.position) * glm::mat4_cast(pose.rotation));
    const world::Transform decomposed = world::Transform::fromMatrix(matrix);
    result.position = decomposed.position;
    result.rotation = decomposed.rotation;
    return result;
  }

  void writePose(flecs::entity entity, Body &body, const Pose &pose) {
    world::Transform *local = entity.try_get_mut<world::Transform>();
    if (local == nullptr) {
      return;
    }
    *local = localTransform(entity, *local, pose);
    body.written = *local;
  }

  void step(float dt) {
    SONNET_ZONE();
    flecs::world &ecs = m_world.ecs();
    // Bodies of entities that are gone, disabled or no longer have a collider.
    std::vector<flecs::entity_t> stale;
    for (const auto &[id, body] : m_bodies) {
      // Ids carry a generation: a deleted entity's id is not alive even when its slot is reused.
      const flecs::entity entity{ecs, id};
      if (!ecs.is_alive(id) || entity.has<world::Disabled>() || bodyRootOf(entity) != entity || !hasShape(entity) ||
          !compoundIntact(entity, body)) {
        stale.push_back(id);
      }
    }
    for (const flecs::entity_t id : stale) {
      destroyBody(id);
    }
    // Bodies for the colliders that have none yet.
    m_colliders.each([&](flecs::entity entity) {
      const flecs::entity root = bodyRootOf(entity);
      if (root == entity) {
        if (!m_bodies.contains(entity.id())) {
          createBody(entity);
        }
        return;
      }
      // A collider below a body: part of that body's shape. The body is built again when it
      // does not have this one yet, unless the shape could not be built at all.
      if (root.has<world::Disabled>() || root.has(flecs::Prefab) || !pathEnabled(entity, root)) {
        return;
      }
      const auto found = m_bodies.find(root.id());
      if (found == m_bodies.end()) {
        createBody(root);
      } else if (!found->second.id.IsInvalid() &&
                 !std::ranges::binary_search(found->second.contributors, entity.id(), {}, &Contributor::id)) {
        destroyBody(root.id());
        createBody(root);
      }
    });

    // Poses in: static and kinematic bodies follow their entities, dynamic ones only when
    // something else moved them. A changed world scale rebuilds the shape.
    std::vector<flecs::entity_t> rescaled;
    for (auto &[id, body] : m_bodies) {
      if (body.id.IsInvalid()) {
        continue;
      }
      const flecs::entity entity = flecs::entity{ecs, id};
      const world::Transform placed = world::Transform::fromMatrix(world::World::worldMatrix(entity));
      if (!nearlyEqual(placed.scale, body.scale)) {
        rescaled.push_back(id);
        continue;
      }
      const Pose pose{placed.position, placed.rotation};
      switch (body.type) {
      case BodyType::Static:
        if (pose != body.pose) {
          bodies().SetPositionAndRotation(body.id, toJolt(pose.position), toJolt(pose.rotation),
                                          JPH::EActivation::DontActivate);
          body.pose = pose;
        }
        break;
      case BodyType::Kinematic:
        bodies().MoveKinematic(body.id, toJolt(pose.position), toJolt(pose.rotation), dt);
        body.pose = pose;
        break;
      case BodyType::Dynamic:
        if (const world::Transform *local = entity.try_get<world::Transform>();
            local != nullptr &&
            (local->position != body.written.position || local->rotation != body.written.rotation)) {
          bodies().SetPositionAndRotation(body.id, toJolt(pose.position), toJolt(pose.rotation),
                                          JPH::EActivation::Activate);
          body.previous = pose;
          body.current = pose;
          body.written = *local;
        }
        break;
      }
    }
    for (const flecs::entity_t id : rescaled) {
      destroyBody(id);
      createBody(flecs::entity{ecs, id});
    }

    const JPH::EPhysicsUpdateError errors = m_system->Update(dt, 1, m_temp.get(), m_jobs.get());
    if (errors != JPH::EPhysicsUpdateError::None) {
      SONNET_LOG_WARN("physics step: Jolt ran out of room (error mask {:#x}); raise the limits in PhysicsDesc",
                      static_cast<std::uint32_t>(errors));
    }

    collectEvents();
    for (const std::uint32_t key : m_retired) {
      m_bodyInfo.erase(key);
    }
    m_retired.clear();

    // Poses out: every dynamic body's step result, which later fixed steps and scripts read.
    for (auto &[id, body] : m_bodies) {
      if (body.id.IsInvalid() || body.type != BodyType::Dynamic) {
        continue;
      }
      body.previous = body.current;
      if (bodies().IsActive(body.id)) {
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies().GetPositionAndRotation(body.id, position, rotation);
        body.current = {fromJolt(position), fromJolt(rotation)};
      }
      writePose(flecs::entity{ecs, id}, body, body.current);
    }
  }

  // Whether a body that is not static is added and not active: it fell asleep, and Jolt reports
  // the contacts of a sleeping body as removed.
  [[nodiscard]] bool sleeping(std::uint32_t key) const {
    const JPH::BodyLockRead lock{m_system->GetBodyLockInterface(), JPH::BodyID{key}};
    if (!lock.Succeeded()) {
      return false;
    }
    const JPH::Body &body = lock.GetBody();
    return body.IsInBroadPhase() && !body.IsStatic() && !body.IsActive();
  }

  void emit(const PairKey &pair, const PairState &state, bool begin, glm::vec3 point, glm::vec3 normal) {
    flecs::world &ecs = m_world.ecs();
    const ContactEventKind kind = state.sensor
                                      ? (begin ? ContactEventKind::TriggerEnter : ContactEventKind::TriggerExit)
                                      : (begin ? ContactEventKind::ContactBegin : ContactEventKind::ContactEnd);
    m_events.push_back({kind, flecs::entity{ecs, pair.first}, flecs::entity{ecs, pair.second}, point, normal});
  }

  // Turns what the listener recorded during the step into events, on the stepping thread. The
  // records arrive in whatever order the workers ran, so they are sorted first, and the events
  // come from how many shape pairs touched before and after the step rather than from each
  // record: a compound body touching with five shapes is one contact, and a pair that ends and
  // begins again within the step is neither.
  void collectEvents() {
    SONNET_ZONE();
    ++m_stepIndex;
    m_events.clear();
    m_dropped = 0;
    std::vector<RawContact> raw = m_recorder.take();
    std::ranges::sort(raw, before);
    for (std::size_t i = 0; i < raw.size();) {
      std::size_t j = i;
      int net = 0;
      const RawContact *firstAdd = nullptr;
      for (; j < raw.size() && raw[j].first == raw[i].first && raw[j].second == raw[i].second; ++j) {
        net += raw[j].delta;
        firstAdd = firstAdd == nullptr && raw[j].delta > 0 ? &raw[j] : firstAdd;
      }
      const PairKey key{raw[i].first, raw[i].second};
      PairState &state = m_pairs[key];
      const int was = state.count;
      state.count = std::max(0, was + net);
      state.sensor = raw[i].sensor;
      state.firstBody = raw[i].firstBody;
      state.secondBody = raw[i].secondBody;
      state.lastStep = m_stepIndex;
      if (was == 0 && state.count > 0) {
        // A pair that only slept comes back without an event.
        if (!state.dormant && firstAdd != nullptr) {
          emit(key, state, true, firstAdd->point, firstAdd->normal);
        }
        state.dormant = false;
      } else if (was > 0 && state.count == 0) {
        if (sleeping(state.firstBody) || sleeping(state.secondBody)) {
          state.dormant = true;
        } else {
          emit(key, state, false, glm::vec3{0.0f}, glm::vec3{0.0f});
          m_pairs.erase(key);
        }
      } else if (state.count == 0 && !state.dormant) {
        m_pairs.erase(key);
      }
      i = j;
    }
    // A sleeping pair ends when neither body sleeps any more and no contact came back, or when
    // a body is gone.
    for (auto it = m_pairs.begin(); it != m_pairs.end();) {
      const PairState &state = it->second;
      if (state.dormant && state.lastStep != m_stepIndex && !sleeping(state.firstBody) && !sleeping(state.secondBody)) {
        emit(it->first, state, false, glm::vec3{0.0f}, glm::vec3{0.0f});
        it = m_pairs.erase(it);
      } else {
        ++it;
      }
    }
    std::ranges::sort(m_events, [](const ContactEvent &a, const ContactEvent &b) {
      return std::tuple{a.first.id(), a.second.id(), a.kind} < std::tuple{b.first.id(), b.second.id(), b.kind};
    });
    if (m_events.size() > MaxContactEvents) {
      m_dropped = static_cast<std::uint32_t>(m_events.size() - MaxContactEvents);
      m_events.resize(MaxContactEvents);
      if (!m_overflowLogged) {
        m_overflowLogged = true;
        SONNET_LOG_WARN("physics step: more than {} contact events in one step, the rest are dropped",
                        MaxContactEvents);
      }
    }
  }

  void interpolate() {
    SONNET_ZONE();
    const float alpha = m_world.fixedAlpha();
    flecs::world &ecs = m_world.ecs();
    for (auto &[id, body] : m_bodies) {
      if (body.id.IsInvalid() || body.type != BodyType::Dynamic || body.previous == body.current) {
        continue;
      }
      const flecs::entity entity = flecs::entity{ecs, id};
      const world::Transform *local = entity.try_get<world::Transform>();
      // A script moved it this frame: the next step teleports the body there.
      if (local == nullptr || local->position != body.written.position || local->rotation != body.written.rotation) {
        continue;
      }
      writePose(entity, body,
                {glm::mix(body.previous.position, body.current.position, alpha),
                 glm::slerp(body.previous.rotation, body.current.rotation, alpha)});
    }
  }

  world::World &m_world;
  assets::AssetDatabase &m_assets;
  LayerInterface m_layers;
  ObjectVsBroadPhaseFilter m_objectVsBroadPhase;
  ObjectPairFilter m_objectPairs;
  std::unique_ptr<JPH::TempAllocatorImpl> m_temp;
  std::unique_ptr<JoltJobSystem> m_jobs;
  std::unique_ptr<JPH::PhysicsSystem> m_system;
  std::unordered_map<flecs::entity_t, Body> m_bodies;
  std::unordered_map<std::uint32_t, BodyInfo> m_bodyInfo; // by Jolt body id; read by the listener
  std::vector<std::uint32_t> m_retired;                   // ids of bodies destroyed since the last step
  ContactRecorder m_recorder;
  std::unordered_map<PairKey, PairState, PairHash> m_pairs;
  std::vector<ContactEvent> m_events;
  std::uint32_t m_dropped{0};
  std::uint64_t m_stepIndex{0};
  bool m_overflowLogged{false};
  flecs::query<> m_colliders;
  flecs::system m_stepSystem;
  flecs::system m_interpolateSystem;
  std::vector<flecs::entity> m_observers;
};

} // namespace

std::unique_ptr<IPhysicsWorld> createPhysicsWorld(world::World &world, assets::AssetDatabase &assets,
                                                  core::JobSystem &jobs, const PhysicsDesc &desc) {
  return std::make_unique<JoltPhysicsWorld>(world, assets, jobs, desc);
}

} // namespace sonnet::physics
