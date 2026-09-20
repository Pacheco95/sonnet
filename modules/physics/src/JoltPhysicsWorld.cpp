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
#include <string>
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

namespace Layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving{0};
constexpr JPH::BroadPhaseLayer Moving{1};
} // namespace BroadPhaseLayers

class LayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
  [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
    return 2;
  }
  [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  [[nodiscard]] const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == BroadPhaseLayers::NonMoving ? "NonMoving" : "Moving";
  }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
    return layer == Layers::Moving || broadPhase == BroadPhaseLayers::Moving;
  }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a == Layers::Moving || b == Layers::Moving;
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

class JoltPhysicsWorld final : public IPhysicsWorld {
public:
  JoltPhysicsWorld(world::World &world, assets::AssetDatabase &assets, core::JobSystem &jobs, const PhysicsDesc &desc)
      : m_world(world), m_assets(assets) {
    acquireJolt();
    registerComponents(world);
    m_temp = std::make_unique<JPH::TempAllocatorImpl>(TempAllocatorBytes);
    // Jolt's jobs run on the engine's pool, never a second one of its own (ADR-0009, ADR-0013).
    m_jobs = std::make_unique<JoltJobSystem>(jobs, JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
    m_system = std::make_unique<JPH::PhysicsSystem>();
    m_system->Init(desc.maxBodies, 0, MaxBodyPairs, MaxContactConstraints, m_layers, m_objectVsBroadPhase,
                   m_objectPairs);
    m_system->SetGravity(toJolt(desc.gravity));

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
          destroyBody(entity.id());
        }));
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
  };

  [[nodiscard]] JPH::BodyInterface &bodies() const {
    return m_system->GetBodyInterfaceNoLock();
  }

  template <typename Component> void observeChanges() {
    m_observers.push_back(m_world.ecs()
                              .observer<const Component>()
                              .event(flecs::OnSet)
                              .event(flecs::OnRemove)
                              .each([this](flecs::entity entity, const Component &) { destroyBody(entity.id()); }));
  }

  // The body an entity has, creating it when the entity should have one and does not yet.
  const Body *bodyOf(flecs::entity entity) {
    if (!entity || !entity.is_alive()) {
      return nullptr;
    }
    auto it = m_bodies.find(entity.id());
    if (it == m_bodies.end()) {
      if (!hasCollider(entity) || entity.has<world::Disabled>() || entity.has(flecs::Prefab)) {
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

  // The entity's colliders as one shape in its local space, scaled; null with the reason logged.
  JPH::ShapeRefC buildShape(flecs::entity entity, BodyType type, glm::vec3 scale) {
    std::vector<std::pair<JPH::ShapeRefC, glm::vec3>> parts;
    std::string error;
    const auto add = [&](const JPH::ShapeSettings::ShapeResult &result, glm::vec3 offset) {
      if (result.HasError()) {
        error = result.GetError().c_str();
      } else {
        parts.emplace_back(result.Get(), offset);
      }
    };
    if (const BoxCollider *box = entity.try_get<BoxCollider>()) {
      const glm::vec3 half = glm::max(box->halfExtents, glm::vec3{0.001f});
      // Jolt rounds a box's edges by the convex radius, which has to fit inside it.
      const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({half.x, half.y, half.z}));
      add(JPH::BoxShapeSettings{toJolt(half), convexRadius}.Create(), box->offset);
    }
    if (const SphereCollider *sphere = entity.try_get<SphereCollider>()) {
      add(JPH::SphereShapeSettings{std::max(sphere->radius, 0.001f)}.Create(), sphere->offset);
    }
    if (const CapsuleCollider *capsule = entity.try_get<CapsuleCollider>()) {
      const float radius = std::max(capsule->radius, 0.001f);
      if (capsule->halfHeight > 0.0f) {
        add(JPH::CapsuleShapeSettings{capsule->halfHeight, radius}.Create(), capsule->offset);
      } else {
        add(JPH::SphereShapeSettings{radius}.Create(), capsule->offset);
      }
    }
    if (entity.has<MeshCollider>()) {
      const renderer::MeshData *mesh = colliderMesh(entity);
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
    if (!error.empty() || parts.empty()) {
      SONNET_LOG_ERROR("{}: no physics body: {}", describe(entity), error.empty() ? "no collider" : error);
      return {};
    }

    JPH::ShapeRefC shape;
    if (parts.size() == 1) {
      shape = parts.front().first;
      if (parts.front().second != glm::vec3{0.0f}) {
        shape = JPH::RotatedTranslatedShapeSettings{toJolt(parts.front().second), JPH::Quat::sIdentity(), shape}
                    .Create()
                    .Get();
      }
    } else {
      JPH::StaticCompoundShapeSettings compound;
      for (const auto &[part, offset] : parts) {
        compound.AddShape(toJolt(offset), JPH::Quat::sIdentity(), part);
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

    const JPH::ShapeRefC shape = buildShape(entity, settings.type, placed.scale);
    if (shape == nullptr) {
      return;
    }
    const JPH::EMotionType motion = settings.type == BodyType::Static      ? JPH::EMotionType::Static
                                    : settings.type == BodyType::Kinematic ? JPH::EMotionType::Kinematic
                                                                           : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings creation{shape, toJolt(placed.position), toJolt(placed.rotation), motion,
                                       settings.type == BodyType::Static ? Layers::NonMoving : Layers::Moving};
    creation.mFriction = settings.friction;
    creation.mRestitution = settings.restitution;
    creation.mLinearDamping = settings.linearDamping;
    creation.mAngularDamping = settings.angularDamping;
    creation.mGravityFactor = settings.gravityScale;
    creation.mUserData = entity.id();
    if (settings.type == BodyType::Dynamic && settings.mass > 0.0f) {
      creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      creation.mMassPropertiesOverride.mMass = settings.mass;
    }
    body.id = bodies().CreateAndAddBody(creation, settings.type == BodyType::Static ? JPH::EActivation::DontActivate
                                                                                    : JPH::EActivation::Activate);
    if (body.id.IsInvalid()) {
      SONNET_LOG_ERROR("{}: no physics body: the world's {} bodies are in use", describe(entity),
                       m_system->GetMaxBodies());
    }
  }

  void destroyBody(flecs::entity_t entity) {
    const auto it = m_bodies.find(entity);
    if (it == m_bodies.end()) {
      return;
    }
    if (!it->second.id.IsInvalid()) {
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
      if (!ecs.is_alive(id) || entity.has<world::Disabled>() || !hasCollider(entity)) {
        stale.push_back(id);
      }
    }
    for (const flecs::entity_t id : stale) {
      destroyBody(id);
    }
    // Bodies for the colliders that have none yet.
    m_colliders.each([&](flecs::entity entity) {
      if (!m_bodies.contains(entity.id())) {
        createBody(entity);
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
