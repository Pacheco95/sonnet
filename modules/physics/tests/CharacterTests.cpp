// The character controller (ADR-0038) through IPhysicsWorld, in play mode. Tolerances are Jolt's:
// the capsule rests within a few millimetres of the floor, stops its 2 cm padding short of a wall,
// and limits are tested a margin either side of the configured value rather than on it.

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

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

constexpr float Step = 1.0f / 60.0f;
constexpr float Gravity = 9.81f;
constexpr float Radius = 0.3f;

float degrees(float value) {
  return value * std::numbers::pi_v<float> / 180.0f;
}

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  core::JobSystem jobs{{.workerCount = 2}};
  std::unique_ptr<physics::IPhysicsWorld> physics = physics::createPhysicsWorld(world, assets, jobs);

  // A static 100 x 1 x 100 slab whose top face is at y = 0.
  flecs::entity ground() {
    return box("Ground", {50.0f, 0.5f, 50.0f}, {0.0f, -0.5f, 0.0f});
  }

  flecs::entity box(const char *name, glm::vec3 half, glm::vec3 position, glm::quat rotation = {1, 0, 0, 0}) {
    const flecs::entity entity = world.createEntity(name);
    entity.set<world::Transform>({.position = position, .rotation = rotation});
    entity.set<physics::BoxCollider>({.halfExtents = half});
    return entity;
  }

  // A static ramp rising along +x at `angle`, its surface starting at (x0, 0).
  flecs::entity ramp(float angle, float x0, float length = 10.0f) {
    const glm::vec3 along{std::cos(angle), std::sin(angle), 0.0f};
    const glm::vec3 normal{-std::sin(angle), std::cos(angle), 0.0f};
    const glm::vec3 centre = glm::vec3{x0, 0.0f, 0.0f} + along * length - normal * 0.5f;
    return box("Ramp", {length, 0.5f, 3.0f}, centre, glm::angleAxis(angle, glm::vec3{0.0f, 0.0f, 1.0f}));
  }

  flecs::entity character(glm::vec3 position, physics::CharacterController controller = {}) {
    const flecs::entity entity = world.createEntity("Character");
    entity.set<world::Transform>({.position = position});
    entity.set<physics::CharacterController>(controller);
    return entity;
  }

  void play() {
    world.setPlaying(true);
  }

  void run(float seconds) {
    for (float t = 0.0f; t < seconds; t += Step) {
      world.progress(Step);
    }
  }

  // One frame of one fixed step, after which the drawn pose is the step's own.
  void step(int count = 1) {
    for (int i = 0; i < count; ++i) {
      world.progress(Step);
    }
  }

  [[nodiscard]] physics::CharacterState state(flecs::entity entity) const {
    const auto state = physics->character(entity);
    REQUIRE(state.has_value());
    return *state;
  }

  [[nodiscard]] std::size_t countEvents(physics::ContactEventKind kind, flecs::entity a, flecs::entity b) const {
    return static_cast<std::size_t>(std::ranges::count_if(physics->events(), [&](const physics::ContactEvent &event) {
      return event.kind == kind && ((event.first == a && event.second == b) || (event.first == b && event.second == a));
    }));
  }
};

glm::vec3 positionOf(flecs::entity entity) {
  return entity.get<world::Transform>().position;
}

// Steps until the character stands, failing after a second.
void settle(Fixture &fixture, flecs::entity character) {
  for (int i = 0; i < 60; ++i) {
    fixture.step();
    if (fixture.state(character).grounded) {
      fixture.step(5);
      return;
    }
  }
  FAIL("the character never stood");
}

} // namespace

TEST_CASE("the character controller is registered with reflection and round-trips through scenes",
          "[physics][character]") {
  Fixture fixture;
  REQUIRE(fixture.world.findComponent("CharacterController") != nullptr);
  const flecs::entity entity =
      fixture.character({1.0f, 2.0f, 3.0f}, {.radius = 0.4f, .maxSlope = degrees(40.0f), .stepHeight = 0.2f});
  const core::Uuid uuid = fixture.world.uuidOf(entity);
  const nlohmann::json scene = world::saveScene(fixture.world);
  fixture.world.clearScene();
  REQUIRE(world::loadScene(fixture.world, scene).has_value());
  const physics::CharacterController loaded = fixture.world.find(uuid).get<physics::CharacterController>();
  REQUIRE(loaded.radius == 0.4f);
  REQUIRE(loaded.maxSlope == Approx(degrees(40.0f)));
  REQUIRE(loaded.stepHeight == 0.2f);
  REQUIRE(loaded.halfHeight == 0.6f);
}

