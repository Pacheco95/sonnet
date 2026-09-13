#include <sonnet/world/DrawList.h>
#include <sonnet/world/World.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sonnet;
using Catch::Approx;

TEST_CASE("the draw list holds every visible mesh renderer with its pick id", "[world][drawlist]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  world::World world;
  std::vector<renderer::DrawItem> draws;
  {
    const world::PrimitiveMeshes meshes{renderer};
    REQUIRE(renderer.isValid(meshes.mesh(world::Primitive::Capsule)));

    const flecs::entity box = world.createEntity("Box");
    box.set<world::MeshRenderer>({.primitive = world::Primitive::Box, .color = {1.0f, 0.0f, 0.0f, 1.0f}});
    box.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
    const flecs::entity sphere = world.createEntity("Sphere");
    sphere.set<world::MeshRenderer>({.primitive = world::Primitive::Sphere});
    const flecs::entity hidden = world.createEntity("Hidden");
    hidden.set<world::MeshRenderer>({.visible = false});
    const flecs::entity disabled = world.createEntity("Disabled");
    disabled.set<world::MeshRenderer>({});
    disabled.add<world::Disabled>();
    world.createEntity("Empty");

    world.progress(0.016f);
    world::buildDrawList(world, meshes, draws);
    REQUIRE(draws.size() == 2);
    const auto boxDraw = std::ranges::find(draws, world::World::pickId(box), &renderer::DrawItem::id);
    REQUIRE(boxDraw != draws.end());
    REQUIRE(boxDraw->mesh == meshes.mesh(world::Primitive::Box));
    REQUIRE(boxDraw->color.r == Approx(1.0f));
    REQUIRE(boxDraw->transform[3].y == Approx(0.5f));
    REQUIRE(std::ranges::find(draws, world::World::pickId(sphere), &renderer::DrawItem::id) != draws.end());
    REQUIRE(world.fromPickId(boxDraw->id) == box);
  }
  // Meshes are released with PrimitiveMeshes; the renderer reports nothing leaked.
}

TEST_CASE("the scene light and camera come from their entities' transforms", "[world][drawlist]") {
  world::World world;
  REQUIRE(!world::sceneLight(world).has_value());
  REQUIRE(!world::sceneCamera(world).has_value());

  const flecs::entity sun = world.createEntity("Sun");
  sun.set<world::DirectionalLight>({.color = {1.0f, 1.0f, 1.0f}, .intensity = 2.0f});
  // Pitched down 90 degrees: -Z turns into -Y.
  sun.set<world::Transform>({.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f})});
  const flecs::entity camera = world.createEntity("Camera");
  camera.set<world::Camera>({.fovY = glm::radians(50.0f), .nearPlane = 0.5f});
  camera.set<world::Transform>({.position = {0.0f, 1.0f, 5.0f}});
  world.progress(0.016f);

  const auto light = world::sceneLight(world);
  REQUIRE(light.has_value());
  REQUIRE(light->direction.y == Approx(-1.0f).margin(1e-5f));
  REQUIRE(light->intensity == Approx(2.0f));
  const auto view = world::sceneCamera(world);
  REQUIRE(view.has_value());
  REQUIRE(view->position == glm::vec3{0.0f, 1.0f, 5.0f});
  REQUIRE(view->fovY == Approx(glm::radians(50.0f)));
  REQUIRE(view->nearPlane == Approx(0.5f));
}
