#include <sonnet/world/DrawList.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace sonnet;
using Catch::Approx;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  assets::AssetDatabase assets{renderer};
  world::World world;
};

} // namespace

TEST_CASE("the draw list resolves every visible mesh renderer through the database", "[world][drawlist]") {
  Fixture fixture;
  world::World &world = fixture.world;
  std::vector<renderer::DrawItem> draws;
  std::vector<glm::mat4> joints;

  const flecs::entity box = world.createEntity("Box");
  box.set<world::MeshRenderer>({.mesh = assets::builtin::box(), .color = {1.0f, 0.0f, 0.0f, 1.0f}});
  box.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
  const flecs::entity sphere = world.createEntity("Sphere");
  sphere.set<world::MeshRenderer>({.mesh = assets::builtin::sphere()});
  const flecs::entity hidden = world.createEntity("Hidden");
  hidden.set<world::MeshRenderer>({.visible = false});
  const flecs::entity disabled = world.createEntity("Disabled");
  disabled.set<world::MeshRenderer>({});
  disabled.add<world::Disabled>();
  const flecs::entity missing = world.createEntity("Missing");
  missing.set<world::MeshRenderer>({.mesh = core::Uuid::generate()});
  world.createEntity("Empty");

  world.progress(0.016f);
  world::buildDrawList(world, fixture.assets, draws, joints);
  REQUIRE(draws.size() == 2); // the missing asset draws nothing
  REQUIRE(joints.empty());
  REQUIRE(draws[0].jointCount == 0);
  const auto boxDraw = std::ranges::find(draws, world::World::pickId(box), &renderer::DrawItem::id);
  REQUIRE(boxDraw != draws.end());
  REQUIRE(boxDraw->mesh == fixture.assets.mesh(assets::builtin::box()));
  REQUIRE(!boxDraw->material.isValid()); // the renderer's default
  REQUIRE(boxDraw->color.r == Approx(1.0f));
  REQUIRE(boxDraw->transform[3].y == Approx(0.5f));
  REQUIRE(std::ranges::find(draws, world::World::pickId(sphere), &renderer::DrawItem::id) != draws.end());
  REQUIRE(world.fromPickId(boxDraw->id) == box);
}

TEST_CASE("the light list holds the point and spot lights placed by their transforms", "[world][drawlist]") {
  world::World world;
  std::vector<renderer::Light> lights;
  const flecs::entity point = world.createEntity("Point");
  point.set<world::PointLight>({.color = {1.0f, 0.5f, 0.0f}, .intensity = 6.0f, .range = 4.0f});
  point.set<world::Transform>({.position = {1.0f, 2.0f, 3.0f}});
  const flecs::entity spot = world.createEntity("Spot");
  spot.set<world::SpotLight>({.range = 8.0f, .innerAngle = 0.2f, .outerAngle = 0.4f});
  // Pitched down 90 degrees: -Z turns into -Y.
  spot.set<world::Transform>({.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f})});
  const flecs::entity off = world.createEntity("Off");
  off.set<world::PointLight>({});
  off.add<world::Disabled>();
  world.progress(0.016f);

  world::buildLightList(world, lights);
  REQUIRE(lights.size() == 2);
  const auto pointLight = std::ranges::find(lights, renderer::LightType::Point, &renderer::Light::type);
  REQUIRE(pointLight != lights.end());
  REQUIRE(pointLight->position == glm::vec3{1.0f, 2.0f, 3.0f});
  REQUIRE(pointLight->intensity == Approx(6.0f));
  REQUIRE(pointLight->range == Approx(4.0f));
  const auto spotLight = std::ranges::find(lights, renderer::LightType::Spot, &renderer::Light::type);
  REQUIRE(spotLight != lights.end());
  REQUIRE(spotLight->direction.y == Approx(-1.0f).margin(1e-5f));
  REQUIRE(spotLight->outerAngle == Approx(0.4f));
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

TEST_CASE("the scene environment needs a loadable map", "[world][drawlist]") {
  Fixture fixture;
  world::World &world = fixture.world;
  REQUIRE(!world::sceneEnvironment(world, fixture.assets).has_value());
  const flecs::entity sky = world.createEntity("Sky");
  sky.set<world::Environment>({.map = core::Uuid::generate(), .intensity = 2.0f, .exposure = 0.5f});
  // An unknown map is no environment: the renderer falls back to its ambient term.
  REQUIRE(!world::sceneEnvironment(world, fixture.assets).has_value());
}