TEST_CASE("a character exists in play mode only and its commands check their input", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 1.0f, 0.0f});
  const flecs::entity plain = fixture.world.createEntity("Plain");

  // Edit mode has none.
  fixture.run(0.2f);
  REQUIRE_FALSE(fixture.physics->character(character).has_value());
  REQUIRE_FALSE(fixture.physics->setCharacterMove(character, {1.0f, 0.0f, 0.0f}));
  REQUIRE(positionOf(character).y == 1.0f);

  fixture.play();
  // Created on the spot by a command, before any step: at rest, in the air, nothing below.
  const auto fresh = fixture.physics->character(character);
  REQUIRE(fresh.has_value());
  REQUIRE_FALSE(fresh->grounded);
  REQUIRE(fresh->velocity == glm::vec3{0.0f});
  REQUIRE_FALSE(fresh->support);

  REQUIRE_FALSE(fixture.physics->setCharacterMove(character, {1.0f, 0.5f, 0.0f}));
  REQUIRE_FALSE(fixture.physics->setCharacterMove(character, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}));
  REQUIRE_FALSE(fixture.physics->jumpCharacter(character, 0.0f));
  REQUIRE_FALSE(fixture.physics->jumpCharacter(character, std::numeric_limits<float>::infinity()));
  REQUIRE_FALSE(
      fixture.physics->teleportCharacter(character, {0.0f, std::numeric_limits<float>::infinity(), 0.0f}, 0.0f));
  REQUIRE(fixture.physics->setCharacterMove(character, {0.0f, 0.00005f, 0.0f}));
  REQUIRE_FALSE(fixture.physics->setCharacterMove(plain, {1.0f, 0.0f, 0.0f}));
  REQUIRE_FALSE(fixture.physics->jumpCharacter(plain, 1.0f));
  REQUIRE_FALSE(fixture.physics->character(plain).has_value());
  REQUIRE_FALSE(fixture.physics->character(flecs::entity{}).has_value());
  // Characters are not counted as bodies.
  REQUIRE(fixture.physics->bodyCount() == 0);
  fixture.step();
  REQUIRE(fixture.physics->bodyCount() == 1);
}

TEST_CASE("a character falls under gravity and stands on flat ground", "[physics][character]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 1.0f, 0.0f});
  fixture.play();
  bool began = false;
  float fallTime = 0.0f;
  while (!fixture.state(character).grounded && fallTime < 2.0f) {
    fixture.step();
    fallTime += Step;
    began = began || fixture.countEvents(physics::ContactEventKind::ContactBegin, character, ground) == 1;
    if (fixture.countEvents(physics::ContactEventKind::ContactBegin, character, ground) == 1) {
      // The ground is the lower id: the normal points from it up towards the character.
      const auto event = std::ranges::find(fixture.physics->events(), physics::ContactEventKind::ContactBegin,
                                           &physics::ContactEvent::kind);
      REQUIRE(event->first == ground);
      REQUIRE(event->normal.y == Approx(1.0f).margin(0.01f));
    }
  }
  // About sqrt(2h/g) = 0.45 s, at the step that sees the ground.
  CAPTURE(fallTime);
  REQUIRE(fallTime == Approx(std::sqrt(2.0f / Gravity)).margin(0.1f));
  REQUIRE(began);

  // What is drawn trails the step by up to one, so the rest pose shows a step later.
  fixture.step(2);
  const float rest = positionOf(character).y;
  CAPTURE(rest);
  REQUIRE(rest == Approx(0.0f).margin(0.005f));
  for (int i = 0; i < 60; ++i) {
    fixture.step();
    REQUIRE(positionOf(character).y == Approx(rest).margin(1e-3f));
  }
  const physics::CharacterState state = fixture.state(character);
  REQUIRE(state.grounded);
  REQUIRE_FALSE(state.sliding);
  REQUIRE(state.support == ground);
  REQUIRE(state.groundNormal.y == Approx(1.0f).margin(1e-3f));
  REQUIRE(glm::length(state.velocity) < 0.01f);
  REQUIRE(state.supportVelocity == glm::vec3{0.0f});
}

TEST_CASE("a character slides along a wall it walks into diagonally and stops in a corner", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  // A wall along x with its face at z = 2, and one along z with its face at x = 4.
  fixture.box("Wall", {10.0f, 2.0f, 0.5f}, {0.0f, 2.0f, 2.5f});
  fixture.box("Side", {0.5f, 2.0f, 10.0f}, {4.5f, 2.0f, 0.0f});
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, character);

  REQUIRE(fixture.physics->setCharacterMove(character, glm::vec3{1.0f, 0.0f, 2.0f}));
  fixture.run(1.5f);
  // Against the wall, short of it by the radius and Jolt's padding, and still moving along it.
  const glm::vec3 sliding = positionOf(character);
  CAPTURE(sliding.x, sliding.z);
  REQUIRE(sliding.z == Approx(2.0f - Radius - 0.02f).margin(0.02f));
  REQUIRE(sliding.x > 1.0f);
  REQUIRE(fixture.state(character).velocity.x == Approx(1.0f).margin(0.05f));
  REQUIRE(fixture.state(character).grounded);

  // Into the corner: it comes to rest there, against both walls, without jitter.
  fixture.run(4.0f);
  const glm::vec3 corner = positionOf(character);
  CAPTURE(corner.x, corner.z);
  REQUIRE(corner.x == Approx(4.0f - Radius - 0.02f).margin(0.02f));
  REQUIRE(corner.z == Approx(2.0f - Radius - 0.02f).margin(0.02f));
  for (int i = 0; i < 30; ++i) {
    fixture.step();
    REQUIRE(glm::length(positionOf(character) - corner) < 2e-3f);
  }
}

