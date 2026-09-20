#include <sonnet/core/JobSystem.h>
#include <sonnet/world/Animation.h>
#include <sonnet/world/DrawList.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>

using namespace sonnet;
using Catch::Approx;

namespace {

// Nodes Rig, Rig/Root, Rig/Root/Tip (one metre up) and Rig/Strip, a quad two metres high skinned
// to Root at the bottom and Tip at the top; the clip "Bend" turns Tip a quarter about Z over one
// second and moves Root to x = 1 at half a second, in a step.
void writeRig(const std::filesystem::path &gltf) {
  using nlohmann::json;
  std::vector<std::byte> bin;
  json views = json::array();
  json accessors = json::array();
  const auto add = [&](const void *data, std::size_t bytes, std::size_t count, const char *type, int component) {
    views.push_back(json{{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", bytes}});
    const auto *begin = static_cast<const std::byte *>(data);
    bin.insert(bin.end(), begin, begin + bytes);
    accessors.push_back(
        json{{"bufferView", views.size() - 1}, {"componentType", component}, {"count", count}, {"type", type}});
    return accessors.size() - 1;
  };
  constexpr int Float = 5126;
  constexpr int UnsignedInt = 5125;
  const std::vector<float> positions{-0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 2, 0, 0.5f, 2, 0};
  const std::vector<float> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<std::uint16_t> joints{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<std::uint32_t> indices{0, 1, 3, 0, 3, 2};
  const glm::mat4 identity{1.0f};
  const glm::mat4 tipInverse = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, -1.0f, 0.0f});
  std::vector<float> inverseBinds(32);
  std::memcpy(inverseBinds.data(), glm::value_ptr(identity), 64);
  std::memcpy(inverseBinds.data() + 16, glm::value_ptr(tipInverse), 64);
  const glm::quat quarter = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
  const std::vector<float> times{0.0f, 1.0f};
  const std::vector<float> rotations{0, 0, 0, 1, quarter.x, quarter.y, quarter.z, quarter.w};
  const std::vector<float> steps{0.0f, 0.5f};
  const std::vector<float> translations{0, 0, 0, 1, 0, 0};
  const auto position = add(positions.data(), 48, 4, "VEC3", Float);
  accessors.back()["min"] = json::array({-0.5, 0, 0});
  accessors.back()["max"] = json::array({0.5, 2, 0});
  const auto weight = add(weights.data(), 64, 4, "VEC4", Float);
  const auto joint = add(joints.data(), 32, 4, "VEC4", 5123);
  const auto index = add(indices.data(), 24, 6, "SCALAR", UnsignedInt);
  const auto inverseBind = add(inverseBinds.data(), 128, 2, "MAT4", Float);
  const auto time = add(times.data(), 8, 2, "SCALAR", Float);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({1.0});
  const auto rotation = add(rotations.data(), 32, 2, "VEC4", Float);
  const auto step = add(steps.data(), 8, 2, "SCALAR", Float);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({0.5});
  const auto translation = add(translations.data(), 24, 2, "VEC3", Float);
  const std::string binName = gltf.stem().string() + ".bin";
  REQUIRE(core::writeFile(gltf.parent_path() / binName, bin).has_value());
  const json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", json::array({json{{"name", "Rig"}, {"children", json::array({1, 3})}},
                             json{{"name", "Root"}, {"children", json::array({2})}},
                             json{{"name", "Tip"}, {"translation", json::array({0.0, 1.0, 0.0})}},
                             json{{"name", "Strip"}, {"mesh", 0}, {"skin", 0}}})},
      {"meshes",
       json::array({json{
           {"name", "StripMesh"},
           {"primitives",
            json::array({json{{"attributes", {{"POSITION", position}, {"JOINTS_0", joint}, {"WEIGHTS_0", weight}}},
                              {"indices", index}}})}}})},
      {"skins", json::array({json{{"joints", json::array({1, 2})}, {"inverseBindMatrices", inverseBind}}})},
      {"animations",
       json::array({json{
           {"name", "Bend"},
           {"samplers", json::array({json{{"input", time}, {"output", rotation}},
                                     json{{"input", step}, {"output", translation}, {"interpolation", "STEP"}}})},
           {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}},
                                     json{{"sampler", 1}, {"target", {{"node", 1}, {"path", "translation"}}}}})}}})},
      {"buffers", json::array({json{{"uri", binName}, {"byteLength", bin.size()}}})},
      {"bufferViews", views},
      {"accessors", accessors},
  };
  REQUIRE(core::writeFile(gltf, document.dump()).has_value());
}

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_world_animation";
  core::Uuid model;
  flecs::entity prefab;

  Fixture() {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    writeRig(root / "assets" / "rig.gltf");
    const std::vector<std::string> roots{"assets"};
    assets.open(root, roots);
    const auto models = assets.assets(assets::AssetType::Model);
    REQUIRE(models.size() == 1);
    model = models[0]->uuid;
    const assets::Model *loaded = assets.model(model);
    REQUIRE(loaded != nullptr);
    prefab = world::loadModelPrefab(world, *loaded, model, "Rig");
  }
  ~Fixture() {
    world.clearScene();
    std::filesystem::remove_all(root);
  }

  // The instance's node at a path under it.
  flecs::entity node(flecs::entity instance, std::string_view path) const {
    const flecs::entity found = world.findByPath(instance, path);
    REQUIRE(found);
    return found;
  }
};

float angleAboutZ(const glm::quat &rotation) {
  return glm::degrees(2.0f * std::atan2(rotation.z, rotation.w));
}

} // namespace

