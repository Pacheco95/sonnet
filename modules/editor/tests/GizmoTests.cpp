#include <sonnet/editor/Gizmo.h>

#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sonnet/renderer/Camera.h>

#include <cmath>

using namespace sonnet;
using Catch::Approx;

namespace {

// A camera three metres in front of the origin looking down -Z over a 400 by 300 viewport.
editor::GizmoView viewAt(glm::vec2 mouse, bool down = false, bool clicked = false) {
  renderer::Camera camera;
  camera.position = {0.0f, 0.0f, 3.0f};
  return editor::GizmoView{.view = camera.view(),
                           .projection = camera.projection(400.0f / 300.0f),
                           .cameraPosition = camera.position,
                           .origin = {100.0f, 50.0f},
                           .size = {400.0f, 300.0f},
                           .mouse = mouse,
                           .mouseDown = down,
                           .mouseClicked = clicked};
}

} // namespace

TEST_CASE("projection and unprojection agree through the viewport rectangle", "[editor][gizmo]") {
  const editor::GizmoView view = viewAt({0.0f, 0.0f});
  const auto centre = editor::Gizmo::project(view, {0.0f, 0.0f, 0.0f});
  REQUIRE(centre.has_value());
  REQUIRE(centre->x == Approx(300.0f));
  REQUIRE(centre->y == Approx(200.0f));
  // +X is to the right on screen, +Y up.
  const auto right = editor::Gizmo::project(view, {1.0f, 0.0f, 0.0f});
  const auto up = editor::Gizmo::project(view, {0.0f, 1.0f, 0.0f});
  REQUIRE(right->x > centre->x);
  REQUIRE(up->y < centre->y);
  REQUIRE(!editor::Gizmo::project(view, {0.0f, 0.0f, 10.0f}).has_value()); // behind the camera

  const glm::vec3 ray = editor::Gizmo::rayDirection(view, *right);
  const auto hit = editor::Gizmo::planeRayHit({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, view.cameraPosition, ray);
  REQUIRE(hit.has_value());
  REQUIRE(hit->x == Approx(1.0f).margin(1e-3f));
  REQUIRE(hit->y == Approx(0.0f).margin(1e-3f));
  REQUIRE(editor::Gizmo::axisRayParam({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, view.cameraPosition, ray) ==
          Approx(1.0f).margin(1e-3f));
  REQUIRE(!editor::Gizmo::planeRayHit({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, view.cameraPosition, {1.0f, 0.0f, 0.0f})
               .has_value());
}

TEST_CASE("a translate drag along X moves the entity by the mouse travel on that axis", "[editor][gizmo]") {
  world::World world;
  const flecs::entity entity = world.createEntity("Box");
  world.progress(0.016f);
  editor::Gizmo gizmo;
  REQUIRE(gizmo.mode() == editor::GizmoMode::Translate);

  // Hover the X handle: a point along +X a little from the origin.
  const editor::GizmoView hover = viewAt(*editor::Gizmo::project(viewAt({0.0f, 0.0f}), {0.3f, 0.0f, 0.0f}));
  editor::GizmoResult result = gizmo.update(hover, world, entity, nullptr);
  REQUIRE(result.hovered);
  REQUIRE(gizmo.hoveredAxis() == editor::GizmoAxis::X);
  REQUIRE(!result.active);

  const editor::GizmoView press = viewAt(hover.mouse, true, true);
  result = gizmo.update(press, world, entity, nullptr);
  REQUIRE(result.active);
  REQUIRE(gizmo.isDragging());

  // Move the mouse to where world X = 1.3 projects: the entity follows by one metre.
  const glm::vec2 target = *editor::Gizmo::project(hover, {1.3f, 0.0f, 0.0f});
  result = gizmo.update(viewAt(target, true, false), world, entity, nullptr);
  REQUIRE(result.active);
  REQUIRE(entity.get<world::Transform>().position.x == Approx(1.0f).margin(1e-3f));
  REQUIRE(entity.get<world::Transform>().position.y == Approx(0.0f).margin(1e-3f));
  // The handles follow the object during the drag, in the same frame, before the transform
  // system has run; the maths stays anchored at the start.
  REQUIRE(gizmo.origin().x == Approx(1.0f).margin(1e-3f));
  // The same mouse position again leaves it in place: the drag is anchored, not accumulated.
  result = gizmo.update(viewAt(target, true, false), world, entity, nullptr);
  REQUIRE(entity.get<world::Transform>().position.x == Approx(1.0f).margin(1e-3f));

  result = gizmo.update(viewAt(target, false, false), world, entity, nullptr);
  REQUIRE(result.finished);
  REQUIRE(result.before.position.x == Approx(0.0f));
  REQUIRE(!gizmo.isDragging());
  REQUIRE(entity.get<world::Transform>().position.x == Approx(1.0f).margin(1e-3f));

  // Away from every handle nothing is hovered, and a null entity resets the gizmo.
  result = gizmo.update(viewAt({120.0f, 60.0f}), world, entity, nullptr);
  REQUIRE(!result.hovered);
  result = gizmo.update(viewAt({120.0f, 60.0f}), world, {}, nullptr);
  REQUIRE(!result.hovered);
}

TEST_CASE("a scale drag changes one axis and a rotate drag turns about it", "[editor][gizmo]") {
  world::World world;
  const flecs::entity entity = world.createEntity("Box");
  world.progress(0.016f);
  editor::Gizmo gizmo;

  gizmo.setMode(editor::GizmoMode::Scale);
  const editor::GizmoView base = viewAt({0.0f, 0.0f});
  const glm::vec2 handle = *editor::Gizmo::project(base, {0.3f, 0.0f, 0.0f});
  gizmo.update(viewAt(handle, true, true), world, entity, nullptr);
  REQUIRE(gizmo.isDragging());
  // Dragging out along +X by the handle's own length doubles the scale on X.
  const float length = glm::distance(base.cameraPosition, glm::vec3{0.0f}) * 0.18f;
  const glm::vec2 farther = *editor::Gizmo::project(base, {0.3f + length, 0.0f, 0.0f});
  gizmo.update(viewAt(farther, true, false), world, entity, nullptr);
  REQUIRE(entity.get<world::Transform>().scale.x == Approx(2.0f).margin(1e-2f));
  REQUIRE(entity.get<world::Transform>().scale.y == Approx(1.0f));
  gizmo.update(viewAt(farther, false, false), world, entity, nullptr);

  gizmo.setMode(editor::GizmoMode::Rotate);
  // The Z circle lies in the screen plane; the point at 45 degrees is on no other circle. Grab
  // it there and drag to 135 degrees for a quarter turn.
  const float diagonal = length * std::sqrt(0.5f);
  const glm::vec2 onCircle = *editor::Gizmo::project(base, {diagonal, diagonal, 0.0f});
  gizmo.update(viewAt(onCircle), world, entity, nullptr);
  REQUIRE(gizmo.hoveredAxis() == editor::GizmoAxis::Z);
  gizmo.update(viewAt(onCircle, true, true), world, entity, nullptr);
  const glm::vec2 quarter = *editor::Gizmo::project(base, {-diagonal, diagonal, 0.0f});
  gizmo.update(viewAt(quarter, true, false), world, entity, nullptr);
  const glm::quat rotation = entity.get<world::Transform>().rotation;
  REQUIRE(glm::angle(rotation) == Approx(glm::radians(90.0f)).margin(1e-2f));
  REQUIRE(std::abs(glm::axis(rotation).z) == Approx(1.0f).margin(1e-3f));
  const editor::GizmoResult finished = gizmo.update(viewAt(quarter, false, false), world, entity, nullptr);
  REQUIRE(finished.finished);
  REQUIRE(finished.before.rotation == glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
}