TEST_CASE("a character climbs a step just below its step height and not one just above", "[physics][character]") {
  const auto walkInto = [](float stepTop) {
    Fixture fixture;
    fixture.ground();
    fixture.box("Step", {1.0f, stepTop / 2.0f, 3.0f}, {2.0f, stepTop / 2.0f, 0.0f}); // from x = 1 to 3
    const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f}, {.stepHeight = 0.3f});
    fixture.play();
    settle(fixture, character);
    REQUIRE(fixture.physics->setCharacterMove(character, {2.0f, 0.0f, 0.0f}));
    float highestRise = 0.0f;
    float previous = positionOf(character).y;
    for (int i = 0; i < 60; ++i) {
      fixture.step();
      highestRise = std::max(highestRise, positionOf(character).y - previous);
      previous = positionOf(character).y;
    }
    return std::pair{positionOf(character), highestRise};
  };
  // Below the step height, and above what it reaches: the step height plus the height at which the
  // rounded bottom still meets an edge at the maximum slope, r (1 - cos 50 degrees) = 0.107 m, a
  // margin either side.
  const auto [climbed, rise] = walkInto(0.25f);
  CAPTURE(climbed.x, climbed.y, rise);
  REQUIRE(climbed.x > 1.5f);
  REQUIRE(climbed.y == Approx(0.25f).margin(0.01f));
  // The capsule rides up the edge over several steps: no single step rises by the whole step.
  REQUIRE(rise < 0.1f);
  const auto [blocked, none] = walkInto(0.3f + Radius * (1.0f - std::cos(degrees(50.0f))) + 0.05f);
  CAPTURE(blocked.x, blocked.y);
  REQUIRE(blocked.x == Approx(1.0f - Radius - 0.02f).margin(0.02f));
  REQUIRE(blocked.y < 0.01f);
}

TEST_CASE("a character walks up a slope just under its maximum and not one just over", "[physics][character]") {
  const auto walkUp = [](float slope) {
    Fixture fixture;
    fixture.ground();
    fixture.ramp(degrees(slope), 1.0f);
    const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f}, {.maxSlope = degrees(45.0f)});
    fixture.play();
    settle(fixture, character);
    REQUIRE(fixture.physics->setCharacterMove(character, {2.0f, 0.0f, 0.0f}));
    fixture.run(2.5f);
    return std::pair{positionOf(character).y, fixture.state(character)};
  };
  const auto [gentle, gentleState] = walkUp(40.0f);
  CAPTURE(gentle);
  REQUIRE(gentle > 1.5f);
  REQUIRE(gentleState.grounded);
  REQUIRE(gentleState.groundNormal.y == Approx(std::cos(degrees(40.0f))).margin(0.01f));
  const auto [steep, steepState] = walkUp(50.0f);
  CAPTURE(steep);
  REQUIRE(steep < 0.35f);
}

TEST_CASE("a character stands still on a walkable slope and slides down a steep one", "[physics][character]") {
  const auto settleOn = [](float slope) {
    Fixture fixture;
    fixture.ground();
    const float angle = degrees(slope);
    fixture.ramp(angle, 0.0f, 4.0f);
    const float x = 2.0f * std::cos(angle);
    const flecs::entity character = fixture.character({x, x * std::tan(angle) + 0.2f, 0.0f});
    fixture.play();
    fixture.run(0.5f);
    const glm::vec3 landed = positionOf(character);
    const physics::CharacterState state = fixture.state(character);
    fixture.run(2.0f);
    return std::tuple{landed, positionOf(character), state};
  };
  const auto [gentleLanded, gentleAfter, gentleState] = settleOn(30.0f);
  CAPTURE(glm::length(gentleAfter - gentleLanded));
  REQUIRE(gentleState.grounded);
  REQUIRE(glm::length(gentleAfter - gentleLanded) < 0.005f);
  const auto [steepLanded, steepAfter, steepState] = settleOn(60.0f);
  CAPTURE(steepLanded.y, steepAfter.y);
  REQUIRE(steepState.sliding);
  REQUIRE_FALSE(steepState.grounded);
  REQUIRE(steepAfter.y < 0.3f);
}

TEST_CASE("a character runs downhill without leaving the ground or popping", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const float angle = degrees(35.0f);
  fixture.ramp(angle, 0.0f, 6.0f);
  const float topX = 6.0f * std::cos(angle) - 0.6f;
  const flecs::entity character = fixture.character({topX, topX * std::tan(angle) + 0.1f, 0.0f});
  fixture.play();
  settle(fixture, character);
  const float speed = 5.0f;
  REQUIRE(fixture.physics->setCharacterMove(character, {-speed, 0.0f, 0.0f}));
  float previous = positionOf(character).y;
  int steps = 0;
  while (positionOf(character).x > 0.6f && steps < 300) {
    fixture.step();
    ++steps;
    REQUIRE(fixture.state(character).grounded);
    // Each step drops as far as the slope says, never a snap far below it.
    const float drop = previous - positionOf(character).y;
    REQUIRE(drop <= speed * Step * std::tan(angle) + 0.01f);
    previous = positionOf(character).y;
  }
  REQUIRE(positionOf(character).y < 0.5f);
}

