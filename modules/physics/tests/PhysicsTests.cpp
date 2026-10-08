#include <sonnet/physics/PhysicsWorld.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

constexpr float Step = 1.0f / 60.0f;

// The world is declared before physics, which has to go first.
struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  // Two workers rather than the machine's count: enough to run Jolt's jobs on the pool rather
  // than inline, few enough that a fixture per test case stays cheap. The benchmark sets its own.
  core::JobSystem jobs;
  std::unique_ptr<physics::IPhysicsWorld> physics = physics::createPhysicsWorld(world, assets, jobs);

  explicit Fixture(std::uint32_t workers = 2) : jobs({.workerCount = workers}) {
  }

  // A static 20 x 1 x 20 slab whose top face is at y = 0.
  flecs::entity ground() {
    const flecs::entity entity = world.createEntity("Ground");
    entity.set<world::Transform>({.position = {0.0f, -0.5f, 0.0f}});
    entity.set<physics::BoxCollider>({.halfExtents = {10.0f, 0.5f, 10.0f}});
    return entity;
  }

  flecs::entity ball(glm::vec3 position, float radius = 0.5f) {
    const flecs::entity entity = world.createEntity("Ball");
    entity.set<world::Transform>({.position = position});
    entity.set<physics::SphereCollider>({.radius = radius});
    entity.set<physics::RigidBody>({});
    return entity;
  }

  void run(float seconds) {
    for (float t = 0.0f; t < seconds; t += Step) {
      world.progress(Step);
    }
  }
};

glm::vec3 positionOf(flecs::entity entity) {
  return entity.get<world::Transform>().position;
}

// Every event of `seconds` of play, one fixed step per frame, with the step it came in.
struct Recorded {
  int step{0};
  physics::ContactEventKind kind{};
  flecs::entity_t first{0};
  flecs::entity_t second{0};
  bool operator==(const Recorded &) const = default;
};

std::vector<Recorded> record(Fixture &fixture, float seconds) {
  std::vector<Recorded> result;
  int step = 0;
  for (float t = 0.0f; t < seconds; t += Step, ++step) {
    fixture.world.progress(Step);
    for (const physics::ContactEvent &event : fixture.physics->events()) {
      result.push_back({step, event.kind, event.first.id(), event.second.id()});
    }
  }
  return result;
}

std::size_t count(const std::vector<Recorded> &events, physics::ContactEventKind kind) {
  return static_cast<std::size_t>(std::ranges::count(events, kind, &Recorded::kind));
}

// A static 2 x 2 x 2 sensor centred at (0, 1, 0).
flecs::entity zone(Fixture &fixture) {
  const flecs::entity entity = fixture.world.createEntity("Zone");
  entity.set<world::Transform>({.position = {0.0f, 1.0f, 0.0f}});
  entity.set<physics::BoxCollider>({.halfExtents = {1.0f, 1.0f, 1.0f}});
  entity.add<physics::Trigger>();
  return entity;
}

} // namespace

TEST_CASE("physics components are registered with reflection and round-trip through scenes", "[physics]") {
  Fixture fixture;
  for (const char *name :
       {"RigidBody", "BoxCollider", "SphereCollider", "CapsuleCollider", "MeshCollider", "Trigger"}) {
    REQUIRE(fixture.world.findComponent(name) != nullptr);
  }
  physics::registerComponents(fixture.world); // idempotent
  REQUIRE(std::ranges::count(fixture.world.components(), std::string{"RigidBody"}, &world::ComponentInfo::name) == 1);

  const flecs::entity body = fixture.ball({1.0f, 2.0f, 3.0f});
  body.set<physics::RigidBody>({.type = physics::BodyType::Kinematic, .mass = 4.0f});
  const world::ComponentInfo *rigidBody = fixture.world.findComponent("RigidBody");
  const nlohmann::json json = fixture.world.componentToJson(body, rigidBody->id);
  REQUIRE(json["type"] == "Kinematic");
  REQUIRE(json["mass"] == 4.0f);

  const core::Uuid uuid = fixture.world.uuidOf(body);
  const nlohmann::json scene = world::saveScene(fixture.world);
  fixture.world.clearScene();
  REQUIRE(world::loadScene(fixture.world, scene).has_value());
  const flecs::entity loaded = fixture.world.find(uuid);
  REQUIRE(loaded);
  REQUIRE(loaded.get<physics::RigidBody>().type == physics::BodyType::Kinematic);
  REQUIRE(loaded.get<physics::SphereCollider>().radius == 0.5f);
}

