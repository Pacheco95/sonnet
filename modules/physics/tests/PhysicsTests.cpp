#include <sonnet/physics/PhysicsWorld.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
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
  assets::AssetDatabase assets{renderer};
  world::World world;
  std::unique_ptr<physics::IPhysicsWorld> physics = physics::createPhysicsWorld(world, assets);

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

} // namespace

TEST_CASE("physics components are registered with reflection and round-trip through scenes", "[physics]") {
  Fixture fixture;
  for (const char *name : {"RigidBody", "BoxCollider", "SphereCollider", "CapsuleCollider", "MeshCollider"}) {
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