TEST_CASE("a character snaps down a small ledge and falls off a high one", "[physics][character]") {
  const auto walkOff = [](float height) {
    Fixture fixture;
    fixture.ground();
    fixture.box("Ledge", {2.0f, height / 2.0f, 3.0f}, {0.0f, height / 2.0f, 0.0f}); // edge at x = 2
    const flecs::entity character = fixture.character({1.0f, height, 0.0f}, {.groundSnap = 0.3f});
    fixture.play();
    settle(fixture, character);
    REQUIRE(fixture.physics->setCharacterMove(character, {2.0f, 0.0f, 0.0f}));
    int airborne = 0;
    for (int i = 0; i < 90; ++i) {
      fixture.step();
      airborne += fixture.state(character).grounded ? 0 : 1;
    }
    REQUIRE(fixture.state(character).grounded);
    REQUIRE(positionOf(character).y == Approx(0.0f).margin(0.01f));
    return airborne;
  };
  // Under the snap distance it stays on the ground but for the step or two its rounded bottom rolls
  // over the edge, where Jolt sees the edge as too steep; over it, it falls for a while and lands.
  const int low = walkOff(0.2f);
  const int high = walkOff(0.8f);
  CAPTURE(low, high);
  REQUIRE(low <= 2);
  REQUIRE(high > 10);
}

TEST_CASE("a jump leaves the ground, peaks, stops at a ceiling and lands", "[physics][character]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  // A ceiling 3 m up over x > 5 only.
  fixture.box("Ceiling", {2.0f, 0.1f, 2.0f}, {8.0f, 3.1f, 0.0f});
  fixture.play();
  settle(fixture, character);

  REQUIRE(fixture.physics->jumpCharacter(character, 5.0f));
  REQUIRE(fixture.physics->jumpCharacter(character, 4.0f)); // the last request wins
  fixture.step();
  REQUIRE_FALSE(fixture.state(character).grounded);
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactEnd, character, ground) == 1);
  float peak = 0.0f;
  int airborne = 1;
  while (!fixture.state(character).grounded && airborne < 120) {
    fixture.step();
    peak = std::max(peak, positionOf(character).y);
    ++airborne;
  }
  CAPTURE(peak, airborne);
  REQUIRE(peak == Approx(4.0f * 4.0f / (2.0f * Gravity)).margin(0.05f));
  REQUIRE(static_cast<float>(airborne) * Step == Approx(2.0f * 4.0f / Gravity).margin(0.1f));
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactBegin, character, ground) == 1);
  fixture.step(2);
  REQUIRE(positionOf(character).y == Approx(0.0f).margin(0.005f));

  // Under the ceiling a 6 m/s jump (1.8 m high) is cut short with the head under it (1.8 m tall
  // capsule, ceiling at 3 m), and the character falls straight back.
  REQUIRE(fixture.physics->teleportCharacter(character, {8.0f, 0.0f, 0.0f}, 0.0f));
  settle(fixture, character);
  REQUIRE(fixture.physics->jumpCharacter(character, 6.0f));
  peak = 0.0f;
  for (int i = 0; i < 90; ++i) {
    fixture.step();
    peak = std::max(peak, positionOf(character).y);
  }
  CAPTURE(peak);
  REQUIRE(peak < 3.0f - 1.8f);
  REQUIRE(peak > 1.0f);
  REQUIRE(fixture.state(character).grounded);
}

TEST_CASE("a character rides a translating platform and keeps its velocity when it leaves", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity platform = fixture.box("Platform", {3.0f, 0.1f, 2.0f}, {0.0f, 1.0f, 0.0f});
  platform.set<physics::RigidBody>({.type = physics::BodyType::Kinematic});
  const flecs::entity character = fixture.character({0.0f, 1.1f, 0.0f});
  fixture.play();
  settle(fixture, character);
  REQUIRE(fixture.state(character).support == platform);

  // The platform moves at 1 m/s, as a script would move it: the character goes with it.
  const auto movePlatform = [&](float speed, int steps) {
    for (int i = 0; i < steps; ++i) {
      platform.get_mut<world::Transform>().position.x += speed * Step;
      fixture.step();
    }
  };
  const glm::vec3 start = positionOf(character);
  movePlatform(1.0f, 60);
  const glm::vec3 rode = positionOf(character) - start;
  CAPTURE(rode.x, rode.y);
  REQUIRE(rode.x == Approx(1.0f).margin(0.03f));
  REQUIRE(rode.y == Approx(0.0f).margin(0.01f));
  REQUIRE(fixture.state(character).grounded);
  REQUIRE(fixture.state(character).supportVelocity.x == Approx(1.0f).margin(0.01f));
  REQUIRE(fixture.state(character).velocity.x == Approx(1.0f).margin(0.02f));

  // Jumping off keeps the platform's velocity in the air after the platform stops.
  REQUIRE(fixture.physics->jumpCharacter(character, 3.0f));
  movePlatform(1.0f, 1);
  movePlatform(0.0f, 10);
  REQUIRE_FALSE(fixture.state(character).grounded);
  REQUIRE_FALSE(fixture.state(character).support);
  REQUIRE(fixture.state(character).velocity.x == Approx(1.0f).margin(0.01f));
  // Landing on the stopped platform replaces it: the character stands still again.
  fixture.run(1.0f);
  REQUIRE(fixture.state(character).grounded);
  REQUIRE(std::abs(fixture.state(character).velocity.x) < 0.01f);

  // Walking off the platform's edge while it moves keeps its velocity through the fall too, once
  // the capsule has slid past the edge (touching it, it slides on steep ground that moves).
  REQUIRE(fixture.physics->setCharacterMove(character, {0.0f, 0.0f, 3.0f}));
  bool left = false;
  for (int i = 0; i < 120 && !left; ++i) {
    movePlatform(1.0f, 1);
    left = !fixture.state(character).support;
  }
  REQUIRE(left);
  movePlatform(0.0f, 5);
  REQUIRE(fixture.state(character).velocity.x == Approx(1.0f).margin(0.02f));
}