// Jolt was built without RTTI, and so was this module to match, so the world's vtable had no
// typeinfo: the sanitizer's vptr check rejected it wherever another module destroyed it, and
// typeid here read a null pointer.
TEST_CASE("the physics world carries its type information into other modules", "[physics]") {
  Fixture fixture;
  physics::IPhysicsWorld &physics = *fixture.physics;
  REQUIRE(typeid(physics) != typeid(physics::IPhysicsWorld));
  REQUIRE(dynamic_cast<physics::IPhysicsWorld *>(&physics) == &physics);
}

TEST_CASE("a dynamic ball falls onto static ground in play mode only", "[physics]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 3.0f, 0.0f});

  fixture.run(0.5f);
  REQUIRE(positionOf(ball).y == 3.0f);
  REQUIRE(fixture.physics->bodyCount() == 0);

  fixture.world.setPlaying(true);
  fixture.run(0.2f);
  REQUIRE(fixture.physics->bodyCount() == 2);
  REQUIRE(positionOf(ball).y < 3.0f);
  fixture.run(3.0f);
  // Resting on the ground: its centre one radius above the top face, less Jolt's penetration
  // slop of 2 cm.
  REQUIRE(positionOf(ball).y == Approx(0.49f).margin(0.02f));
  REQUIRE(positionOf(ball).x == Approx(0.0f).margin(0.01f));
  // World transforms follow the written pose.
  REQUIRE(ball.get<world::WorldTransform>().matrix[3].y == Approx(positionOf(ball).y));
}

TEST_CASE("rendering interpolates between the last two steps", "[physics]") {
  Fixture fixture;
  const flecs::entity ball = fixture.ball({0.0f, 10.0f, 0.0f});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  fixture.world.progress(Step);
  const float afterTwo = positionOf(ball).y;
  fixture.world.progress(Step);
  const float afterThree = positionOf(ball).y;
  REQUIRE(afterThree < afterTwo);
  // Half a step on: no fixed step runs, the pose is halfway between the last two.
  fixture.world.progress(Step * 0.5f);
  REQUIRE(fixture.world.fixedAlpha() == Approx(0.5f).margin(1e-3f));
  const float halfway = positionOf(ball).y;
  REQUIRE(halfway < afterTwo);
  REQUIRE(halfway > afterThree - (afterTwo - afterThree));
}

TEST_CASE("a transform written from outside teleports a dynamic body", "[physics]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 0.5f, 0.0f});
  fixture.world.setPlaying(true);
  fixture.run(0.5f);
  REQUIRE(positionOf(ball).y == Approx(0.49f).margin(0.02f));

  // The next steps start from the written pose; what is drawn trails the last step by up to one.
  ball.get_mut<world::Transform>().position = {5.0f, 4.0f, 0.0f};
  fixture.world.progress(Step);
  fixture.world.progress(Step);
  REQUIRE(positionOf(ball).x == Approx(5.0f).margin(1e-3f));
  REQUIRE(positionOf(ball).y < 4.0f);
  REQUIRE(positionOf(ball).y > 3.9f);
}

TEST_CASE("kinematic bodies follow their transforms and push dynamic ones", "[physics]") {
  Fixture fixture;
  const flecs::entity paddle = fixture.world.createEntity("Paddle");
  paddle.set<physics::BoxCollider>({.halfExtents = {0.5f, 0.5f, 0.5f}});
  paddle.set<physics::RigidBody>({.type = physics::BodyType::Kinematic});
  const flecs::entity ball = fixture.ball({2.0f, 0.0f, 0.0f});
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);

  // Sweep the paddle through where the ball floats.
  for (int i = 0; i < 90; ++i) {
    paddle.get_mut<world::Transform>().position.x = 4.0f * static_cast<float>(i) / 90.0f;
    fixture.world.progress(Step);
  }
  REQUIRE(positionOf(paddle).x == Approx(4.0f * 89.0f / 90.0f));
  REQUIRE(positionOf(ball).x > 4.0f);
  // Rays see the kinematic body where its transform put it.
  const auto hit = fixture.physics->raycast({positionOf(paddle).x, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->entity == paddle);
}

