// Feasibility tests for ADR-0038: what Jolt's CharacterVirtual, at the pinned version, does on its
// own and what the engine's wrapper has to add. They drive Jolt directly, with the same two
// object layers as JoltPhysicsWorld, and a small wrapper that follows the ADR's contract: the
// wrapper owns the velocity (intent, gravity, jump, inherited support velocity), the character
// moves before the rigid-body step, and the inner body is the character's presence in queries.

// Jolt.h configures every other Jolt header and comes first.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using Catch::Approx;

namespace {

constexpr float Step = 1.0f / 60.0f;
constexpr float Gravity = -9.81f;
constexpr float Radius = 0.3f;
constexpr float HalfHeight = 0.6f; // of the cylinder: a 1.8 m capsule

namespace layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace layers

class LayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
  [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
    return 2;
  }
  [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return JPH::BroadPhaseLayer{static_cast<JPH::uint8>(layer)};
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  [[nodiscard]] const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == JPH::BroadPhaseLayer{0} ? "NonMoving" : "Moving";
  }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
    return layer == layers::Moving || broadPhase == JPH::BroadPhaseLayer{layers::Moving};
  }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
  [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a == layers::Moving || b == layers::Moving;
  }
};

// Jolt's process-wide state, registered here unless a physics world in this binary still holds it.
struct JoltRuntime {
  bool owned = JPH::Factory::sInstance == nullptr;

  JoltRuntime() {
    if (owned) {
      JPH::RegisterDefaultAllocator();
      JPH::Factory::sInstance = new JPH::Factory();
      JPH::RegisterTypes();
    }
  }
  ~JoltRuntime() {
    if (owned) {
      JPH::UnregisterTypes();
      delete JPH::Factory::sInstance;
      JPH::Factory::sInstance = nullptr;
    }
  }
  JoltRuntime(const JoltRuntime &) = delete;
  JoltRuntime &operator=(const JoltRuntime &) = delete;
};

// The rigid-body contact listener, as JoltPhysicsWorld's records pairs: only the count matters here.
class PairCounter final : public JPH::ContactListener {
public:
  void OnContactAdded(const JPH::Body &first, const JPH::Body &second, const JPH::ContactManifold &,
                      JPH::ContactSettings &) override {
    added.emplace_back(first.GetID(), second.GetID());
  }
  void OnContactRemoved(const JPH::SubShapeIDPair &pair) override {
    removed.emplace_back(pair.GetBody1ID(), pair.GetBody2ID());
  }

  [[nodiscard]] static bool involves(const std::vector<std::pair<JPH::BodyID, JPH::BodyID>> &list, JPH::BodyID a,
                                     JPH::BodyID b) {
    return std::ranges::any_of(list, [&](const auto &pair) {
      return (pair.first == a && pair.second == b) || (pair.first == b && pair.second == a);
    });
  }

  std::vector<std::pair<JPH::BodyID, JPH::BodyID>> added;
  std::vector<std::pair<JPH::BodyID, JPH::BodyID>> removed;
};

// What the character's own listener reports, which Jolt calls on the thread running ExtendedUpdate.
class CharacterContacts final : public JPH::CharacterContactListener {
public:
  void OnContactAdded(const JPH::CharacterVirtual *, const JPH::CharacterContact &contact,
                      JPH::CharacterContactSettings &) override {
    added.push_back(contact.mBodyB);
  }
  void OnContactRemoved(const JPH::CharacterVirtual *, const JPH::BodyID &body, const JPH::SubShapeID &) override {
    removed.push_back(body);
  }

  std::vector<JPH::BodyID> added;
  std::vector<JPH::BodyID> removed;
};

struct Scene {
  JoltRuntime runtime;
  LayerInterface layerInterface;
  ObjectVsBroadPhaseFilter objectVsBroadPhase;
  ObjectPairFilter objectPairs;
  JPH::TempAllocatorImpl temp{4u << 20};
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem system;
  PairCounter pairs;