TEST_CASE("a character pushes a light crate and not a heavy one, with one contact each", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const auto crate = [&](float mass, float z) {
    const flecs::entity entity = fixture.box("Crate", glm::vec3{0.5f}, {2.0f, 0.5f, z});
    entity.set<physics::RigidBody>({.mass = mass});
    return entity;
  };
  const flecs::entity light = crate(20.0f, 0.0f);
  const flecs::entity heavy = crate(1000.0f, 4.0f);
  const flecs::entity pusher = fixture.character({0.0f, 0.0f, 0.0f});
  const flecs::entity blocked = fixture.character({0.0f, 0.0f, 4.0f});
  fixture.play();
  settle(fixture, pusher);
  REQUIRE(fixture.physics->setCharacterMove(pusher, {2.0f, 0.0f, 0.0f}));
  REQUIRE(fixture.physics->setCharacterMove(blocked, {2.0f, 0.0f, 0.0f}));
  std::size_t lightBegins = 0;
  std::size_t lightEnds = 0;
  std::size_t heavyBegins = 0;
  for (int i = 0; i < 120; ++i) {
    fixture.step();
    lightBegins += fixture.countEvents(physics::ContactEventKind::ContactBegin, pusher, light);
    lightEnds += fixture.countEvents(physics::ContactEventKind::ContactEnd, pusher, light);
    heavyBegins += fixture.countEvents(physics::ContactEventKind::ContactBegin, blocked, heavy);
  }
  CAPTURE(positionOf(light).x, positionOf(heavy).x, positionOf(blocked).x);
  REQUIRE(positionOf(light).x > 2.3f);
  REQUIRE(positionOf(heavy).x == Approx(2.0f).margin(0.01f));
  REQUIRE(positionOf(blocked).x == Approx(1.5f - Radius - 0.02f).margin(0.03f));
  // The character's own contact, not also its inner body's.
  REQUIRE(heavyBegins == 1);
  REQUIRE(lightBegins >= 1);
  REQUIRE(lightBegins - lightEnds <= 1);

  // A crate destroyed while touched ends the contact once.
  fixture.world.destroyEntity(heavy);
  fixture.step();
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactEnd, blocked, heavy) == 1);
  fixture.step();
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactEnd, blocked, heavy) == 0);
}

TEST_CASE("a trigger sees a character enter and leave once and does not stop it", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity zone = fixture.box("Zone", {0.5f, 1.0f, 3.0f}, {3.0f, 1.0f, 0.0f});
  zone.add<physics::Trigger>();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  // Long enough at rest for a body to fall asleep: the inner body must not.
  fixture.run(2.0f);
  REQUIRE(fixture.physics->setCharacterMove(character, {2.0f, 0.0f, 0.0f}));
  std::size_t enters = 0;
  std::size_t exits = 0;
  std::size_t contacts = 0;
  for (int i = 0; i < 180; ++i) {
    fixture.step();
    enters += fixture.countEvents(physics::ContactEventKind::TriggerEnter, character, zone);
    exits += fixture.countEvents(physics::ContactEventKind::TriggerExit, character, zone);
    contacts += fixture.countEvents(physics::ContactEventKind::ContactBegin, character, zone);
  }
  REQUIRE(positionOf(character).x > 5.0f);
  REQUIRE(enters == 1);
  REQUIRE(exits == 1);
  REQUIRE(contacts == 0);

  // Standing inside it and then destroyed: the trigger sees it leave.
  REQUIRE(fixture.physics->teleportCharacter(character, {3.0f, 0.0f, 0.0f}, 0.0f));
  fixture.step(2);
  fixture.world.destroyEntity(character);
  fixture.step();
  REQUIRE(fixture.countEvents(physics::ContactEventKind::TriggerExit, character, zone) == 1);
}