TEST_CASE("a raycast reports the nearest body with its point, normal and distance", "[physics]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 2.0f, 0.0f});
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  REQUIRE(!fixture.physics->raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f).has_value());
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);

  const auto hit = fixture.physics->raycast({0.0f, 10.0f, 0.0f}, {0.0f, -2.0f, 0.0f}, 20.0f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->entity == ball);
  REQUIRE(hit->point.y == Approx(2.5f).margin(1e-3f));
  REQUIRE(hit->normal.y == Approx(1.0f).margin(1e-3f));
  REQUIRE(hit->distance == Approx(7.5f).margin(1e-3f));

  const auto beside = fixture.physics->raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
  REQUIRE(beside.has_value());
  REQUIRE(beside->entity == ground);
  REQUIRE(beside->point.y == Approx(0.0f).margin(1e-3f));
  REQUIRE(!fixture.physics->raycast({3.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f).has_value());
  // From inside the ball its own body comes first unless it is ignored.
  REQUIRE(fixture.physics->raycast({0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f)->entity == ball);
  const auto below = fixture.physics->raycast({0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, ball);
  REQUIRE(below.has_value());
  REQUIRE(below->entity == ground);
  REQUIRE(below->distance == Approx(2.0f).margin(1e-3f));
  REQUIRE(!fixture.physics->raycast({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 5.0f).has_value());
}

TEST_CASE("impulses and velocities act on dynamic bodies only", "[physics]") {
  Fixture fixture;
  const flecs::entity ball = fixture.ball({0.0f, 0.0f, 0.0f});
  ball.set<physics::RigidBody>({.mass = 2.0f, .linearDamping = 0.0f, .gravityScale = 0.0f});
  const flecs::entity wall = fixture.world.createEntity("Wall");
  wall.set<physics::BoxCollider>({});
  wall.get_mut<world::Transform>().position = {0.0f, 10.0f, 0.0f};
  fixture.world.setPlaying(true);

  // The body is made on demand, before the first step.
  fixture.physics->addImpulse(ball, {4.0f, 0.0f, 0.0f});
  REQUIRE(fixture.physics->linearVelocity(ball).x == Approx(2.0f)); // impulse / mass
  fixture.run(1.0f);
  REQUIRE(positionOf(ball).x == Approx(2.0f).margin(0.1f));

  fixture.physics->setLinearVelocity(ball, {0.0f, 0.0f, -1.0f});
  REQUIRE(fixture.physics->linearVelocity(ball) == glm::vec3{0.0f, 0.0f, -1.0f});
  fixture.physics->setAngularVelocity(ball, {0.0f, 1.0f, 0.0f});
  REQUIRE(fixture.physics->angularVelocity(ball).y == Approx(1.0f));

  fixture.physics->addImpulse(wall, {100.0f, 0.0f, 0.0f});
  fixture.physics->setLinearVelocity(wall, {1.0f, 0.0f, 0.0f});
  REQUIRE(fixture.physics->linearVelocity(wall) == glm::vec3{0.0f});
  REQUIRE(fixture.physics->linearVelocity(fixture.world.createEntity("No body")) == glm::vec3{0.0f});
}

TEST_CASE("bodies go with their entity, collider or enabled state and follow changes", "[physics]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 5.0f, 0.0f});
  const flecs::entity other = fixture.ball({3.0f, 5.0f, 0.0f});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 3);

  other.add<world::Disabled>();
  REQUIRE(fixture.physics->bodyCount() == 2);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 2);
  other.remove<world::Disabled>();
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 3);

  fixture.world.destroyEntity(other);
  REQUIRE(fixture.physics->bodyCount() == 2);

  // A bigger collider rebuilds the body: rays now hit its new surface.
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  ball.set<physics::SphereCollider>({.radius = 1.0f});
  fixture.world.progress(Step);
  const auto hit = fixture.physics->raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
  REQUIRE(hit.has_value());
  REQUIRE(hit->point.y == Approx(positionOf(ball).y + 1.0f).margin(1e-3f));

  ball.remove<physics::SphereCollider>();
  REQUIRE(fixture.physics->bodyCount() == 1);
  fixture.world.clearScene();
  REQUIRE(fixture.physics->bodyCount() == 0);
  static_cast<void>(ground);
}

