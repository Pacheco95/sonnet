#include <sonnet/editor/CameraLines.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>

using namespace sonnet;
using Catch::Approx;

TEST_CASE("a camera's frustum opens at its field of view and aspect", "[editor][camera]") {
  // Turned to look down -X: forward (-1, 0, 0), right (0, 0, -1), up (0, 1, 0).
  const renderer::Camera camera{.position = {1.0f, 2.0f, 3.0f},
                                .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                                .fovY = glm::half_pi<float>(),
                                .nearPlane = 0.5f};
  constexpr float aspect = 2.0f;
  std::vector<renderer::DebugLine> lines;
  editor::appendCameraFrustumLines(std::span{&camera, 1}, aspect, lines);
  REQUIRE(lines.size() == 12);

  int fromCamera = 0;
  int nearEdges = 0;
  int farEdges = 0;
  const float farDistance = camera.nearPlane + editor::CameraGizmoLength;
  for (const renderer::DebugLine &line : lines) {
    const glm::vec3 to = line.to - camera.position;
    if (line.from == camera.position) {
      ++fromCamera;
      // At 90 degrees the half-height equals the distance, and the half-width is twice that.
      REQUIRE(-to.x == Approx(farDistance));
      REQUIRE(std::abs(to.y) == Approx(farDistance));
      REQUIRE(std::abs(to.z) == Approx(farDistance * aspect));
      continue;
    }
    // A rectangle's edges lie in one cross-section of the view.
    const float depth = -(line.from - camera.position).x;
    REQUIRE(-to.x == Approx(depth));
    if (depth == Approx(camera.nearPlane)) {
      ++nearEdges;
      REQUIRE(std::abs(to.y) == Approx(camera.nearPlane));
    } else {
      REQUIRE(depth == Approx(farDistance));
      ++farEdges;
    }
  }
  REQUIRE(fromCamera == 4);
  REQUIRE(nearEdges == 4);
  REQUIRE(farEdges == 4);
}

TEST_CASE("every camera gets a frustum", "[editor][camera]") {
  const std::array<renderer::Camera, 2> cameras{};
  std::vector<renderer::DebugLine> lines;
  editor::appendCameraFrustumLines(cameras, 1.0f, lines);
  REQUIRE(lines.size() == 24);
}

TEST_CASE("an orthographic camera gizmo has parallel sides", "[editor][camera]") {
  const renderer::Camera camera{
      .nearPlane = 0.5f, .projectionKind = renderer::ProjectionKind::Orthographic, .orthoHeight = 4.0f};
  std::vector<renderer::DebugLine> lines;
  editor::appendCameraFrustumLines(std::span{&camera, 1}, 2.0f, lines);
  REQUIRE(lines.size() == 12);
  for (std::size_t i = 8; i < 12; ++i) {
    REQUIRE(lines[i].from.x == Approx(lines[i].to.x));
    REQUIRE(lines[i].from.y == Approx(lines[i].to.y));
    REQUIRE(glm::length(lines[i].to - lines[i].from) == Approx(editor::CameraGizmoLength));
    REQUIRE(std::abs(lines[i].from.x) == Approx(4.0f));
    REQUIRE(std::abs(lines[i].from.y) == Approx(2.0f));
  }
}