TEST_CASE("queries see a character's inner body, and ignoring the character skips it", "[physics][character]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, character);

  const glm::vec3 down{0.0f, -1.0f, 0.0f};
  const auto ray = fixture.physics->raycast({0.0f, 5.0f, 0.0f}, down, 10.0f);
  REQUIRE((ray && ray->entity == character));
  REQUIRE(ray->point.y == Approx(1.8f).margin(0.05f));
  const auto ignored = fixture.physics->raycast({0.0f, 5.0f, 0.0f}, down, 10.0f, character);
  REQUIRE((ignored && ignored->entity == ground));

  // A camera's sphere cast towards the character hits it, and passes through when told to ignore
  // it, however close it starts to the capsule.
  const auto cast = fixture.physics->sphereCast({-5.0f, 1.0f, 0.0f}, 0.2f, {1.0f, 0.0f, 0.0f}, 10.0f);
  REQUIRE((cast && cast->entity == character));
  const std::array ignore{character};
  REQUIRE_FALSE(fixture.physics->sphereCast({-5.0f, 1.0f, 0.0f}, 0.2f, {1.0f, 0.0f, 0.0f}, 10.0f, ignore).has_value());
  REQUIRE_FALSE(fixture.physics->sphereCast({-0.4f, 1.0f, 0.0f}, 0.2f, {1.0f, 0.0f, 0.0f}, 1.0f, ignore).has_value());
  REQUIRE_FALSE(fixture.physics->sphereCast({0.0f, 1.0f, 0.0f}, 0.2f, {0.0f, 0.0f, 0.0f}, 0.0f, ignore).has_value());
  REQUIRE(fixture.physics->sphereCast({0.0f, 1.0f, 0.0f}, 0.2f, {0.0f, 0.0f, 0.0f}, 0.0f).has_value());
}

TEST_CASE("teleports and outside writes move a character and clear what it carried", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, character);
  REQUIRE(fixture.physics->setCharacterMove(character, {2.0f, 0.0f, 0.0f}));
  fixture.run(0.5f);
  REQUIRE(fixture.physics->jumpCharacter(character, 5.0f));

  // At once, before any step: the Transform is where it was sent, facing the yaw.
  REQUIRE(fixture.physics->teleportCharacter(character, {10.0f, 2.0f, -3.0f}, std::numbers::pi_v<float> / 2.0f));
  REQUIRE(positionOf(character) == glm::vec3{10.0f, 2.0f, -3.0f});
  const glm::vec3 facing = character.get<world::Transform>().rotation * glm::vec3{0.0f, 0.0f, 1.0f};
  REQUIRE(facing.x == Approx(1.0f).margin(1e-4f));
  const physics::CharacterState cleared = fixture.state(character);
  REQUIRE(cleared.velocity == glm::vec3{0.0f});
  REQUIRE_FALSE(cleared.grounded);
  // No intent, no jump: it falls straight down and stands there.
  fixture.run(1.0f);
  REQUIRE(positionOf(character).x == Approx(10.0f).margin(1e-3f));
  REQUIRE(positionOf(character).z == Approx(-3.0f).margin(1e-3f));
  REQUIRE(positionOf(character).y == Approx(0.0f).margin(0.005f));

  // A Transform written from outside teleports it, its rotation's yaw too.
  REQUIRE(fixture.physics->setCharacterMove(character, {0.0f, 0.0f, 1.0f}));
  character.get_mut<world::Transform>().position = {-4.0f, 0.0f, 0.0f};
  character.get_mut<world::Transform>().rotation = glm::angleAxis(1.0f, glm::vec3{0.0f, 1.0f, 0.0f});
  fixture.run(0.5f);
  REQUIRE(positionOf(character).x == Approx(-4.0f).margin(1e-3f));
  REQUIRE(positionOf(character).z == Approx(0.0f).margin(1e-3f));
  const glm::quat rotation = character.get<world::Transform>().rotation;
  REQUIRE(std::abs(glm::dot(rotation, glm::angleAxis(1.0f, glm::vec3{0.0f, 1.0f, 0.0f}))) ==
          Approx(1.0f).margin(1e-4f));

  // A tilt is no yaw: the controller is rejected until the rotation is upright again.
  character.get_mut<world::Transform>().rotation = glm::angleAxis(0.5f, glm::vec3{1.0f, 0.0f, 0.0f});
  fixture.step();
  REQUIRE_FALSE(fixture.physics->character(character).has_value());
  REQUIRE_FALSE(fixture.physics->jumpCharacter(character, 1.0f));
  character.get_mut<world::Transform>().rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());
}

TEST_CASE("the drawn character moves smoothly at 30, 60 and 144 frames per second", "[physics][character]") {
  for (const float rate : {30.0f, 60.0f, 144.0f}) {
    CAPTURE(rate);
    Fixture fixture;
    fixture.ground();
    const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
    fixture.play();
    settle(fixture, character);
    const float speed = 3.0f;
    REQUIRE(fixture.physics->setCharacterMove(character, {speed, 0.0f, 0.0f}));
    fixture.run(0.5f); // up to speed
    const float frame = 1.0f / rate;
    float previous = positionOf(character).x;
    for (int i = 0; i < static_cast<int>(rate); ++i) {
      fixture.world.progress(frame);
      const float x = positionOf(character).x;
      // Every frame advances by the frame's share of the motion, whether it ran zero, one or two
      // steps.
      REQUIRE(x - previous == Approx(speed * frame).margin(speed * frame * 0.02f));
      REQUIRE(positionOf(character).y == Approx(0.0f).margin(0.005f));
      previous = x;
    }
  }
}