TEST_CASE("scale, offsets, compounds and mesh colliders shape the bodies", "[physics]") {
  Fixture fixture;
  // A static box scaled to 4 m wide: a ray at x = 1.5 still hits it.
  const flecs::entity scaled = fixture.world.createEntity("Scaled");
  scaled.set<world::Transform>({.position = {0.0f, 0.0f, 0.0f}, .scale = {4.0f, 1.0f, 1.0f}});
  scaled.set<physics::BoxCollider>({});
  // A sphere offset 2 m up from its entity at x = 10, and a compound of a box and a sphere.
  const flecs::entity offset = fixture.world.createEntity("Offset");
  offset.set<world::Transform>({.position = {10.0f, 0.0f, 0.0f}});
  offset.set<physics::SphereCollider>({.radius = 0.5f, .offset = {0.0f, 2.0f, 0.0f}});
  const flecs::entity compound = fixture.world.createEntity("Compound");
  compound.set<world::Transform>({.position = {20.0f, 0.0f, 0.0f}});
  compound.set<physics::BoxCollider>({});
  compound.set<physics::SphereCollider>({.radius = 0.5f, .offset = {0.0f, 3.0f, 0.0f}});
  // The built-in plane as a static mesh, and the box mesh as a dynamic convex hull that falls onto it.
  const flecs::entity floor = fixture.world.createEntity("Floor");
  floor.set<world::Transform>({.position = {30.0f, 0.0f, 0.0f}, .scale = {10.0f, 1.0f, 10.0f}});
  floor.set<world::MeshRenderer>({.mesh = assets::builtin::plane()});
  floor.set<physics::MeshCollider>({});
  const flecs::entity crate = fixture.world.createEntity("Crate");
  crate.set<world::Transform>({.position = {30.0f, 2.0f, 0.0f}});
  crate.set<physics::MeshCollider>({.mesh = assets::builtin::box()});
  crate.set<physics::RigidBody>({});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 5);

  const glm::vec3 down{0.0f, -1.0f, 0.0f};
  const auto scaledHit = fixture.physics->raycast({1.5f, 5.0f, 0.0f}, down, 10.0f);
  REQUIRE((scaledHit && scaledHit->entity == scaled));
  REQUIRE(scaledHit->point.y == Approx(0.5f).margin(1e-3f));
  const auto offsetHit = fixture.physics->raycast({10.0f, 5.0f, 0.0f}, down, 10.0f);
  REQUIRE((offsetHit && offsetHit->entity == offset));
  REQUIRE(offsetHit->point.y == Approx(2.5f).margin(1e-3f));
  const auto compoundHit = fixture.physics->raycast({20.0f, 5.0f, 0.0f}, down, 10.0f);
  REQUIRE((compoundHit && compoundHit->entity == compound));
  REQUIRE(compoundHit->point.y == Approx(3.5f).margin(1e-3f));

  fixture.run(2.0f);
  REQUIRE(positionOf(crate).y == Approx(0.49f).margin(0.03f));
}

TEST_CASE("a collider that cannot be built gives no body and is not retried until it changes", "[physics]") {
  Fixture fixture;
  const flecs::entity entity = fixture.world.createEntity("Missing mesh");
  entity.set<physics::MeshCollider>({.mesh = core::Uuid::generate()});
  fixture.world.setPlaying(true);
  fixture.run(0.1f);
  REQUIRE(fixture.physics->bodyCount() == 0);
  entity.set<physics::MeshCollider>({.mesh = assets::builtin::box()});
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 1);
}

TEST_CASE("debug lines outline every collider, in edit mode too", "[physics]") {
  Fixture fixture;
  const flecs::entity box = fixture.world.createEntity("Box");
  box.set<physics::BoxCollider>({});
  const flecs::entity ball = fixture.ball({0.0f, 5.0f, 0.0f});
  const flecs::entity capsule = fixture.world.createEntity("Capsule");
  capsule.set<physics::CapsuleCollider>({});
  capsule.add<world::Disabled>();
  fixture.world.progress(0.0f);

  std::vector<renderer::DebugLine> lines;
  fixture.physics->debugLines(lines);
  // Twelve box edges and three circles of 24 segments.
  REQUIRE(lines.size() == 12 + 3 * 24);
  // The ball's circles sit around its world position, in the dynamic colour.
  const auto ballLine = std::ranges::find_if(lines, [](const renderer::DebugLine &line) { return line.from.y > 4.0f; });
  REQUIRE(ballLine != lines.end());
  REQUIRE(glm::length(ballLine->from - glm::vec3{0.0f, 5.0f, 0.0f}) == Approx(0.5f));
  REQUIRE(ballLine->color.g > ballLine->color.r);
  static_cast<void>(box);
  static_cast<void>(ball);

  capsule.remove<world::Disabled>();
  lines.clear();
  fixture.physics->debugLines(lines);
  // Two rings, four sides and four half circles.
  REQUIRE(lines.size() == 12 + 3 * 24 + 2 * 24 + 4 + 4 * 12);
}

TEST_CASE("stopping play and reloading the snapshot discards the simulated state", "[physics]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 3.0f, 0.0f});
  const core::Uuid uuid = fixture.world.uuidOf(ball);
  const nlohmann::json snapshot = world::saveScene(fixture.world);
  fixture.world.setPlaying(true);
  fixture.run(1.0f);
  REQUIRE(positionOf(ball).y < 2.0f);

  fixture.world.setPlaying(false);
  fixture.world.clearScene();
  REQUIRE(fixture.physics->bodyCount() == 0);
  REQUIRE(world::loadScene(fixture.world, snapshot).has_value());
  REQUIRE(positionOf(fixture.world.find(uuid)).y == 3.0f);

  fixture.world.setPlaying(true);
  fixture.run(0.1f);
  REQUIRE(fixture.physics->bodyCount() == 2);
}