  Scene() {
    system.Init(1024, 0, 1024, 1024, layerInterface, objectVsBroadPhase, objectPairs);
    system.SetGravity(JPH::Vec3{0.0f, Gravity, 0.0f});
    system.SetContactListener(&pairs);
  }

  JPH::BodyInterface &bodies() {
    return system.GetBodyInterface();
  }

  JPH::BodyID box(JPH::Vec3 half, JPH::RVec3 position, JPH::Quat rotation = JPH::Quat::sIdentity(),
                  JPH::EMotionType motion = JPH::EMotionType::Static, bool sensor = false) {
    JPH::BodyCreationSettings settings{new JPH::BoxShape{half}, position, rotation, motion,
                                       motion == JPH::EMotionType::Static ? layers::NonMoving : layers::Moving};
    settings.mIsSensor = sensor;
    return bodies().CreateAndAddBody(settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                                                  : JPH::EActivation::Activate);
  }

  // A floor whose top is at y = 0.
  JPH::BodyID floor() {
    return box({50.0f, 0.5f, 50.0f}, {0.0f, -0.5f, 0.0f});
  }

  // A ramp rising along +x at `angle` whose surface starts at (x0, 0).
  JPH::BodyID ramp(float angle, float x0, float length = 10.0f) {
    const JPH::Vec3 along{std::cos(angle), std::sin(angle), 0.0f};
    const JPH::Vec3 normal{-std::sin(angle), std::cos(angle), 0.0f};
    const JPH::Vec3 surfaceCentre = JPH::Vec3{x0, 0.0f, 0.0f} + along * length;
    return box({length, 0.5f, 3.0f}, JPH::RVec3{surfaceCentre - normal * 0.5f},
               JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), angle));
  }

  void stepBodies() {
    system.Update(Step, 1, &temp, &jobs);
  }
};

// The capsule with its bottom at the character's origin, as the ADR's `CharacterController` puts it.
JPH::RefConst<JPH::Shape> capsule(float radius) {
  return JPH::RotatedTranslatedShapeSettings{JPH::Vec3{0.0f, HalfHeight + radius, 0.0f}, JPH::Quat::sIdentity(),
                                             new JPH::CapsuleShape{HalfHeight, radius}}
      .Create()
      .Get();
}

struct CharacterOptions {
  float maxSlope = 50.0f * std::numbers::pi_v<float> / 180.0f;
  float stepHeight = 0.3f;
  float groundSnap = 0.3f;
  bool innerBody = true;
};

// The wrapper the ADR describes, reduced to what the tests need. It owns the velocity: Jolt's
// ExtendedUpdate takes gravity only to press on what the character stands on.
struct Character {
  Scene &scene;
  CharacterOptions options;
  JPH::Ref<JPH::CharacterVirtual> virtualCharacter;
  CharacterContacts contacts;
  JPH::Vec3 move = JPH::Vec3::sZero();      // horizontal intent, held until changed
  std::optional<float> jump;                // one-shot, consumed by the next step
  JPH::Vec3 inherited = JPH::Vec3::sZero(); // horizontal support velocity kept after leaving it

  Character(Scene &owner, JPH::RVec3 position, CharacterOptions settings = {}) : scene(owner), options(settings) {
    JPH::Ref<JPH::CharacterVirtualSettings> config = new JPH::CharacterVirtualSettings;
    config->mShape = capsule(Radius);
    config->mMaxSlopeAngle = options.maxSlope;
    config->mMass = 70.0f;
    config->mMaxStrength = 100.0f;
    // Contacts with the lower half sphere support the character; the rest only collide.
    config->mSupportingVolume = JPH::Plane{JPH::Vec3::sAxisY(), -Radius};
    if (options.innerBody) {
      config->mInnerBodyShape = capsule(Radius * 0.9f);
      config->mInnerBodyLayer = layers::Moving;
    }
    virtualCharacter = new JPH::CharacterVirtual{config, position, JPH::Quat::sIdentity(), 0, &scene.system};
    virtualCharacter->SetListener(&contacts);
  }