TEST_CASE("commands wait for a step and a jump is consumed by the first of several", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, character);

  // A frame too short for a step: no physics, the jump waits.
  REQUIRE(fixture.physics->jumpCharacter(character, 4.0f));
  fixture.world.progress(Step * 0.25f);
  REQUIRE(fixture.state(character).grounded);
  // A frame of two steps: the first consumes the jump, the second moves on upwards.
  fixture.world.progress(Step * 2.0f);
  const physics::CharacterState up = fixture.state(character);
  REQUIRE_FALSE(up.grounded);
  REQUIRE(up.velocity.y == Approx(4.0f - 2.0f * Gravity * Step).margin(0.05f));

  // Time scale 0 runs no steps: the pose stays where it was drawn.
  fixture.world.setTimeScale(0.0f);
  const glm::vec3 frozen = positionOf(character);
  fixture.run(0.5f);
  REQUIRE(positionOf(character) == frozen);
}

TEST_CASE("configuration changes rebuild a character and invalid ones reject it", "[physics][character]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, character);
  REQUIRE(fixture.physics->setCharacterMove(character, {1.0f, 0.0f, 0.0f}));
  fixture.run(0.5f);
  const glm::vec3 there = positionOf(character);

  // A new radius: rebuilt where it is, its intent and velocity gone, the rays see the new size.
  character.set<physics::CharacterController>({.radius = 0.5f});
  fixture.step();
  REQUIRE(fixture.state(character).velocity.x == Approx(0.0f).margin(1e-3f));
  fixture.run(0.5f);
  REQUIRE(glm::length(positionOf(character) - there) < 0.05f);
  REQUIRE(fixture.state(character).grounded);
  const auto side = fixture.physics->raycast({there.x + 0.42f, 5.0f, there.z}, {0.0f, -1.0f, 0.0f}, 10.0f);
  REQUIRE((side && side->entity == character));

  // Out of range values, a body on the entity, a collider below it or a body above it reject it.
  const auto rejected = [&] {
    fixture.step();
    return !fixture.physics->character(character).has_value() &&
           !fixture.physics->setCharacterMove(character, {1.0f, 0.0f, 0.0f});
  };
  character.set<physics::CharacterController>({.radius = 0.0f});
  REQUIRE(rejected());
  character.set<physics::CharacterController>({.maxSlope = degrees(90.0f)});
  REQUIRE(rejected());
  character.set<physics::CharacterController>({.stepHeight = 2.0f});
  REQUIRE(rejected());
  character.set<physics::CharacterController>({});
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());

  character.set<physics::RigidBody>({});
  REQUIRE(rejected());
  character.remove<physics::RigidBody>();
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());

  const flecs::entity child = fixture.world.createEntity("Hand", character);
  child.set<physics::SphereCollider>({.radius = 0.1f});
  REQUIRE(rejected());
  child.add<world::Disabled>(); // a disabled collider is not in the way
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());
  fixture.world.destroyEntity(child);

  const flecs::entity carrier = fixture.world.createEntity("Carrier");
  carrier.set<physics::RigidBody>({.type = physics::BodyType::Kinematic});
  fixture.world.setParent(character, carrier);
  REQUIRE(rejected());
  fixture.world.setParent(character, {});
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());
  static_cast<void>(ground);
}

TEST_CASE("a character follows its entity's lifetime, enabled state and the play snapshot", "[physics][character]") {
  Fixture fixture;
  const flecs::entity ground = fixture.ground();
  const flecs::entity group = fixture.world.createEntity("Group");
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.world.setParent(character, group);
  const core::Uuid uuid = fixture.world.uuidOf(character);
  const nlohmann::json snapshot = world::saveScene(fixture.world);
  fixture.play();
  settle(fixture, character);
  REQUIRE(fixture.physics->setCharacterMove(character, {1.0f, 0.0f, 0.0f}));
  fixture.run(0.5f);
  const glm::vec3 walked = positionOf(character);
  const glm::vec3 down{0.0f, -1.0f, 0.0f};

  // A disabled ancestor removes it at once: no state, no inner body in the way of a ray, the
  // contact with the ground ended at the next step.
  group.add<world::Disabled>();
  REQUIRE_FALSE(fixture.physics->character(character).has_value());
  REQUIRE(fixture.physics->raycast({walked.x, 5.0f, 0.0f}, down, 10.0f)->entity == ground);
  fixture.step();
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactEnd, character, ground) == 1);
  fixture.run(0.5f);
  REQUIRE(positionOf(character) == walked);

  // Enabled again: a new character where the entity is, without its old intent.
  group.remove<world::Disabled>();
  fixture.run(0.5f);
  REQUIRE(fixture.physics->character(character).has_value());
  REQUIRE(glm::length(positionOf(character) - walked) < 0.01f);
  REQUIRE(fixture.physics->raycast({walked.x, 5.0f, 0.0f}, down, 10.0f)->entity == character);

  // Removing the controller or destroying the entity removes the character and its inner body.
  character.remove<physics::CharacterController>();
  REQUIRE_FALSE(fixture.physics->character(character).has_value());
  REQUIRE(fixture.physics->raycast({walked.x, 5.0f, 0.0f}, down, 10.0f)->entity == ground);
  character.set<physics::CharacterController>({});
  fixture.step();
  REQUIRE(fixture.physics->character(character).has_value());
  fixture.world.destroyEntity(character);
  REQUIRE(fixture.physics->raycast({walked.x, 5.0f, 0.0f}, down, 10.0f)->entity == ground);
  fixture.step();
  REQUIRE(fixture.countEvents(physics::ContactEventKind::ContactEnd, character, ground) == 1);

  // Stopping play reloads the snapshot: a new entity at the authored pose, no character until the
  // next play.
  fixture.world.setPlaying(false);
  fixture.world.clearScene();
  REQUIRE(world::loadScene(fixture.world, snapshot).has_value());
  const flecs::entity reloaded = fixture.world.find(uuid);
  REQUIRE(positionOf(reloaded) == glm::vec3{0.0f});
  REQUIRE_FALSE(fixture.physics->character(reloaded).has_value());
  REQUIRE_FALSE(fixture.physics->raycast({0.0f, 5.0f, 0.0f}, down, 10.0f).has_value());
  fixture.play();
  fixture.step();
  REQUIRE(fixture.physics->character(reloaded).has_value());
}