// `physics_tests "[benchmark]"` prints the wall time of a stress scene's fixed steps with the pool
// idle and with it working, which is what ADR-0013 claims Jolt gains from the job system. Run it in
// Release: a Debug Jolt is slow enough to bury the difference in its own overhead.
TEST_CASE("a pile of dynamic bodies steps faster across cores", "[.][benchmark][physics]") {
  constexpr int columns = 12;
  constexpr int layers = 7;
  constexpr int steps = 120;

  const auto measure = [](std::uint32_t workers) {
    Fixture fixture{workers};
    fixture.ground();
    // A loose pile rather than a grid: the boxes settle into each other, so the solver has real
    // islands to split across threads instead of a thousand independent falls.
    for (int layer = 0; layer < layers; ++layer) {
      for (int x = 0; x < columns; ++x) {
        for (int z = 0; z < columns; ++z) {
          const flecs::entity body = fixture.world.createEntity("Box");
          const float jitter = static_cast<float>((x * 7 + z * 13 + layer * 3) % 5) * 0.01f;
          body.set<world::Transform>(
              {.position = {static_cast<float>(x) - 5.5f + jitter, 1.0f + static_cast<float>(layer) * 1.1f,
                            static_cast<float>(z) - 5.5f + jitter}});
          body.set<physics::BoxCollider>({.halfExtents = {0.5f, 0.5f, 0.5f}});
          body.set<physics::RigidBody>({});
        }
      }
    }
    fixture.world.setPlaying(true);
    fixture.world.progress(Step); // the first step creates the bodies; not what is being timed

    const auto start = std::chrono::steady_clock::now();
    for (int step = 0; step < steps; ++step) {
      fixture.world.progress(Step);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    return std::pair{elapsed.count() / steps, fixture.physics->bodyCount()};
  };

  const unsigned hardware = std::thread::hardware_concurrency();
  const auto [inlineMs, inlineBodies] = measure(0);
  const auto [pooledMs, pooledBodies] = measure(hardware > 1 ? hardware - 1 : 1);
  REQUIRE(inlineBodies == pooledBodies);

  WARN(std::format("{} bodies, {} steps", inlineBodies, steps));
  WARN(std::format("no workers        {:8.3f} ms per step", inlineMs));
  WARN(std::format("{:2} workers        {:8.3f} ms per step, {:.2f}x", hardware > 1 ? hardware - 1 : 1, pooledMs,
                   inlineMs / pooledMs));
}

TEST_CASE("every built-in solid takes a mesh collider, static or dynamic", "[physics]") {
  struct Shape {
    core::Uuid (*mesh)() noexcept;
    float rayX; // where a ray from above meets the solid: not through the torus's hole or the arch's opening
  };
  const Shape shape = GENERATE(Shape{assets::builtin::box, 0.0f}, Shape{assets::builtin::sphere, 0.0f},
                               Shape{assets::builtin::cylinder, 0.0f}, Shape{assets::builtin::capsule, 0.0f},
                               Shape{assets::builtin::cone, 0.0f}, Shape{assets::builtin::torus, 0.35f},
                               Shape{assets::builtin::ramp, 0.0f}, Shape{assets::builtin::stairs, 0.0f},
                               Shape{assets::builtin::hemisphere, 0.0f}, Shape{assets::builtin::arch, 0.4f},
                               Shape{assets::builtin::icosphere, 0.0f});
  Fixture fixture;
  fixture.ground();
  fixture.world.progress(0.0f);

  // Static: its triangles are in the way of a ray from above.
  const flecs::entity fixed = fixture.world.createEntity("Fixed");
  fixed.set<world::Transform>({.position = {20.0f, 1.0f, 0.0f}});
  fixed.set<world::MeshRenderer>({.mesh = shape.mesh()});
  fixed.set<physics::MeshCollider>({});
  // Dynamic: its hull falls onto the ground and stays there.
  const flecs::entity falling = fixture.world.createEntity("Falling");
  falling.set<world::Transform>({.position = {0.0f, 3.0f, 0.0f}});
  falling.set<world::MeshRenderer>({.mesh = shape.mesh()});
  falling.set<physics::MeshCollider>({});
  falling.set<physics::RigidBody>({});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 3);

  const auto hit = fixture.physics->raycast({20.0f + shape.rayX, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f);
  REQUIRE((hit && hit->entity == fixed));

  fixture.run(4.0f);
  REQUIRE(positionOf(falling).y > 0.05f);
  REQUIRE(positionOf(falling).y < 1.0f);
  REQUIRE(std::abs(positionOf(falling).x) < 2.0f);
}

TEST_CASE("a trigger reports a body entering and leaving once and pushes nothing", "[physics][events]") {
  Fixture fixture;
  const flecs::entity trigger = zone(fixture);
  const flecs::entity ball = fixture.ball({-5.0f, 1.0f, 0.0f});
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  fixture.world.setPlaying(true);
  fixture.physics->setLinearVelocity(ball, {5.0f, 0.0f, 0.0f});

  const std::vector<Recorded> events = record(fixture, 3.0f);
  REQUIRE(events.size() == 2);
  REQUIRE(events[0].kind == physics::ContactEventKind::TriggerEnter);
  REQUIRE(events[1].kind == physics::ContactEventKind::TriggerExit);
  REQUIRE(events[0].step < events[1].step);
  // Ordered by id, whichever is the sensor.
  REQUIRE(events[0].first == std::min(trigger.id(), ball.id()));
  REQUIRE(events[0].second == std::max(trigger.id(), ball.id()));
  // The ball went through, slowed by its damping alone, and well past.
  REQUIRE(fixture.physics->linearVelocity(ball).x > 4.0f);
  REQUIRE(std::abs(fixture.physics->linearVelocity(ball).y) < 0.001f);
  REQUIRE(positionOf(ball).x > 5.0f);
}

TEST_CASE("a trigger on a body that falls asleep inside it does not report an exit", "[physics][events]") {
  Fixture fixture;
  zone(fixture);
  fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 2.5f, 0.0f});
  fixture.world.setPlaying(true);

  const std::vector<Recorded> events = record(fixture, 4.0f);
  REQUIRE(count(events, physics::ContactEventKind::TriggerEnter) == 1);
  REQUIRE(count(events, physics::ContactEventKind::TriggerExit) == 0);
  REQUIRE(count(events, physics::ContactEventKind::ContactBegin) >= 1);
  REQUIRE(count(events, physics::ContactEventKind::ContactEnd) == 0);

  // Taken out of the zone by a script, it exits once, and a sleeping body leaves it too.
  fixture.physics->setLinearVelocity(ball, {8.0f, 8.0f, 0.0f});
  const std::vector<Recorded> after = record(fixture, 1.0f);
  REQUIRE(count(after, physics::ContactEventKind::TriggerExit) == 1);
  REQUIRE(count(after, physics::ContactEventKind::ContactEnd) == 1);
}