  [[nodiscard]] JPH::Vec3 position() const {
    return JPH::Vec3{virtualCharacter->GetPosition()};
  }
  [[nodiscard]] bool walkable() const {
    return virtualCharacter->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
  }

  void step() {
    JPH::CharacterVirtual &character = *virtualCharacter;
    // The support may have changed velocity since the last step (a platform started moving).
    character.UpdateGroundVelocity();
    const JPH::Vec3 up = character.GetUp();
    const JPH::Vec3 ground = character.GetGroundVelocity();
    float nextVertical = character.GetLinearVelocity().Dot(up);
    // Standing, the support's vertical velocity is the character's and gravity adds nothing: a
    // lift that stops does not throw it up, and a walkable slope does not make it creep. A jump
    // is not undone by this, since it leaves walkable ground at once.
    const bool standing = walkable() && !jump;
    if (walkable()) {
      inherited = ground - ground.Dot(up) * up;
    }
    if (standing) {
      nextVertical = ground.Dot(up);
    } else if (jump) {
      // The jump replaces the vertical velocity, so a late (coyote) jump goes as high as one from
      // the ground; on a support it adds to the support's own vertical velocity.
      nextVertical = (walkable() ? ground.Dot(up) : 0.0f) + *jump + Gravity * Step;
      jump.reset();
    } else {
      nextVertical += Gravity * Step;
    }
    character.SetLinearVelocity(inherited + move + nextVertical * up);

    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = JPH::Vec3{0.0f, -options.groundSnap, 0.0f};
    settings.mWalkStairsStepUp = JPH::Vec3{0.0f, options.stepHeight, 0.0f};
    character.ExtendedUpdate(Step, JPH::Vec3{0.0f, Gravity, 0.0f}, settings,
                             scene.system.GetDefaultBroadPhaseLayerFilter(layers::Moving),
                             scene.system.GetDefaultLayerFilter(layers::Moving), {}, {}, scene.temp);
    if (!walkable() && character.IsSupported()) {
      inherited = JPH::Vec3::sZero(); // sliding off something too steep keeps nothing of it
    }
  }
};

// One fixed step in the ADR's order: kinematic bodies are moved, characters move against the
// world as the last step left it, then the rigid bodies step.
void fixedStep(Scene &scene, std::initializer_list<Character *> characters) {
  for (Character *character : characters) {
    character->step();
  }
  scene.stepBodies();
}

} // namespace

TEST_CASE("ExtendedUpdate does not integrate gravity; the wrapper does", "[physics][character]") {
  Scene scene;
  scene.floor();
  JPH::Ref<JPH::CharacterVirtualSettings> config = new JPH::CharacterVirtualSettings;
  config->mShape = capsule(Radius);
  JPH::Ref<JPH::CharacterVirtual> bare =
      new JPH::CharacterVirtual{config, JPH::RVec3{0.0f, 2.0f, 0.0f}, JPH::Quat::sIdentity(), 0, &scene.system};
  for (int i = 0; i < 60; ++i) {
    bare->ExtendedUpdate(Step, JPH::Vec3{0.0f, Gravity, 0.0f}, {},
                         scene.system.GetDefaultBroadPhaseLayerFilter(layers::Moving),
                         scene.system.GetDefaultLayerFilter(layers::Moving), {}, {}, scene.temp);
    scene.stepBodies();
  }
  // A second of "gravity" left it where it was.
  REQUIRE(bare->GetPosition().GetY() == Approx(2.0f).margin(1e-4f));
  REQUIRE(bare->GetGroundState() == JPH::CharacterBase::EGroundState::InAir);

  Character character{scene, JPH::RVec3{3.0f, 2.0f, 0.0f}};
  for (int i = 0; i < 60; ++i) {
    fixedStep(scene, {&character});
  }
  REQUIRE(character.walkable());
  // It rests with its origin, the bottom of the capsule, on the floor, and stays there.
  const float rest = character.position().GetY();
  CAPTURE(rest);
  REQUIRE(rest == Approx(0.0f).margin(0.005f));
  for (int i = 0; i < 60; ++i) {
    fixedStep(scene, {&character});
    REQUIRE(character.walkable());
    REQUIRE(character.position().GetY() == Approx(rest).margin(1e-3f));
  }
}

