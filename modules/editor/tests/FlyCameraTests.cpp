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

TEST_CASE("dolly moves along the view by a fraction of the distance", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
  camera.dolly(1.0f, 10.0f, false); // a tenth of the distance, forward
  requireClose(camera.camera().position, {0.0f, 0.0f, 9.0f});
  camera.dolly(-2.0f, 10.0f, false); // back, two notches
  requireClose(camera.camera().position, {0.0f, 0.0f, 11.0f});
  camera.dolly(1.0f, 10.0f, true); // Shift is four times as far
  requireClose(camera.camera().position, {0.0f, 0.0f, 7.0f});

  camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  camera.dolly(1.0f, 0.1f, false); // a close-up still moves a little
  requireClose(camera.camera().position, {0.0f, 0.0f, -0.05f});

  camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  camera.dolly(1.0f, 0.0f, false); // without a reference the step follows the fly speed
  requireClose(camera.camera().position, {0.0f, 0.0f, -0.2f * camera.speed()});
}

TEST_CASE("pan slides the camera so the point at the pivot distance follows the cursor", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
  const float perPixel = 2.0f * 10.0f * std::tan(camera.camera().fovY * 0.5f) / 800.0f;
  const glm::vec3 offset = camera.pan({100.0f, 50.0f}, 10.0f, 800.0f); // dragging right/down moves the camera left/up
  requireClose(offset, {-100.0f * perPixel, 50.0f * perPixel, 0.0f});
  requireClose(camera.camera().position, glm::vec3{0.0f, 0.0f, 10.0f} + offset);
  requireClose(camera.camera().forward(), {0.0f, 0.0f, -1.0f});
}