TEST_CASE("a ball landing on the ground begins one contact with its point and normal", "[physics][events]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 2.0f, 0.0f});
  fixture.world.setPlaying(true);

  std::vector<physics::ContactEvent> begins;
  for (int step = 0; step < 120; ++step) {
    fixture.world.progress(Step);
    for (const physics::ContactEvent &event : fixture.physics->events()) {
      if (event.kind == physics::ContactEventKind::ContactBegin) {
        begins.push_back(event);
      }
    }
  }
  REQUIRE(begins.size() == 1);
  const physics::ContactEvent &begin = begins.front();
  REQUIRE(((begin.first == ground && begin.second == ball) || (begin.first == ball && begin.second == ground)));
  // The normal runs from the lower id to the higher, and the ball is above the ground.
  const float towardsBall = begin.second == ball ? 1.0f : -1.0f;
  REQUIRE(begin.normal.y * towardsBall == Approx(1.0f).margin(0.05f));
  REQUIRE(begin.point.y == Approx(0.0f).margin(0.1f));
  REQUIRE(std::abs(begin.point.x) < 0.3f);
}

TEST_CASE("a body that sleeps keeps its contacts and ends them when it leaves", "[physics][events]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 1.0f, 0.0f});
  fixture.world.setPlaying(true);

  const std::vector<Recorded> settling = record(fixture, 5.0f); // sleeps after about half a second
  REQUIRE(count(settling, physics::ContactEventKind::ContactBegin) == 1);
  REQUIRE(count(settling, physics::ContactEventKind::ContactEnd) == 0);
  // Pushed, it wakes, rolls on the ground and keeps touching it without a new begin.
  fixture.physics->addImpulse(ball, {1.0f, 0.0f, 0.0f});
  const std::vector<Recorded> rolling = record(fixture, 0.5f);
  REQUIRE(count(rolling, physics::ContactEventKind::ContactBegin) == 0);
  REQUIRE(count(rolling, physics::ContactEventKind::ContactEnd) == 0);
  // Thrown up, it leaves.
  fixture.physics->addImpulse(ball, {0.0f, 8.0f, 0.0f});
  const std::vector<Recorded> leaving = record(fixture, 0.5f);
  REQUIRE(count(leaving, physics::ContactEventKind::ContactEnd) == 1);
}