TEST_CASE("a character climbs a step up to its step height and no higher", "[physics][character]") {
  const auto walkInto = [](float stepTop, float stepHeight) {
    Scene scene;
    scene.floor();
    scene.box({1.0f, stepTop / 2.0f, 3.0f}, {2.0f, stepTop / 2.0f, 0.0f}); // from x = 1 to 3
    Character character{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}, {.stepHeight = stepHeight}};
    character.move = {2.0f, 0.0f, 0.0f};
    for (int i = 0; i < 90; ++i) {
      fixedStep(scene, {&character});
    }
    return character.position();
  };
  const JPH::Vec3 climbed = walkInto(0.25f, 0.3f);
  CAPTURE(climbed.GetX(), climbed.GetY());
  REQUIRE(climbed.GetX() > 1.5f);
  REQUIRE(climbed.GetY() == Approx(0.25f).margin(0.03f));

  const JPH::Vec3 tooHigh = walkInto(0.45f, 0.3f);
  CAPTURE(tooHigh.GetX(), tooHigh.GetY());
  REQUIRE(tooHigh.GetX() < 1.0f - Radius + 0.05f);
  REQUIRE(tooHigh.GetY() < 0.05f);

  // Without stair walking even the low step blocks: the step height is the stair setting alone.
  const JPH::Vec3 noStairs = walkInto(0.25f, 0.0f);
  CAPTURE(noStairs.GetX(), noStairs.GetY());
  REQUIRE(noStairs.GetX() < 1.0f - Radius + 0.05f);
}

TEST_CASE("a character walks up a slope under its maximum and not one over it", "[physics][character]") {
  const auto walkUp = [](float degrees) {
    Scene scene;
    scene.floor();
    scene.ramp(degrees * std::numbers::pi_v<float> / 180.0f, 1.0f);
    Character character{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}};
    character.move = {2.0f, 0.0f, 0.0f};
    for (int i = 0; i < 150; ++i) {
      fixedStep(scene, {&character});
    }
    return character.position().GetY();
  };
  const float gentle = walkUp(30.0f);
  const float steep = walkUp(60.0f);
  CAPTURE(gentle, steep);
  REQUIRE(gentle > 1.0f);
  REQUIRE(steep < 0.35f);
}

TEST_CASE("a character stands on a walkable slope and slides off a steep one", "[physics][character]") {
  const auto settle = [](float degrees) {
    Scene scene;
    scene.floor();
    const float angle = degrees * std::numbers::pi_v<float> / 180.0f;
    scene.ramp(angle, 0.0f, 4.0f);
    // Dropped a little above the ramp, two metres along it.
    const float x = 2.0f * std::cos(angle);
    Character character{scene, JPH::RVec3{x, x * std::tan(angle) + 0.2f, 0.0f}};
    for (int i = 0; i < 30; ++i) {
      fixedStep(scene, {&character});
    }
    const JPH::Vec3 landed = character.position();
    for (int i = 0; i < 120; ++i) {
      fixedStep(scene, {&character});
    }
    return std::pair{landed, character.position()};
  };
  // Gravity builds no velocity while the ground is walkable, so the character does not creep.
  const auto [gentleLanded, gentleAfter] = settle(30.0f);
  const float creep = (gentleAfter - gentleLanded).Length();
  CAPTURE(creep);
  REQUIRE(creep < 0.005f);
  // On ground too steep to stand on, it keeps its vertical velocity and slides to the bottom.
  const auto [steepLanded, steepAfter] = settle(60.0f);
  CAPTURE(steepLanded.GetY(), steepAfter.GetY());
  REQUIRE(steepAfter.GetY() < 0.3f);
}