TEST_CASE("orbit turns around the pivot at a constant distance", "[editor][camera]") {
  FlyCamera camera;
  const glm::vec3 pivot{1.0f, 2.0f, -3.0f};
  camera.lookAt(pivot + glm::vec3{0.0f, 0.0f, 10.0f}, pivot);
  camera.orbit({100.0f, 0.0f}, pivot); // dragging right turns right, so the camera swings left
  REQUIRE(camera.yaw() < 0.0f);
  REQUIRE(glm::distance(camera.camera().position, pivot) == Approx(10.0f));
  REQUIRE(camera.camera().position.x < pivot.x);
  requireClose(camera.camera().forward(), glm::normalize(pivot - camera.camera().position));

  camera.orbit({-37.0f, 55.0f}, pivot);
  REQUIRE(glm::distance(camera.camera().position, pivot) == Approx(10.0f));
  requireClose(camera.camera().forward(), glm::normalize(pivot - camera.camera().position));
  REQUIRE(camera.camera().right().y == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("orbit clamps the pitch like the mouse look", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
  camera.orbit({0.0f, 10000.0f}, {0.0f, 0.0f, 0.0f}); // mouse down looks down: the camera rises
  REQUIRE(camera.pitch() == Approx(-glm::radians(89.0f)));
  REQUIRE(camera.camera().position.y > 9.9f);
  REQUIRE(glm::length(camera.camera().position) == Approx(10.0f));
}

TEST_CASE("orbit keeps a minimum distance when the camera sits on the pivot", "[editor][camera]") {
  FlyCamera camera;
  camera.lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  camera.orbit({10.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
  REQUIRE(glm::length(camera.camera().position) == Approx(0.05f));
}

TEST_CASE("six axis snaps preserve the pivot scale and give exact bases", "[editor][camera][orthographic]") {
  const AxisView views[] = {AxisView::Front, AxisView::Back, AxisView::Right,
                            AxisView::Left,  AxisView::Top,  AxisView::Bottom};
  const glm::vec3 forwards[] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
  const glm::vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  for (int i = 0; i < 6; ++i) {
    FlyCamera camera;
    const glm::vec3 pivot{1, 2, 3};
    camera.lookAt(pivot + glm::vec3{0, 0, 10}, pivot);
    camera.snap(views[i], pivot);
    REQUIRE(camera.camera().projectionKind == sonnet::renderer::ProjectionKind::Orthographic);
    REQUIRE(camera.camera().orthoHeight == Approx(20.0f * std::tan(camera.camera().fovY * 0.5f)));
    requireClose(camera.camera().forward(), forwards[i]);
    requireClose(camera.camera().up(), ups[i]);
    requireClose(camera.camera().position + camera.camera().forward() * 10.0f, pivot);
    camera.opposite(pivot);
    requireClose(camera.camera().forward(), -forwards[i]);
  }
}

TEST_CASE("auto perspective leaves a snap only when turning and explicit orthographic survives",
          "[editor][camera][orthographic]") {
  FlyCamera camera;
  const glm::vec3 pivot{0};
  camera.lookAt({0, 0, 10}, pivot);
  camera.snap(AxisView::Front, pivot);
  camera.update(0.1f, {});
  REQUIRE(camera.axisView() == AxisView::Front);
  camera.zoom(2.0f, {0.5f, 0.5f}, 1.0f);
  const float height = camera.camera().orthoHeight;
  camera.orbit({10, 5}, pivot);
  REQUIRE(camera.camera().projectionKind == sonnet::renderer::ProjectionKind::Perspective);
  REQUIRE(2.0f * glm::distance(camera.camera().position, pivot) * std::tan(camera.camera().fovY * 0.5f) ==
          Approx(height));
  REQUIRE(camera.axisView() == AxisView::User);
  camera.toggleProjection(pivot);
  camera.orbit({10, 5}, pivot);
  REQUIRE(camera.camera().projectionKind == sonnet::renderer::ProjectionKind::Orthographic);
  camera.snap(AxisView::Top, pivot);
  camera.update(0.1f, {.lookDelta = {10, 5}});
  REQUIRE(camera.camera().projectionKind == sonnet::renderer::ProjectionKind::Orthographic);
}

TEST_CASE("an orthographic render view stands back so the near plane cuts nothing a pan or zoom brings level",
          "[editor][camera][orthographic]") {
  FlyCamera camera;
  camera.snap(AxisView::Front, {0, 0, 0});
  const FlyCamera::RenderView view = camera.renderView();
  const auto &eye = camera.camera();
  REQUIRE(glm::dot(eye.position - view.camera.position, eye.forward()) == Approx(10.0f * eye.orthoHeight));
  REQUIRE(view.depthOffset == Approx(10.0f * eye.orthoHeight));
  REQUIRE(view.camera.farPlane == Approx(eye.farPlane + 10.0f * eye.orthoHeight));
  // The same rays, only started further back.
  const auto a = eye.ray({0.3f, 0.7f}, 2.0f);
  const auto b = view.camera.ray({0.3f, 0.7f}, 2.0f);
  requireClose(a.direction, b.direction);
  const glm::vec3 shift = b.origin - a.origin;
  REQUIRE(glm::length(glm::cross(shift, eye.forward())) == Approx(0.0f).margin(1e-3f));

  camera.toggleProjection({0, 0, 0}); // perspective: untouched
  REQUIRE(camera.renderView().camera.position == camera.camera().position);
  REQUIRE(camera.renderView().depthOffset == camera.dollyDepthOffset());
}

TEST_CASE("orthographic zoom anchors the cursor and keys pan in the view plane", "[editor][camera][orthographic]") {
  FlyCamera camera;
  camera.snap(AxisView::Top, {0, 0, 0});
  const auto before = camera.camera().ray({0.2f, 0.3f}, 2.0f);
  const float height = camera.camera().orthoHeight;
  camera.zoom(1.0f, {0.2f, 0.3f}, 2.0f);
  requireClose(before.origin, camera.camera().ray({0.2f, 0.3f}, 2.0f).origin, 1e-3f);
  REQUIRE(camera.camera().orthoHeight < height);
  const auto position = camera.camera().position;
  camera.update(1.0f, {.forward = true, .right = true});
  const auto movement = camera.camera().position - position;
  REQUIRE(glm::dot(movement, camera.camera().forward()) == Approx(0.0f).margin(1e-5f));
  REQUIRE(glm::length(movement) == Approx(camera.speed()));
}

TEST_CASE("framing an orthographic selection keeps the axis view and fits its size", "[editor][camera][orthographic]") {
  FlyCamera camera;
  camera.snap(AxisView::Top, {0, 0, 0});
  const glm::vec3 target{3, 2, 1};
  camera.frame(target, 4.0f);
  REQUIRE(camera.axisView() == AxisView::Top);
  requireClose(camera.camera().position + camera.camera().forward() * 4.0f, target);
  REQUIRE(camera.camera().orthoHeight == Approx(8.0f * std::tan(camera.camera().fovY * 0.5f)));
}

TEST_CASE("smooth axis snaps keep the pivot centred and reach the exact target", "[editor][camera][smooth-view]") {
  const glm::vec3 pivot{2, 1, -3};
  for (const AxisView axis :
       {AxisView::Front, AxisView::Back, AxisView::Right, AxisView::Left, AxisView::Top, AxisView::Bottom}) {
    FlyCamera camera;
    camera.lookAt(pivot + glm::vec3{5, 3, 7}, pivot);
    const FlyCamera from = camera;
    camera.snap(axis, pivot);
    const auto target = camera.camera();
    camera.transitionFrom(from, pivot, pivot, 0.2f);
    requireClose(camera.camera().position, from.camera().position);
    for (int step = 0; step < 4; ++step) {
      camera.advanceTransition(0.04f);
      REQUIRE(camera.transitioning());
      const auto &pose = camera.camera();
      const glm::vec4 clip = pose.projection(1.5f) * pose.view() * glm::vec4{pivot, 1};
      REQUIRE(clip.x / clip.w == Approx(0.0f).margin(1e-4f));
      REQUIRE(clip.y / clip.w == Approx(0.0f).margin(1e-4f));
    }
    camera.advanceTransition(0.05f);
    REQUIRE(!camera.transitioning());
    requireClose(camera.camera().position, target.position);
    requireClose(camera.camera().forward(), target.forward());
    REQUIRE(camera.camera().projectionKind == target.projectionKind);
    REQUIRE(camera.axisView() == axis);
  }
}

TEST_CASE("smooth projection switches preserve scale on the pivot plane", "[editor][camera][smooth-view]") {
  FlyCamera camera;
  const glm::vec3 pivot{0};
  camera.lookAt({0, 0, 10}, pivot);
  for (int toggle = 0; toggle < 2; ++toggle) {
    const FlyCamera from = camera;
    const auto projectedHeight = [](const sonnet::renderer::Camera &pose) {
      const glm::vec4 clip = pose.projection(1.0f) * pose.view() * glm::vec4{0, 1, 0, 1};
      return clip.y / clip.w;
    };
    const float size = projectedHeight(camera.camera());
    camera.toggleProjection(pivot);
    const auto target = camera.camera();
    camera.transitionFrom(from, pivot, pivot, 0.2f);
    for (int step = 0; step < 21; ++step) {
      camera.advanceTransition(0.01f);
      REQUIRE(projectedHeight(camera.camera()) == Approx(size).margin(1e-5f));
    }
    REQUIRE(camera.camera().projectionKind == target.projectionKind);
    requireClose(camera.camera().position, target.position);
  }
}

TEST_CASE("smooth framing moves the pivot and zero duration is immediate", "[editor][camera][smooth-view]") {
  FlyCamera camera;
  camera.lookAt({0, 0, 10}, {0, 0, 0});
  const FlyCamera from = camera;
  const glm::vec3 targetPivot{4, 2, -1};
  camera.frame(targetPivot, 3.0f);
  const auto target = camera.camera();
  camera.transitionFrom(from, {0, 0, 0}, targetPivot, 0.2f);
  camera.advanceTransition(0.1f);
  requireClose(camera.camera().position, glm::mix(from.camera().position, target.position, 0.5f));
  camera.advanceTransition(1.0f);
  requireClose(camera.camera().position, target.position);
  camera.snap(AxisView::Top, targetPivot);
  const auto snapped = camera.camera();
  camera.transitionFrom(from, {0, 0, 0}, targetPivot, 0.0f);
  REQUIRE(!camera.transitioning());
  requireClose(camera.camera().position, snapped.position);
}

TEST_CASE("navigation cancels a smooth view at its current pose", "[editor][camera][smooth-view]") {
  FlyCamera camera;
  const glm::vec3 pivot{0};
  camera.lookAt({4, 3, 8}, pivot);
  const FlyCamera from = camera;
  camera.snap(AxisView::Top, pivot);
  camera.transitionFrom(from, pivot, pivot, 0.2f);
  camera.advanceTransition(0.1f);
  const auto current = camera.camera();
  SECTION("cancel alone") {
    camera.cancelTransition();
    camera.advanceTransition(1.0f);
    requireClose(camera.camera().position, current.position);
    requireClose(camera.camera().forward(), current.forward());
  }
  SECTION("movement") {
    camera.update(0.1f, {.right = true});
    requireClose(camera.camera().position, current.position + current.right() * camera.speed() * 0.1f);
    requireClose(camera.camera().forward(), current.forward());
  }
  SECTION("mouse look starts without levelling the slerped rotation") {
    camera.update(0.1f, {.lookDelta = {0.001f, 0.001f}});
    requireClose(camera.camera().forward(), current.forward(), 1e-4f);
    requireClose(camera.camera().up(), current.up(), 1e-4f);
  }
  SECTION("wheel") {
    camera.dolly(1.0f, 1.0f, false);
    requireClose(camera.camera().position, current.position + current.forward() * 0.1f);
  }
  REQUIRE(!camera.transitioning());
}

TEST_CASE("a snap after interrupted dolly zoom restores a usable orthographic depth range",
          "[editor][camera][smooth-view]") {
  FlyCamera camera;
  const glm::vec3 pivot{0};
  camera.lookAt({0, 0, 10}, pivot);
  const FlyCamera from = camera;
  camera.toggleProjection(pivot);
  camera.transitionFrom(from, pivot, pivot, 0.2f);
  camera.advanceTransition(0.19f);
  REQUIRE(camera.dollyDepthOffset() > 100.0f);
  camera.cancelTransition();
  const auto interrupted = camera.camera();
  camera.snap(AxisView::Right, pivot);
  REQUIRE(camera.dollyDepthOffset() == 0.0f);
  REQUIRE(glm::distance(camera.camera().position, pivot) < camera.camera().farPlane);
  REQUIRE(camera.camera().orthoHeight ==
          Approx(2.0f * glm::distance(interrupted.position, pivot) * std::tan(interrupted.fovY * 0.5f)).epsilon(1e-4f));
}