TEST_CASE("a model's clip plays on each instance in play mode, looping or stopping at its end", "[world][animation]") {
  Fixture fixture;
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const auto *clipInfo = fixture.assets.assets(assets::AssetType::Animation).front();
  REQUIRE(fixture.prefab.get<world::Animator>().clip == clipInfo->uuid);
  const flecs::entity first = world.instantiate(fixture.prefab, "First");
  const flecs::entity second = world.instantiate(fixture.prefab, "Second");
  const flecs::entity tip = fixture.node(first, "Rig/Root/Tip");
  const flecs::entity root = fixture.node(first, "Rig/Root");

  // Edit mode: nothing moves.
  world.progress(0.5f);
  REQUIRE(first.get<world::Animator>().time == 0.0f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(0.0f).margin(1e-4));

  world.setPlaying(true);
  world.progress(0.25f);
  REQUIRE(first.get<world::Animator>().time == Approx(0.25f));
  REQUIRE(first.owns<world::Animator>()); // its own time, not the prefab's
  REQUIRE(fixture.prefab.get<world::Animator>().time == 0.0f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(22.5f));
  REQUIRE(tip.get<world::Transform>().position.y == Approx(1.0f)); // untouched channels keep theirs
  REQUIRE(root.get<world::Transform>().position.x == Approx(0.0f));
  world.progress(0.5f);
  REQUIRE(root.get<world::Transform>().position.x == Approx(1.0f)); // the step at half a second
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(67.5f));

  // Looping wraps; the other instance keeps its own time.
  world.progress(0.5f);
  REQUIRE(first.get<world::Animator>().time == Approx(0.25f));
  REQUIRE(root.get<world::Transform>().position.x == Approx(0.0f));
  second.set<world::Animator>({.clip = clipInfo->uuid, .time = 0.0f, .speed = 1.0f, .playing = true, .loop = false});
  world.progress(1.5f);
  REQUIRE(second.get<world::Animator>().time == Approx(1.0f));
  REQUIRE_FALSE(second.get<world::Animator>().playing);
  REQUIRE(angleAboutZ(fixture.node(second, "Rig/Root/Tip").get<world::Transform>().rotation) == Approx(90.0f));
  REQUIRE(first.get<world::Animator>().playing);

  // Stopped, a new time still poses: scrubbing.
  second.set<world::Animator>({.clip = clipInfo->uuid, .time = 0.5f, .speed = 1.0f, .playing = false, .loop = false});
  world.progress(0.1f);
  REQUIRE(second.get<world::Animator>().time == Approx(0.5f));
  REQUIRE(angleAboutZ(fixture.node(second, "Rig/Root/Tip").get<world::Transform>().rotation) == Approx(45.0f));

  // A clip on an entity without the clip's nodes binds nothing and moves nothing.
  const flecs::entity lone = world.createEntity("Lone");
  lone.set<world::Animator>({.clip = clipInfo->uuid});
  world.progress(0.1f);
  REQUIRE(lone.get<world::Animator>().time == Approx(0.1f));
  REQUIRE(lone.get<world::Transform>().position == glm::vec3{0.0f});
}

TEST_CASE("a skinned mesh's pose follows its joints into the draw list, in edit mode too", "[world][animation]") {
  Fixture fixture;
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Rig 1");
  instance.set<world::Transform>({.position = {5.0f, 0.0f, 0.0f}});
  const flecs::entity strip = fixture.node(instance, "Rig/Strip");
  REQUIRE(strip.has<world::SkinnedMesh>());
  world.progress(0.016f);

  // The bind pose: every joint matrix is the identity, wherever the instance stands.
  REQUIRE(strip.has<world::SkinPose>());
  const std::vector<glm::mat4> &bind = strip.get<world::SkinPose>().joints;
  REQUIRE(bind.size() == 2);
  for (const glm::mat4 &joint : bind) {
    REQUIRE(glm::all(glm::lessThan(glm::abs(joint[3] - glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}), glm::vec4{1e-5f})));
  }

  // Turning Tip a quarter about Z swings the strip's top corner (0.5, 2) about (0, 1).
  fixture.node(instance, "Rig/Root/Tip")
      .set<world::Transform>({.position = {0.0f, 1.0f, 0.0f},
                              .rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f})});
  world.progress(0.016f);
  const glm::vec4 corner = strip.get<world::SkinPose>().joints[1] * glm::vec4{0.5f, 2.0f, 0.0f, 1.0f};
  REQUIRE(corner.x == Approx(-1.0f));
  REQUIRE(corner.y == Approx(1.5f));

  std::vector<renderer::DrawItem> draws;
  std::vector<glm::mat4> joints;
  world::buildDrawList(world, fixture.assets, draws, joints);
  REQUIRE(draws.size() == 1);
  REQUIRE(draws[0].jointCount == 2);
  REQUIRE(draws[0].firstJoint == 0);
  REQUIRE(draws[0].skinInstance == strip.id());
  REQUIRE(joints.size() == 2);
  REQUIRE(draws[0].transform[3].x == Approx(5.0f));

  // A skinned mesh whose joints are nowhere above it draws unposed.
  const flecs::entity lone = world.createEntity("Lone");
  lone.set<world::MeshRenderer>(strip.get<world::MeshRenderer>());
  lone.set<world::SkinnedMesh>(strip.get<world::SkinnedMesh>());
  world.progress(0.016f);
  REQUIRE_FALSE(lone.has<world::SkinPose>());
  world::buildDrawList(world, fixture.assets, draws, joints);
  REQUIRE(draws.size() == 2);
  const auto loneDraw = std::ranges::find(draws, world::World::pickId(lone), &renderer::DrawItem::id);
  REQUIRE(loneDraw->jointCount == 0);
  REQUIRE(loneDraw->skinInstance == 0);
}