TEST_CASE("ground snapping keeps a character on a descent and lets it fall off a ledge", "[physics][character]") {
  // Running down a 40 degree ramp at 5 m/s: each step drops 7 cm below where the last one left it.
  const auto airborneSteps = [](float snap) {
    Scene scene;
    scene.floor();
    const float angle = 40.0f * std::numbers::pi_v<float> / 180.0f;
    scene.ramp(angle, 0.0f, 4.0f);
    const float topX = 4.0f * std::cos(angle) - 0.6f;
    Character character{scene, JPH::RVec3{topX, topX * std::tan(angle) + 0.1f, 0.0f}, {.groundSnap = snap}};
    for (int i = 0; i < 30; ++i) {
      fixedStep(scene, {&character});
    }
    REQUIRE(character.walkable());
    character.move = {-5.0f, 0.0f, 0.0f};
    int airborne = 0;
    while (character.position().GetX() > 0.6f) {
      fixedStep(scene, {&character});
      airborne += character.walkable() ? 0 : 1;
    }
    return airborne;
  };
  const int unsnapped = airborneSteps(0.0f);
  const int snapped = airborneSteps(0.3f);
  CAPTURE(unsnapped, snapped);
  REQUIRE(unsnapped > 0);
  REQUIRE(snapped == 0);

  // A 0.6 m ledge is more than the snap distance: walking off it is a fall, not a teleport down.
  Scene scene;
  scene.floor();
  scene.box({2.0f, 0.3f, 3.0f}, {0.0f, 0.3f, 0.0f}); // top at 0.6, edge at x = 2
  Character character{scene, JPH::RVec3{1.0f, 0.6f, 0.0f}};
  for (int i = 0; i < 10; ++i) {
    fixedStep(scene, {&character});
  }
  REQUIRE(character.walkable());
  character.move = {2.0f, 0.0f, 0.0f};
  int airborne = 0;
  for (int i = 0; i < 90; ++i) {
    fixedStep(scene, {&character});
    airborne += character.walkable() ? 0 : 1;
  }
  CAPTURE(airborne);
  REQUIRE(airborne > 3);
  REQUIRE(character.walkable());
  REQUIRE(character.position().GetY() == Approx(0.0f).margin(0.03f));
}

TEST_CASE("a jump leaves walkable ground on the step that consumes it", "[physics][character]") {
  Scene scene;
  scene.floor();
  Character character{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}};
  for (int i = 0; i < 10; ++i) {
    fixedStep(scene, {&character});
  }
  REQUIRE(character.walkable());
  character.jump = 5.0f;
  fixedStep(scene, {&character});
  // Not walkable at once, so a script gating its jumps on walkable ground (or on its own coyote
  // timer started from it) cannot take a second one from the following steps.
  REQUIRE_FALSE(character.walkable());
  float peak = 0.0f;
  int airborne = 1;
  while (!character.walkable() && airborne < 120) {
    fixedStep(scene, {&character});
    peak = std::max(peak, character.position().GetY());
    ++airborne;
  }
  CAPTURE(peak, airborne);
  // v^2 / 2g, less the first step's gravity.
  REQUIRE(peak == Approx(5.0f * 5.0f / (2.0f * -Gravity)).margin(0.1f));
  REQUIRE(character.walkable());
}