TEST_CASE("destroying an entity ends its contacts for the other one", "[physics][events]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity ball = fixture.ball({0.0f, 0.6f, 0.0f});
  fixture.world.setPlaying(true);
  record(fixture, 1.0f);
  fixture.world.destroyEntity(ball);
  const std::vector<Recorded> events = record(fixture, 0.2f);
  REQUIRE(count(events, physics::ContactEventKind::ContactEnd) == 1);
  // The destroyed entity is in the event, no longer alive; the survivor is the other.
  const auto &event = *std::ranges::find(events, physics::ContactEventKind::ContactEnd, &Recorded::kind);
  REQUIRE(((event.first == ground.id()) != (event.second == ground.id())));
  REQUIRE(!fixture.world.ecs().is_alive(event.first == ground.id() ? event.second : event.first));
}

TEST_CASE("events come in the same order whatever the worker count", "[physics][events]") {
  const auto run = [](std::uint32_t workers) {
    Fixture fixture{workers};
    fixture.ground();
    zone(fixture);
    for (int i = 0; i < 40; ++i) {
      const flecs::entity box = fixture.world.createEntity("Box");
      const float x = static_cast<float>(i % 8) - 3.5f;
      const int row = i / 8;
      const float z = static_cast<float>(row) - 2.0f;
      box.set<world::Transform>({.position = {x * 0.9f, 1.0f + static_cast<float>(i % 3) * 1.2f, z * 0.9f}});
      box.set<physics::BoxCollider>({.halfExtents = {0.4f, 0.4f, 0.4f}});
      box.set<physics::RigidBody>({});
    }
    fixture.world.setPlaying(true);
    return record(fixture, 3.0f);
  };
  const std::vector<Recorded> inlineRun = run(0);
  const std::vector<Recorded> pooled = run(6);
  const std::vector<Recorded> again = run(6);
  REQUIRE(inlineRun.size() > 40);
  REQUIRE(pooled == again);
  REQUIRE(pooled == inlineRun);
  // Within a step the events are sorted by their entities.
  for (std::size_t i = 1; i < pooled.size(); ++i) {
    if (pooled[i].step == pooled[i - 1].step) {
      const bool ordered =
          std::tie(pooled[i - 1].first, pooled[i - 1].second) <= std::tie(pooled[i].first, pooled[i].second);
      REQUIRE(ordered);
    }
  }
}

TEST_CASE("more events than the cap in one step are counted and dropped from the end", "[physics][events]") {
  Fixture fixture;
  const flecs::entity big = fixture.world.createEntity("Big");
  big.set<physics::BoxCollider>({.halfExtents = {50.0f, 1.0f, 50.0f}});
  big.add<physics::Trigger>();
  const int bodies = static_cast<int>(physics::IPhysicsWorld::MaxContactEvents) + 300;
  for (int i = 0; i < bodies; ++i) {
    const flecs::entity ball = fixture.world.createEntity("Ball");
    const int line = i / 40;
    ball.set<world::Transform>(
        {.position = {static_cast<float>(i % 40) - 20.0f, 0.0f, static_cast<float>(line) - 15.0f}});
    ball.set<physics::SphereCollider>({.radius = 0.2f});
    ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  }
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  const std::span<const physics::ContactEvent> events = fixture.physics->events();
  REQUIRE(events.size() == physics::IPhysicsWorld::MaxContactEvents);
  REQUIRE(fixture.physics->droppedEvents() == 300);
  REQUIRE(std::ranges::is_sorted(events, {}, [](const physics::ContactEvent &e) { return e.first.id(); }));
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->events().empty());
  REQUIRE(fixture.physics->droppedEvents() == 0);
}

TEST_CASE("child colliders without a body join the nearest ancestor's body", "[physics][compound]") {
  Fixture fixture;
  fixture.ground();
  // A dumbbell: two boxes either side of an empty body entity.
  const flecs::entity dumbbell = fixture.world.createEntity("Dumbbell");
  dumbbell.set<world::Transform>({.position = {0.0f, 2.0f, 0.0f}});
  dumbbell.set<physics::RigidBody>({});
  for (const float x : {-2.0f, 2.0f}) {
    const flecs::entity weight = fixture.world.createEntity("Weight", dumbbell);
    weight.set<world::Transform>({.position = {x, 0.0f, 0.0f}});
    weight.set<physics::BoxCollider>({.halfExtents = {0.5f, 0.5f, 0.5f}});
  }
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 2); // the ground and one body for the dumbbell
  fixture.run(3.0f);
  REQUIRE(positionOf(dumbbell).y == Approx(0.49f).margin(0.03f));
  // It rests on both weights: level.
  REQUIRE(std::abs(positionOf(dumbbell).x) < 0.05f);
  const glm::quat rotation = dumbbell.get<world::Transform>().rotation;
  REQUIRE(std::abs(rotation.x) + std::abs(rotation.y) + std::abs(rotation.z) < 0.05f);
}

