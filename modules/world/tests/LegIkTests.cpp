#include <sonnet/world/LegIk.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using namespace sonnet;
using Catch::Approx;

namespace {

// A leg standing on the floor: the hip a metre up, the knee bent forward (-Z) and the ankle 8 cm up.
// Thigh 0.510 m, calf 0.432 m, 0.942 m in all.
const glm::vec3 Hip{0.0f, 1.0f, 0.0f};
const glm::vec3 Knee{0.0f, 0.5f, -0.1f};
const glm::vec3 Ankle{0.0f, 0.08f, 0.0f};
const glm::vec3 Forward{0.0f, 0.0f, -1.0f};

// How far the knee leans off the line from the hip to the target, along `direction`.
float leans(const glm::vec3 &hip, const glm::vec3 &knee, const glm::vec3 &target, const glm::vec3 &direction) {
  const glm::vec3 axis = glm::normalize(target - hip);
  const glm::vec3 off = (knee - hip) - axis * glm::dot(knee - hip, axis);
  return glm::dot(off, direction);
}

bool sameBits(const glm::vec3 &a, const glm::vec3 &b) {
  return std::memcmp(&a, &b, sizeof(glm::vec3)) == 0;
}

} // namespace

TEST_CASE("a two-bone solve puts the ankle on a reachable target within a millimetre and keeps the bones' lengths",
          "[world][legik]") {
  const float upperLength = glm::length(Knee - Hip);
  const float lowerLength = glm::length(Ankle - Knee);
  const glm::vec3 pole = Knee;
  for (const glm::vec3 &offset :
       {glm::vec3{0.0f, -0.015f, 0.0f}, glm::vec3{0.0f, 0.2f, 0.0f}, glm::vec3{0.1f, 0.1f, 0.2f},
        glm::vec3{-0.15f, 0.3f, -0.1f}, glm::vec3{0.0f, 0.15f, 0.3f}, glm::vec3{0.05f, 0.35f, 0.0f}}) {
    const glm::vec3 target = Ankle + offset;
    CAPTURE(offset.x, offset.y, offset.z);
    const world::TwoBone solved = world::solveTwoBone(Hip, Knee, Ankle, target, pole, 1.0f);
    REQUIRE(solved.reached);
    REQUIRE(glm::length(solved.ankle - target) < 1.0e-3f);
    REQUIRE(glm::length(solved.knee - Hip) == Approx(upperLength).margin(1.0e-4f));
    REQUIRE(glm::length(solved.ankle - solved.knee) == Approx(lowerLength).margin(1.0e-4f));
    // The rotations are what move the joints.
    REQUIRE(glm::length(Hip + solved.upper * (Knee - Hip) - solved.knee) < 1.0e-4f);
    REQUIRE(glm::length(solved.knee + solved.lower * (solved.upper * (Ankle - Knee)) - solved.ankle) < 1.0e-4f);
  }
}

TEST_CASE("a target past the leg's length clamps along the hip-to-target line with the knee on its side",
          "[world][legik]") {
  const float length = glm::length(Knee - Hip) + glm::length(Ankle - Knee);
  const glm::vec3 direction = glm::normalize(glm::vec3{0.3f, -1.0f, -0.2f});
  float last = 0.0f;
  for (const float distance : {0.9f, 0.941f, 0.95f, 1.2f, 3.0f, 100.0f}) {
    CAPTURE(distance);
    const glm::vec3 target = Hip + direction * distance;
    const world::TwoBone solved = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, 1.0f);
    REQUIRE(std::isfinite(solved.ankle.x));
    REQUIRE(std::isfinite(solved.knee.x));
    const float reach = glm::length(solved.ankle - Hip);
    REQUIRE(reach <= 0.999f * length + 1.0e-4f);
    // Along the line to the target, never off it.
    REQUIRE(glm::length(glm::normalize(solved.ankle - Hip) - direction) < 1.0e-4f);
    if (distance > length) {
      REQUIRE_FALSE(solved.reached);
      REQUIRE(reach == Approx(0.999f * length).margin(1.0e-4f));
    }
    // The knee stays forward of the line, where the animated knee was, and the bend only straightens.
    const float lean = leans(Hip, solved.knee, target, Forward);
    REQUIRE(lean > 0.0f);
    if (distance > 0.95f) {
      REQUIRE(lean <= last + 1.0e-4f);
    }
    last = lean;
  }
}

TEST_CASE("a weight of 0 returns the pose bit for bit and a weight between blends the rotations", "[world][legik]") {
  const glm::vec3 target = Ankle + glm::vec3{0.0f, 0.25f, 0.1f};
  const world::TwoBone none = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, 0.0f);
  REQUIRE(sameBits(none.knee, Knee));
  REQUIRE(sameBits(none.ankle, Ankle));
  REQUIRE(none.upper == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  REQUIRE(none.lower == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  const world::TwoBone negative = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, -1.0f);
  REQUIRE(sameBits(negative.ankle, Ankle));
  const world::TwoBone nan = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, std::nanf(""));
  REQUIRE(sameBits(nan.ankle, Ankle));

  const world::TwoBone full = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, 1.0f);
  const world::TwoBone half = world::solveTwoBone(Hip, Knee, Ankle, target, Knee, 0.5f);
  // Half the way: the rotation is about half the angle, and the ankle is between, nearer the target.
  REQUIRE(glm::angle(half.upper) == Approx(0.5f * glm::angle(full.upper)).margin(0.02f));
  const float toTarget = glm::length(half.ankle - target);
  REQUIRE(toTarget < glm::length(Ankle - target));
  REQUIRE(toTarget > glm::length(full.ankle - target));
}