TEST_CASE("a character rides a translating platform and keeps its velocity on leaving it", "[physics][character]") {
  Scene scene;
  scene.floor();
  const JPH::BodyID platform =
      scene.box({3.0f, 0.1f, 2.0f}, {0.0f, 1.0f, 0.0f}, JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic);
  Character character{scene, JPH::RVec3{0.0f, 1.1f, 0.0f}};
  const auto movePlatform = [&](JPH::Vec3 velocity) {
    const JPH::RVec3 at = scene.bodies().GetPosition(platform);
    scene.bodies().MoveKinematic(platform, at + velocity * Step, JPH::Quat::sIdentity(), Step);
  };
  for (int i = 0; i < 10; ++i) {
    movePlatform(JPH::Vec3::sZero());
    fixedStep(scene, {&character});
  }
  REQUIRE(character.walkable());
  REQUIRE(character.virtualCharacter->GetGroundBodyID() == platform);

  // Moved in the step's order (the platform's MoveKinematic, the character, then the rigid
  // bodies), the character keeps its place on the platform with no intent of its own.
  const JPH::Vec3 start = character.position();
  for (int i = 0; i < 60; ++i) {
    movePlatform({1.0f, 0.0f, 0.0f});
    fixedStep(scene, {&character});
    REQUIRE(character.walkable());
  }
  const JPH::Vec3 slid = character.position() - start;
  CAPTURE(slid.GetX(), slid.GetY());
  REQUIRE(slid.GetX() == Approx(1.0f).margin(0.02f));
  REQUIRE(slid.GetY() == Approx(0.0f).margin(0.01f));

  // Jumping off it keeps its horizontal velocity in the air after the platform stops.
  character.jump = 4.0f;
  movePlatform({1.0f, 0.0f, 0.0f});
  fixedStep(scene, {&character});
  for (int i = 0; i < 10; ++i) {
    movePlatform(JPH::Vec3::sZero());
    fixedStep(scene, {&character});
  }
  REQUIRE_FALSE(character.walkable());
  CAPTURE(character.virtualCharacter->GetLinearVelocity().GetX());
  REQUIRE(character.virtualCharacter->GetLinearVelocity().GetX() == Approx(1.0f).margin(0.01f));
}

TEST_CASE("a character rides a lift up and stays on it when the lift stops", "[physics][character]") {
  Scene scene;
  scene.floor();
  const JPH::BodyID lift =
      scene.box({1.0f, 0.1f, 1.0f}, {0.0f, 1.0f, 0.0f}, JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic);
  Character character{scene, JPH::RVec3{0.0f, 1.1f, 0.0f}};
  const auto moveLift = [&](float speed) {
    const JPH::RVec3 at = scene.bodies().GetPosition(lift);
    scene.bodies().MoveKinematic(lift, at + JPH::Vec3{0.0f, speed * Step, 0.0f}, JPH::Quat::sIdentity(), Step);
  };
  for (int i = 0; i < 10; ++i) {
    moveLift(0.0f);
    fixedStep(scene, {&character});
  }
  // Up at 2 m/s and then a sudden stop: walkable all the way, at the lift's height, so the
  // support's vertical velocity is the character's while it stands, not inertia it keeps.
  for (int i = 0; i < 120; ++i) {
    moveLift(i < 60 ? 2.0f : 0.0f);
    fixedStep(scene, {&character});
    const float top = static_cast<float>(scene.bodies().GetPosition(lift).GetY()) + 0.1f;
    CAPTURE(i, top, character.position().GetY());
    REQUIRE(character.walkable());
    REQUIRE(character.position().GetY() == Approx(top).margin(0.04f));
  }
}

TEST_CASE("a character pushes light dynamic bodies and not heavy ones", "[physics][character]") {
  Scene scene;
  scene.floor();
  // A 20 kg crate slides (friction about 40 N against a strength of 100 N); a 1000 kg one,
  // Jolt's default density for a cubic metre, does not.
  const auto crate = [&](float mass, float z) {
    JPH::BodyCreationSettings settings{new JPH::BoxShape{JPH::Vec3::sReplicate(0.5f)}, JPH::RVec3{2.0f, 0.5f, z},
                                       JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, layers::Moving};
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
    return scene.bodies().CreateAndAddBody(settings, JPH::EActivation::Activate);
  };
  const JPH::BodyID light = crate(20.0f, 0.0f);
  const JPH::BodyID heavy = crate(1000.0f, 4.0f);
  Character pusher{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}};
  Character blocked{scene, JPH::RVec3{0.0f, 0.0f, 4.0f}};
  pusher.move = {2.0f, 0.0f, 0.0f};
  blocked.move = {2.0f, 0.0f, 0.0f};
  for (int i = 0; i < 120; ++i) {
    fixedStep(scene, {&pusher, &blocked});
  }
  const float lightMoved = static_cast<float>(scene.bodies().GetPosition(light).GetX()) - 2.0f;
  const float heavyMoved = static_cast<float>(scene.bodies().GetPosition(heavy).GetX()) - 2.0f;
  CAPTURE(lightMoved, heavyMoved, blocked.position().GetX());
  REQUIRE(lightMoved > 0.3f);
  REQUIRE(heavyMoved < 0.01f);
  REQUIRE(blocked.position().GetX() < 1.5f - Radius + 0.05f);
}