TEST_CASE("a child with its own body, or behind another body, stays separate", "[physics][compound]") {
  Fixture fixture;
  const flecs::entity parent = fixture.world.createEntity("Parent");
  parent.set<physics::RigidBody>({.gravityScale = 0.0f});
  parent.set<physics::BoxCollider>({});
  const flecs::entity own = fixture.world.createEntity("Own", parent);
  own.set<world::Transform>({.position = {5.0f, 0.0f, 0.0f}});
  own.set<physics::SphereCollider>({});
  own.set<physics::RigidBody>({.gravityScale = 0.0f});
  const flecs::entity grandchild = fixture.world.createEntity("Grandchild", own);
  grandchild.set<world::Transform>({.position = {0.0f, 3.0f, 0.0f}});
  grandchild.set<physics::SphereCollider>({});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 2);
  // Pushing the grandchild pushes its body, `Own`, and leaves the parent where it was.
  fixture.physics->addImpulse(grandchild, {0.0f, 6.0f, 0.0f});
  REQUIRE(fixture.physics->linearVelocity(own).y == Approx(6.0f).margin(0.5f));
  REQUIRE(fixture.physics->linearVelocity(parent).y == Approx(0.0f).margin(0.001f));
}

TEST_CASE("a compound body follows its children being added, moved, disabled and destroyed", "[physics][compound]") {
  Fixture fixture;
  const flecs::entity root = fixture.world.createEntity("Root");
  root.set<physics::RigidBody>({.type = physics::BodyType::Kinematic});
  const flecs::entity arm = fixture.world.createEntity("Arm", root);
  arm.set<physics::BoxCollider>({.halfExtents = {0.5f, 0.5f, 0.5f}});
  fixture.world.setPlaying(true);
  fixture.world.progress(Step);
  REQUIRE(fixture.physics->bodyCount() == 1);
  const auto hitsAt = [&](float x) {
    const auto hit = fixture.physics->raycast({x, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f);
    return hit && hit->entity == root;
  };
  REQUIRE(hitsAt(0.0f));
  REQUIRE(!hitsAt(4.0f));

  // Moved: the shape follows.
  arm.set<world::Transform>({.position = {4.0f, 0.0f, 0.0f}});
  fixture.world.progress(Step);
  REQUIRE(!hitsAt(0.0f));
  REQUIRE(hitsAt(4.0f));

  // A second child added to a running body joins it.
  const flecs::entity extra = fixture.world.createEntity("Extra", root);
  extra.set<physics::SphereCollider>({.radius = 0.5f});
  fixture.world.progress(Step);
  REQUIRE(hitsAt(0.0f));
  REQUIRE(hitsAt(4.0f));
  REQUIRE(fixture.physics->bodyCount() == 1);

  // Disabled, it leaves; destroyed, so does the other.
  extra.add<world::Disabled>();
  fixture.world.progress(Step);
  REQUIRE(!hitsAt(0.0f));
  REQUIRE(hitsAt(4.0f));
  fixture.world.destroyEntity(arm);
  fixture.world.progress(Step);
  REQUIRE(!hitsAt(4.0f));
  REQUIRE(fixture.physics->bodyCount() == 0);
}

TEST_CASE("a compound body's contacts name the body, and a trigger can be compound", "[physics][compound][events]") {
  Fixture fixture;
  const flecs::entity sensor = fixture.world.createEntity("Sensor");
  sensor.set<world::Transform>({.position = {0.0f, 1.0f, 0.0f}});
  sensor.set<physics::RigidBody>({.type = physics::BodyType::Static});
  sensor.add<physics::Trigger>();
  for (const float x : {-1.0f, 1.0f}) {
    const flecs::entity part = fixture.world.createEntity("Part", sensor);
    part.set<world::Transform>({.position = {x, 0.0f, 0.0f}});
    part.set<physics::BoxCollider>({.halfExtents = {0.5f, 0.5f, 0.5f}});
  }
  // Both halves overlap the ball at once, and the pair is still entered once.
  const flecs::entity ball = fixture.ball({0.0f, 1.0f, 0.0f}, 1.2f);
  ball.set<physics::RigidBody>({.gravityScale = 0.0f});
  fixture.world.setPlaying(true);
  const std::vector<Recorded> events = record(fixture, 0.5f);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].kind == physics::ContactEventKind::TriggerEnter);
  REQUIRE(events[0].first == std::min(sensor.id(), ball.id()));
}
