#include <sonnet/core/JobSystem.h>
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
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
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
  std::vector<float> morphWeights;
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
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

TEST_CASE("a draw item carries the world matrix of the frame before", "[world][drawlist]") {
  Fixture fixture;
  world::World &world = fixture.world;
  std::vector<renderer::DrawItem> draws;
  std::vector<glm::mat4> joints;
  std::vector<float> morphWeights;

  const flecs::entity box = world.createEntity("Box");
  box.set<world::MeshRenderer>({.mesh = assets::builtin::box()});
  box.set<world::Transform>({.position = {1.0f, 0.0f, 0.0f}});

  // The frame an entity appears in, it has not moved.
  world.progress(0.016f);
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
  REQUIRE(draws.size() == 1);
  REQUIRE(draws[0].previousTransform.has_value());
  REQUIRE(draws[0].previousTransform->operator[](3).x == Approx(1.0f));
  REQUIRE(draws[0].transform[3].x == Approx(1.0f));

  // Moved: the item holds both the new matrix and the one it replaced.
  box.set<world::Transform>({.position = {1.5f, 0.0f, 0.0f}});
  world.progress(0.016f);
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
  REQUIRE(draws[0].transform[3].x == Approx(1.5f));
  REQUIRE(draws[0].previousTransform->operator[](3).x == Approx(1.0f));

  // Still afterwards: the two agree again.
  world.progress(0.016f);
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
  REQUIRE(draws[0].previousTransform->operator[](3).x == Approx(1.5f));

  // A child follows its parent's motion, since the previous matrix is the world one.
  const flecs::entity child = world.createEntity("Child", box);
  child.set<world::MeshRenderer>({.mesh = assets::builtin::box()});
  child.set<world::Transform>({.position = {0.0f, 2.0f, 0.0f}});
  world.progress(0.016f);
  box.set<world::Transform>({.position = {2.5f, 0.0f, 0.0f}});
  world.progress(0.016f);
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
  const auto childDraw = std::ranges::find(draws, world::World::pickId(child), &renderer::DrawItem::id);
  REQUIRE(childDraw != draws.end());
  REQUIRE(childDraw->transform[3].x == Approx(2.5f));
  REQUIRE(childDraw->previousTransform->operator[](3).x == Approx(1.5f));
}

TEST_CASE("the light list holds the point and spot lights placed by their transforms", "[world][drawlist]") {
  world::World world;
  std::vector<renderer::Light> lights;
  const flecs::entity point = world.createEntity("Point");
  point.set<world::PointLight>({.color = {1.0f, 0.5f, 0.0f}, .intensity = 6.0f, .range = 4.0f, .castsShadows = true});
  point.set<world::Transform>({.position = {1.0f, 2.0f, 3.0f}});
  const flecs::entity spot = world.createEntity("Spot");
  spot.set<world::SpotLight>({.range = 8.0f, .innerAngle = 0.2f, .outerAngle = 0.4f, .castsShadows = true});
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
  REQUIRE(spotLight->castsShadows);
  REQUIRE(pointLight->castsShadows);
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

TEST_CASE("the particle list holds the enabled emitters, simulating in play mode", "[world][drawlist][particles]") {
  Fixture fixture;
  world::World &world = fixture.world;
  std::vector<renderer::ParticleEmitterItem> emitters;

  const flecs::entity fire = world.createEntity("Fire");
  fire.set<world::Transform>({.position = {1.0f, 2.0f, 3.0f}});
  fire.set<world::ParticleEmitter>({.rate = 80.0f,
                                    .burst = 10,
                                    .lifetimeMin = 3.0f,
                                    .lifetimeMax = 1.0f, // out of order: the list never has max below min
                                    .colorStart = {4.0f, 2.0f, 1.0f, 1.0f},
                                    .blend = world::ParticleBlendMode::Additive,
                                    .space = world::ParticleSpace::Local,
                                    .seed = 9});
  const flecs::entity off = world.createEntity("Off");
  off.set<world::ParticleEmitter>({.playing = false});
  const flecs::entity disabled = world.createEntity("Disabled");
  disabled.set<world::ParticleEmitter>({});
  disabled.add<world::Disabled>();

  world.progress(0.016f);
  world::buildParticleList(world, fixture.assets, emitters);
  REQUIRE(emitters.size() == 2);
  const auto item = std::ranges::find(emitters, fire.id(), &renderer::ParticleEmitterItem::key);
  REQUIRE(item != emitters.end());
  REQUIRE(item->transform[3].y == Approx(2.0f));
  REQUIRE(item->id == world::World::pickId(fire));
  REQUIRE(item->rate == Approx(80.0f));
  REQUIRE(item->burst == 10);
  REQUIRE(item->lifetime.y == Approx(3.0f));
  REQUIRE(item->colorStart.x == Approx(4.0f));
  REQUIRE(item->blend == renderer::ParticleBlend::Additive);
  REQUIRE(item->local);
  REQUIRE(item->seed == 9);
  REQUIRE_FALSE(item->texture.isValid());
  REQUIRE_FALSE(item->simulate); // edit mode

  world.setPlaying(true);
  world.progress(0.016f);
  world::buildParticleList(world, fixture.assets, emitters);
  const auto playing = std::ranges::find(emitters, fire.id(), &renderer::ParticleEmitterItem::key);
  REQUIRE(playing->simulate);
  const auto stopped = std::ranges::find(emitters, off.id(), &renderer::ParticleEmitterItem::key);
  REQUIRE_FALSE(stopped->simulate);

  // Saved with its fields and loaded back.
  const nlohmann::json saved = world.componentToJson(fire, world.ecs().id<world::ParticleEmitter>());
  REQUIRE(saved["blend"] == "Additive");
  REQUIRE(saved["burst"] == 10);
  const flecs::entity copy = world.createEntity("Copy");
  world.componentFromJson(copy, world.ecs().id<world::ParticleEmitter>(), saved);
  REQUIRE(copy.get<world::ParticleEmitter>().space == world::ParticleSpace::Local);
  REQUIRE(copy.get<world::ParticleEmitter>().colorStart.x == Approx(4.0f));
}