TEST_CASE("the inner body is the character's presence in queries and ignoring it skips it", "[physics][character]") {
  Scene scene;
  scene.floor();
  Character character{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}};
  Character virtualOnly{scene, JPH::RVec3{5.0f, 0.0f, 0.0f}, {.innerBody = false}};
  for (int i = 0; i < 10; ++i) {
    fixedStep(scene, {&character, &virtualOnly});
  }
  const JPH::BodyID inner = character.virtualCharacter->GetInnerBodyID();
  REQUIRE_FALSE(inner.IsInvalid());
  // The inner body moved with the character, and its own sweeps never hit it: it stands.
  REQUIRE(character.walkable());
  REQUIRE(scene.bodies().GetMotionType(inner) == JPH::EMotionType::Kinematic);

  const auto castDown = [&](float x, const JPH::BodyFilter &filter) {
    JPH::RayCastResult hit;
    const JPH::RRayCast ray{JPH::RVec3{x, 3.0f, 0.0f}, JPH::Vec3{0.0f, -5.0f, 0.0f}};
    const bool found = scene.system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter);
    return found ? std::optional{hit.mBodyID} : std::nullopt;
  };
  REQUIRE(castDown(0.0f, {}) == inner);
  // Without an inner body a CharacterVirtual is invisible to queries: the ray reaches the floor.
  REQUIRE(castDown(5.0f, {}) != std::optional{JPH::BodyID{}});
  REQUIRE(castDown(5.0f, {}) != inner);
  const JPH::IgnoreSingleBodyFilter ignoreInner{inner};
  REQUIRE(castDown(0.0f, ignoreInner) != inner);
}

TEST_CASE("a trigger sees a character's inner body enter and leave", "[physics][character]") {
  Scene scene;
  scene.floor();
  const JPH::BodyID trigger =
      scene.box({0.5f, 1.0f, 3.0f}, {3.0f, 1.0f, 0.0f}, JPH::Quat::sIdentity(), JPH::EMotionType::Static, true);
  Character character{scene, JPH::RVec3{0.0f, 0.0f, 0.0f}};
  const JPH::BodyID inner = character.virtualCharacter->GetInnerBodyID();
  // Jolt moves the inner body with SetPositionAndRotation and never activates it, and a static
  // sensor sees only active bodies, so the wrapper keeps it from sleeping.
  {
    const JPH::BodyLockWrite lock{scene.system.GetBodyLockInterface(), inner};
    REQUIRE(lock.Succeeded());
    lock.GetBody().SetAllowSleeping(false);
  }
  // A rest first, long past Jolt's half second before sleep.
  for (int i = 0; i < 120; ++i) {
    fixedStep(scene, {&character});
  }
  REQUIRE(scene.bodies().IsActive(inner));
  character.move = {2.0f, 0.0f, 0.0f};
  for (int i = 0; i < 180; ++i) {
    fixedStep(scene, {&character});
  }
  REQUIRE(character.position().GetX() > 5.0f);
  REQUIRE(PairCounter::involves(scene.pairs.added, trigger, inner));
  REQUIRE(PairCounter::involves(scene.pairs.removed, trigger, inner));
  // The character's own sweep meets the sensor too, without being blocked by it; the inner body
  // is what the engine's trigger events come from, so the two must not both be published.
  CAPTURE(std::ranges::contains(character.contacts.added, trigger),
          std::ranges::contains(character.contacts.removed, trigger));
  REQUIRE(std::ranges::contains(character.contacts.added, trigger));
}