TEST_CASE("a character under a moving parent is simulated in world space", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity parent = fixture.world.createEntity("Parent");
  parent.set<world::Transform>({.position = {5.0f, 0.0f, 0.0f}});
  const flecs::entity character = fixture.character({0.0f, 0.0f, 0.0f});
  fixture.world.setParent(character, parent);
  character.get_mut<world::Transform>().position = glm::vec3{0.0f};
  fixture.play();
  settle(fixture, character);
  const glm::vec3 world = glm::vec3{world::World::worldMatrix(character)[3]};
  REQUIRE(world.x == Approx(5.0f).margin(1e-3f));
  // The parent moves; the character stays where it is in the world.
  parent.get_mut<world::Transform>().position.x = 8.0f;
  fixture.step(3);
  REQUIRE(glm::vec3{world::World::worldMatrix(character)[3]}.x == Approx(5.0f).margin(1e-3f));
  REQUIRE(positionOf(character).x == Approx(-3.0f).margin(1e-3f));
}

TEST_CASE("two characters block each other through their inner bodies", "[physics][character]") {
  Fixture fixture;
  fixture.ground();
  const flecs::entity left = fixture.character({-2.0f, 0.0f, 0.0f});
  const flecs::entity right = fixture.character({2.0f, 0.0f, 0.0f});
  fixture.play();
  settle(fixture, left);
  REQUIRE(fixture.physics->setCharacterMove(left, {1.5f, 0.0f, 0.0f}));
  REQUIRE(fixture.physics->setCharacterMove(right, {-1.5f, 0.0f, 0.0f}));
  std::size_t begins = 0;
  for (int i = 0; i < 180; ++i) {
    fixture.step();
    begins += fixture.countEvents(physics::ContactEventKind::ContactBegin, left, right);
  }
  // Neither passes the other nor pushes it: each stops at the other's inner body (0.9 of the
  // radius), and the pair begins once although both characters report it.
  const float gap = positionOf(right).x - positionOf(left).x;
  CAPTURE(gap);
  REQUIRE(gap == Approx(Radius + 0.9f * Radius + 0.02f).margin(0.03f));
  REQUIRE(begins == 1);
}

TEST_CASE("debug lines draw a character's capsule in edit and play mode", "[physics][character]") {
  Fixture fixture;
  const flecs::entity character =
      fixture.character({1.0f, 0.0f, 0.0f}, {.radius = 0.4f, .halfHeight = 0.5f, .offset = {0.0f, 0.1f, 0.0f}});
  fixture.world.progress(0.0f);
  std::vector<renderer::DebugLine> lines;
  fixture.physics->debugLines(lines);
  // Two rings, four sides and four half circles.
  REQUIRE(lines.size() == 2 * 24 + 4 + 4 * 12);
  float lowest = std::numeric_limits<float>::max();
  float highest = std::numeric_limits<float>::lowest();
  for (const renderer::DebugLine &line : lines) {
    lowest = std::min({lowest, line.from.y, line.to.y});
    highest = std::max({highest, line.from.y, line.to.y});
  }
  // Its bottom at the origin plus the offset, 1.8 m tall.
  REQUIRE(lowest == Approx(0.1f).margin(1e-3f));
  REQUIRE(highest == Approx(0.1f + 1.8f).margin(1e-3f));
  REQUIRE(lines.front().color.r > lines.front().color.g);

  fixture.play();
  fixture.step();
  lines.clear();
  fixture.physics->debugLines(lines);
  REQUIRE(lines.size() == 2 * 24 + 4 + 4 * 12);
  static_cast<void>(character);
}
