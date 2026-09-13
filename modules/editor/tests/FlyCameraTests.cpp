#include <sonnet/editor/FlyCamera.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet::editor;
using Catch::Approx;

namespace {

void requireClose(glm::vec3 a, glm::vec3 b, float margin = 1e-4f) {
  REQUIRE(a.x == Approx(b.x).margin(margin));
  REQUIRE(a.y == Approx(b.y).margin(margin));
  REQUIRE(a.z == Approx(b.z).margin(margin));
}

} // namespace

TEST_CASE("lookAt points the camera at the target", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f});
  requireClose(camera.camera().forward(), {0.0f, 0.0f, -1.0f});
  REQUIRE(camera.yaw() == Approx(0.0f).margin(1e-5f));
  REQUIRE(camera.pitch() == Approx(0.0f).margin(1e-5f));

  camera.lookAt({0.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 0.0f}); // straight down, clamped to the limit
  REQUIRE(camera.pitch() == Approx(-glm::radians(89.0f)));

  camera.lookAt({3.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f});
  requireClose(camera.camera().forward(), glm::normalize(glm::vec3{-1.0f, 0.0f, -1.0f}));
}

TEST_CASE("keys move along the camera axes and the world up", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  camera.update(0.5f, {.forward = true});
  requireClose(camera.camera().position, {0.0f, 0.0f, -0.5f * camera.speed()});
  camera.update(0.5f, {.back = true});
  requireClose(camera.camera().position, {0.0f, 0.0f, 0.0f});
  camera.update(1.0f, {.right = true});
  requireClose(camera.camera().position, {camera.speed(), 0.0f, 0.0f});
  camera.update(1.0f, {.left = true, .up = true});
  // Two directions at once are normalised so diagonal movement is not faster.
  requireClose(camera.camera().position, {camera.speed() * (1.0f - 0.70710678f), camera.speed() * 0.70710678f, 0.0f});
  camera.update(1.0f, {.down = true, .fast = true});
  REQUIRE(camera.camera().position.y == Approx(camera.speed() * 0.70710678f - 4.0f * camera.speed()));
}

TEST_CASE("mouse look turns and pitch is clamped", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  camera.update(0.016f, {.lookDelta = {100.0f, 0.0f}}); // mouse right turns right
  REQUIRE(camera.yaw() < 0.0f);
  REQUIRE(camera.camera().forward().x > 0.0f);
  camera.update(0.016f, {.lookDelta = {0.0f, -10000.0f}}); // mouse up looks up, but not past the limit
  REQUIRE(camera.pitch() == Approx(glm::radians(89.0f)));
  REQUIRE(camera.camera().forward().y > 0.99f);
  // The horizon stays level: right never gains a vertical component.
  REQUIRE(camera.camera().right().y == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("speed scaling is clamped", "[editor][camera]") {
  FlyCamera camera;
  const float base = camera.speed();
  camera.scaleSpeed(2.0f);
  REQUIRE(camera.speed() == Approx(base * 2.0f));
  camera.scaleSpeed(1000.0f);
  REQUIRE(camera.speed() == Approx(200.0f));
  camera.scaleSpeed(0.0001f);
  REQUIRE(camera.speed() == Approx(0.1f));
}