TEST_CASE("the knee leans to the side of the pole", "[world][legik]") {
  const glm::vec3 target = Ankle + glm::vec3{0.0f, 0.3f, 0.0f};
  for (const glm::vec3 &direction :
       {glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{-1.0f, 0.0f, 0.0f}, glm::normalize(glm::vec3{1.0f, 0.0f, -1.0f})}) {
    CAPTURE(direction.x, direction.z);
    const world::TwoBone solved = world::solveTwoBone(Hip, Knee, Ankle, target, Hip + direction, 1.0f);
    REQUIRE(leans(Hip, solved.knee, target, direction) > 0.05f);
    REQUIRE(glm::length(solved.ankle - target) < 1.0e-3f);
  }
}

TEST_CASE("the pole is the animated knee, and the model's forward when the leg is straight", "[world][legik]") {
  const glm::vec3 target = Ankle + glm::vec3{0.0f, 0.1f, 0.0f};
  REQUIRE(world::kneePole(Hip, Knee, target, Forward) == Knee);
  // A straight leg: the knee on the hip-to-target line.
  const glm::vec3 straightKnee = Hip + (Ankle - Hip) * 0.5f;
  const glm::vec3 pole = world::kneePole(Hip, straightKnee, Ankle, Forward);
  REQUIRE(glm::length(pole - (Hip + Forward)) < 1.0e-6f);
  // And the solve bends it forward, finite, with no knee flip across the straight pose.
  for (const float lift : {0.0f, 0.001f, 0.01f, 0.1f}) {
    const glm::vec3 up = Ankle + glm::vec3{0.0f, lift, 0.0f};
    const world::TwoBone solved = world::solveTwoBone(Hip, straightKnee, Ankle, up, pole, 1.0f);
    REQUIRE(std::isfinite(solved.knee.x));
    REQUIRE(solved.knee.z <= 1.0e-4f);
  }
}

TEST_CASE("the knee stays a hinge: the lower bone turns about the bend's axis", "[world][legik]") {
  // A thigh turned out to the side, so the bend's plane is not the one through the vertical.
  const glm::vec3 hip{0.0f, 1.0f, 0.0f};
  const glm::vec3 knee{0.12f, 0.55f, -0.08f};
  const glm::vec3 ankle{0.1f, 0.1f, 0.0f};
  const glm::vec3 target = ankle + glm::vec3{0.0f, 0.2f, 0.05f};
  const world::TwoBone solved = world::solveTwoBone(hip, knee, ankle, target, knee, 1.0f);
  REQUIRE(glm::length(solved.ankle - target) < 1.0e-3f);
  const glm::vec3 normal = glm::normalize(glm::cross(solved.knee - hip, solved.ankle - solved.knee));
  const float angle = glm::angle(solved.lower);
  if (angle > 1.0e-3f) {
    REQUIRE(std::abs(glm::dot(glm::axis(solved.lower), normal)) > 0.999f);
  }
  // The bend keeps its handedness: the same side as before.
  const glm::vec3 oldNormal = glm::normalize(glm::cross(knee - hip, ankle - knee));
  REQUIRE(glm::dot(normal, oldNormal) > 0.9f);
}

TEST_CASE("a foot tilts to the ground's normal, held to the limit and scaled by its weight", "[world][legik]") {
  const glm::vec3 up{0.0f, 1.0f, 0.0f};
  const glm::vec3 slope = glm::normalize(glm::vec3{std::sin(glm::radians(20.0f)), std::cos(glm::radians(20.0f)), 0.0f});
  const glm::quat full = world::footTilt(up, slope, glm::radians(30.0f), 1.0f);
  REQUIRE(glm::length(full * up - slope) < 1.0e-5f);
  const glm::quat half = world::footTilt(up, slope, glm::radians(30.0f), 0.5f);
  REQUIRE(glm::degrees(glm::angle(half)) == Approx(10.0f).margin(0.01f));

  const glm::vec3 steep = glm::normalize(glm::vec3{0.0f, std::cos(glm::radians(50.0f)), std::sin(glm::radians(50.0f))});
  const glm::quat held = world::footTilt(up, steep, glm::radians(30.0f), 1.0f);
  REQUIRE(glm::degrees(glm::angle(held)) == Approx(30.0f).margin(0.01f));
  REQUIRE(glm::dot(held * up, steep) > std::cos(glm::radians(20.0f)) - 1.0e-4f);

  REQUIRE(world::footTilt(up, up, glm::radians(30.0f), 1.0f) == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  REQUIRE(world::footTilt(up, slope, glm::radians(30.0f), 0.0f) == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  REQUIRE(world::footTilt(up, glm::vec3{0.0f}, glm::radians(30.0f), 1.0f) == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
  // Straight down: any way round is a half turn, finite.
  const glm::quat upside = world::footTilt(up, -up, glm::radians(30.0f), 1.0f);
  REQUIRE(glm::degrees(glm::angle(upside)) == Approx(30.0f).margin(0.01f));
}
