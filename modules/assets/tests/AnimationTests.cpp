#include <sonnet/assets/Animation.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet::assets;
using Catch::Approx;

TEST_CASE("channels sample linearly, spherically for rotations, and hold outside their keys", "[assets][animation]") {
  const AnimationChannel translation{.target = "Node",
                                     .path = AnimationPath::Translation,
                                     .interpolation = Interpolation::Linear,
                                     .times = {1.0f, 3.0f},
                                     .values = {{0.0f, 0.0f, 0.0f, 0.0f}, {4.0f, 2.0f, 0.0f, 0.0f}}};
  REQUIRE(sample(translation, 2.0f).x == Approx(2.0f));
  REQUIRE(sample(translation, 2.0f).y == Approx(1.0f));
  REQUIRE(sample(translation, 0.0f).x == Approx(0.0f));  // before the first key
  REQUIRE(sample(translation, 10.0f).x == Approx(4.0f)); // after the last

  const glm::quat turn = glm::angleAxis(glm::radians(120.0f), glm::vec3{0.0f, 1.0f, 0.0f});
  const AnimationChannel rotation{.target = "Node",
                                  .path = AnimationPath::Rotation,
                                  .interpolation = Interpolation::Linear,
                                  .times = {0.0f, 1.0f},
                                  .values = {{0.0f, 0.0f, 0.0f, 1.0f}, {turn.x, turn.y, turn.z, turn.w}}};
  // A quarter of the way along the arc is a 30 degree turn, which a component-wise blend would
  // not give.
  const glm::vec4 middle = sample(rotation, 0.25f);
  const glm::quat expected = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 1.0f, 0.0f});
  REQUIRE(std::abs(glm::dot(glm::quat{middle.w, middle.x, middle.y, middle.z}, expected)) == Approx(1.0f));
  REQUIRE(glm::length(middle) == Approx(1.0f));

  const AnimationChannel empty{};
  REQUIRE(sample(empty, 1.0f) == glm::vec4{0.0f});
}

TEST_CASE("step channels jump at their keys and cubic splines follow their tangents", "[assets][animation]") {
  const AnimationChannel step{.target = "Node",
                              .path = AnimationPath::Scale,
                              .interpolation = Interpolation::Step,
                              .times = {0.0f, 1.0f, 2.0f},
                              .values = {{1.0f, 1.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 2.0f, 0.0f}, {3.0f, 3.0f, 3.0f, 0.0f}}};
  REQUIRE(sample(step, 0.99f).x == Approx(1.0f));
  REQUIRE(sample(step, 1.0f).x == Approx(2.0f));
  REQUIRE(sample(step, 1.5f).x == Approx(2.0f));

  // Flat tangents: an ease between the values, halfway at the middle.
  AnimationChannel flat{.target = "Node",
                        .path = AnimationPath::Translation,
                        .interpolation = Interpolation::CubicSpline,
                        .times = {0.0f, 2.0f},
                        .values = {glm::vec4{0.0f}, glm::vec4{0.0f}, glm::vec4{0.0f}, glm::vec4{0.0f},
                                   glm::vec4{4.0f, 0.0f, 0.0f, 0.0f}, glm::vec4{0.0f}}};
  REQUIRE(sample(flat, 1.0f).x == Approx(2.0f));
  REQUIRE(sample(flat, 0.5f).x < 1.0f); // eased in, behind the linear 1.0
  // Tangents matching the slope make the spline a straight line: 2 per second over the key span.
  flat.values[2] = glm::vec4{2.0f, 0.0f, 0.0f, 0.0f};
  flat.values[3] = glm::vec4{2.0f, 0.0f, 0.0f, 0.0f};
  REQUIRE(sample(flat, 0.5f).x == Approx(1.0f));
  REQUIRE(sample(flat, 2.0f).x == Approx(4.0f));
}
