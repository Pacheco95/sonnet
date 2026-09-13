#include <sonnet/renderer/Camera.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet::renderer;
using Catch::Approx;

namespace {

float depthOf(const glm::mat4 &projection, float viewZ) {
  const glm::vec4 clip = projection * glm::vec4{0.0f, 0.0f, viewZ, 1.0f};
  return clip.z / clip.w;
}

} // namespace

TEST_CASE("reversed-Z projection maps the near plane to 1 and infinity towards 0", "[renderer][camera]") {
  const glm::mat4 projection = perspectiveReversedZ(glm::radians(60.0f), 16.0f / 9.0f, 0.1f);
  REQUIRE(depthOf(projection, -0.1f) == Approx(1.0f));
  REQUIRE(depthOf(projection, -1.0f) == Approx(0.1f));
  REQUIRE(depthOf(projection, -1000.0f) == Approx(0.0001f));
  // Nearer fragments have the greater depth, which is what CompareOp::GreaterOrEqual expects.
  REQUIRE(depthOf(projection, -2.0f) > depthOf(projection, -3.0f));
}

TEST_CASE("projection keeps clip x and y inside the frustum edges", "[renderer][camera]") {
  const float fov = glm::radians(90.0f);
  const glm::mat4 projection = perspectiveReversedZ(fov, 1.0f, 0.1f);
  // At 90 degrees the frustum edge at distance 1 is at x = 1.
  const glm::vec4 edge = projection * glm::vec4{1.0f, 0.0f, -1.0f, 1.0f};
  REQUIRE(edge.x / edge.w == Approx(1.0f));
  const glm::vec4 inside = projection * glm::vec4{0.5f, -0.25f, -1.0f, 1.0f};
  REQUIRE(inside.x / inside.w == Approx(0.5f));
  REQUIRE(inside.y / inside.w == Approx(-0.25f));
}

TEST_CASE("an identity camera looks down -Z with +Y up", "[renderer][camera]") {
  const Camera camera;
  REQUIRE(camera.forward() == glm::vec3{0.0f, 0.0f, -1.0f});
  REQUIRE(camera.right() == glm::vec3{1.0f, 0.0f, 0.0f});
  REQUIRE(camera.up() == glm::vec3{0.0f, 1.0f, 0.0f});
  // A point ahead of the camera ends up in front (negative view z), at the same distance.
  const glm::vec4 ahead = camera.view() * glm::vec4{0.0f, 0.0f, -5.0f, 1.0f};
  REQUIRE(ahead.z == Approx(-5.0f));
}

TEST_CASE("a rotated camera rotates its basis", "[renderer][camera]") {
  Camera camera;
  camera.position = {1.0f, 2.0f, 3.0f};
  camera.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}); // yaw left
  const glm::vec3 forward = camera.forward();
  REQUIRE(forward.x == Approx(-1.0f).margin(1e-5f));
  REQUIRE(forward.z == Approx(0.0f).margin(1e-5f));
  const glm::vec4 origin = camera.view() * glm::vec4{camera.position, 1.0f};
  REQUIRE(glm::length(glm::vec3{origin}) == Approx(0.0f).margin(1e-5f));
}
