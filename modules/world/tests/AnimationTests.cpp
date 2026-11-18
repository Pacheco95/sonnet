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

#include <TestSupport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

// Nodes Rig, Rig/Root, Rig/Root/Tip (one metre up) and Rig/Strip, a quad two metres high skinned
// to Root at the bottom and Tip at the top; the clip "Bend" turns Tip a quarter about Z over one
// second and moves Root to x = 1 at half a second, in a step. "Reach" turns Tip a quarter the
// other way over one second, "Stride" the same over two seconds with its steps at Bend's phases,
// and "Still", of zero duration, holds Tip at 30 degrees.
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
  constexpr int floatType = 5126;
  constexpr int unsignedIntType = 5125;
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
  const std::vector<float> reverse{0, 0, 0, 1, -quarter.x, -quarter.y, -quarter.z, quarter.w};
  const auto position = add(positions.data(), 48, 4, "VEC3", floatType);
  accessors.back()["min"] = json::array({-0.5, 0, 0});
  accessors.back()["max"] = json::array({0.5, 2, 0});
  const auto weight = add(weights.data(), 64, 4, "VEC4", floatType);
  const auto joint = add(joints.data(), 32, 4, "VEC4", 5123);
  const auto index = add(indices.data(), 24, 6, "SCALAR", unsignedIntType);
  const auto inverseBind = add(inverseBinds.data(), 128, 2, "MAT4", floatType);
  const auto time = add(times.data(), 8, 2, "SCALAR", floatType);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({1.0});
  const auto rotation = add(rotations.data(), 32, 2, "VEC4", floatType);
  const auto step = add(steps.data(), 8, 2, "SCALAR", floatType);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({0.5});
  const auto translation = add(translations.data(), 24, 2, "VEC3", floatType);
  const auto reversed = add(reverse.data(), 32, 2, "VEC4", floatType);
  const std::vector<float> longTimes{0.0f, 2.0f};
  const auto longTime = add(longTimes.data(), 8, 2, "SCALAR", floatType);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({2.0});
  const std::vector<float> stillTimes{0.0f};
  const auto stillTime = add(stillTimes.data(), 4, 1, "SCALAR", floatType);
  accessors.back()["min"] = json::array({0.0});
  accessors.back()["max"] = json::array({0.0});
  const glm::quat thirty = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f});
  const std::vector<float> stillRotation{thirty.x, thirty.y, thirty.z, thirty.w};
  const auto still = add(stillRotation.data(), 16, 1, "VEC4", floatType);
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
       json::array(
           {json{{"name", "Bend"},
                 {"extras",
                  {{"events", json::array({json{{"time", 0.3}, {"name", "step"}, {"argument", "left"}},
                                           json{{"time", 0.8}, {"name", "step"}, {"argument", "right"}}})}}},
                 {"samplers", json::array({json{{"input", time}, {"output", rotation}},
                                           json{{"input", step}, {"output", translation}, {"interpolation", "STEP"}}})},
                 {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}},
                                           json{{"sampler", 1}, {"target", {{"node", 1}, {"path", "translation"}}}}})}},
            json{{"name", "Reach"},
                 {"samplers", json::array({json{{"input", time}, {"output", reversed}}})},
                 {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}})}},
            json{{"name", "Stride"},
                 {"extras",
                  {{"events", json::array({json{{"time", 0.6}, {"name", "step"}, {"argument", "left"}},
                                           json{{"time", 1.6}, {"name", "step"}, {"argument", "right"}}})}}},
                 {"samplers", json::array({json{{"input", longTime}, {"output", reversed}}})},
                 {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}})}},
            json{{"name", "Still"},
                 {"samplers", json::array({json{{"input", stillTime}, {"output", still}}})},
                 {"channels", json::array({json{{"sampler", 0}, {"target", {{"node", 2}, {"path", "rotation"}}}}})}}})},
      {"buffers", json::array({json{{"uri", binName}, {"byteLength", bin.size()}}})},
      {"bufferViews", views},
      {"accessors", accessors},
  };
  REQUIRE(core::writeFile(gltf, document.dump()).has_value());
}

// A humanoid's legs (ADR-0042): Rig/pelvis a metre up with thigh_l and thigh_r under it, each with a
// calf 0.5 m down and a bit forward (-Z), a foot 0.42 m below that, back at 8 cm, and a ball under
// the foot. The skin binds every one of them, so a foot's bind-pose height is 8 cm, and a quad is
// skinned to the pelvis. There is no clip.
void writeLegRig(const std::filesystem::path &gltf) {
  using nlohmann::json;
  struct Joint {
    const char *name;
    glm::vec3 local;
    std::vector<int> children;
  };
  // Node indices: 0 Rig, 1 pelvis, 2 thigh_l, 3 calf_l, 4 foot_l, 5 ball_l, 6 thigh_r, 7 calf_r, 8 foot_r,
  // 9 ball_r, 10 Mesh.
  const std::vector<Joint> joints{{"Rig", {0.0f, 0.0f, 0.0f}, {1, 10}},  {"pelvis", {0.0f, 1.0f, 0.0f}, {2, 6}},
                                  {"thigh_l", {-0.1f, 0.0f, 0.0f}, {3}}, {"calf_l", {0.0f, -0.5f, -0.1f}, {4}},
                                  {"foot_l", {0.0f, -0.42f, 0.1f}, {5}}, {"ball_l", {0.0f, -0.05f, -0.1f}, {}},
                                  {"thigh_r", {0.1f, 0.0f, 0.0f}, {7}},  {"calf_r", {0.0f, -0.5f, -0.1f}, {8}},
                                  {"foot_r", {0.0f, -0.42f, 0.1f}, {9}}, {"ball_r", {0.0f, -0.05f, -0.1f}, {}}};
  std::vector<glm::vec3> global(joints.size(), glm::vec3{0.0f});
  for (std::size_t i = 0; i < joints.size(); ++i) {
    global[i] += joints[i].local;
    for (const int child : joints[i].children) {
      if (child < static_cast<int>(joints.size())) {
        global[static_cast<std::size_t>(child)] = global[i];
      }
    }
  }
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
  constexpr int floatType = 5126;
  // The skin's joints: pelvis, the left leg, the right leg, in node order 1 to 9.
  std::vector<float> inverseBinds;
  for (std::size_t i = 1; i < joints.size(); ++i) {
    const glm::mat4 inverse = glm::translate(glm::mat4{1.0f}, -global[i]);
    inverseBinds.insert(inverseBinds.end(), glm::value_ptr(inverse), glm::value_ptr(inverse) + 16);
  }
  const std::vector<float> positions{-0.5f, 0, 0, 0.5f, 0, 0, -0.5f, 2, 0, 0.5f, 2, 0};
  const std::vector<float> weights{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const std::vector<std::uint16_t> skinJoints(16, 0);
  const std::vector<std::uint32_t> indices{0, 1, 3, 0, 3, 2};
  const auto position = add(positions.data(), 48, 4, "VEC3", floatType);
  accessors.back()["min"] = json::array({-0.5, 0, 0});
  accessors.back()["max"] = json::array({0.5, 2, 0});
  const auto weight = add(weights.data(), 64, 4, "VEC4", floatType);
  const auto joint = add(skinJoints.data(), 32, 4, "VEC4", 5123);
  const auto index = add(indices.data(), 24, 6, "SCALAR", 5125);
  const auto inverseBind =
      add(inverseBinds.data(), inverseBinds.size() * sizeof(float), joints.size() - 1, "MAT4", floatType);
  json nodes = json::array();
  for (const Joint &entry : joints) {
    json node{{"name", entry.name}, {"translation", json::array({entry.local.x, entry.local.y, entry.local.z})}};
    if (!entry.children.empty()) {
      node["children"] = entry.children;
    }
    nodes.push_back(node);
  }
  nodes[0].erase("translation");
  nodes.push_back(json{{"name", "Mesh"}, {"mesh", 0}, {"skin", 0}});
  json skinJointNodes = json::array();
  for (int i = 1; i < static_cast<int>(joints.size()); ++i) {
    skinJointNodes.push_back(i);
  }
  const std::string binName = gltf.stem().string() + ".bin";
  REQUIRE(core::writeFile(gltf.parent_path() / binName, bin).has_value());
  const json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", nodes},
      {"meshes",
       json::array({json{
           {"name", "Quad"},
           {"primitives",
            json::array({json{{"attributes", {{"POSITION", position}, {"JOINTS_0", joint}, {"WEIGHTS_0", weight}}},
                              {"indices", index}}})}}})},
      {"skins", json::array({json{{"joints", skinJointNodes}, {"inverseBindMatrices", inverseBind}}})},
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

  // `legs` swaps the bending strip for writeLegRig's humanoid legs.
  explicit Fixture(bool legs = false) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    if (legs) {
      writeLegRig(root / "assets" / "rig.gltf");
    } else {
      writeRig(root / "assets" / "rig.gltf");
    }
    const std::vector<std::string> roots{"assets"};
    assets.open(root, roots);
    const auto models = assets.assets(assets::AssetType::Model);
    REQUIRE(models.size() == 1);
    model = models[0]->uuid;
    const assets::Model *loaded = assets.model(model);
    REQUIRE(loaded != nullptr);
    prefab = world::loadModelPrefab(world, *loaded, model, "Rig");
  }
  // model() reads only the hierarchy, so the skin and the clip arrive through a request like any
  // payload (docs/assets.md, "Database"). The tests of playback and posing start from a loaded
  // file; the one about arrival does not call this.
  void loadPayloads() {
    for (const assets::AssetInfo *info : assets.assets(assets::AssetType::Skin)) {
      static_cast<void>(assets.requestSkin(info->uuid));
    }
    assets.waitForLoads();
  }
  ~Fixture() {
    world.clearScene();
    assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
    sonnet::test::removeScratch(root);
  }

  // The model's clip of that name.
  core::Uuid clip(std::string_view name) const {
    const std::vector<const assets::AssetInfo *> clips = assets.assets(assets::AssetType::Animation);
    const auto found =
        std::ranges::find_if(clips, [name](const assets::AssetInfo *info) { return info->name == name; });
    INFO("no clip named " << name);
    REQUIRE(found != clips.end());
    return (*found)->uuid;
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
  fixture.loadPayloads();
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

TEST_CASE("an animator under a disabled entity holds its time until the entity is shown", "[world][animation]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity group = world.createEntity("Group");
  const flecs::entity rig = world.instantiate(fixture.prefab, "Rig", group);
  world.setPlaying(true);
  world.progress(0.25f);
  REQUIRE(rig.get<world::Animator>().time == Approx(0.25f));

  group.add<world::Disabled>();
  world.progress(0.25f);
  REQUIRE(rig.get<world::Animator>().time == Approx(0.25f));
  group.remove<world::Disabled>();
  world.progress(0.25f);
  REQUIRE(rig.get<world::Animator>().time == Approx(0.5f));
}

TEST_CASE("a skinned mesh's pose follows its joints into the draw list, in edit mode too", "[world][animation]") {
  Fixture fixture;
  fixture.loadPayloads();
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
  std::vector<float> morphWeights;
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
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
  world::buildDrawList(world, fixture.assets, draws, joints, morphWeights);
  REQUIRE(draws.size() == 2);
  const auto loneDraw = std::ranges::find(draws, world::World::pickId(lone), &renderer::DrawItem::id);
  REQUIRE(loneDraw->jointCount == 0);
  REQUIRE(loneDraw->skinInstance == 0);
}

// Placing a model reads its hierarchy and nothing else, so the first frames of a freshly loaded
// scene find the skin and the clip missing: the file's import is requested, the strip draws in its
// bind pose meanwhile, and play waits, rather than the frame stalling on the whole file.
TEST_CASE("a model's skin and clip arrive on a later frame instead of stalling the first", "[world][animation]") {
  Fixture fixture;
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Rig 1");
  const flecs::entity strip = fixture.node(instance, "Rig/Strip");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  const std::size_t buffersBefore = fixture.device->bufferCount();

  world.setPlaying(true);
  world.progress(0.25f);
  REQUIRE(fixture.assets.loading());
  REQUIRE_FALSE(strip.has<world::SkinPose>());
  REQUIRE(instance.get<world::Animator>().time == 0.0f);
  REQUIRE(fixture.device->bufferCount() == buffersBefore); // nothing of the file on the GPU yet

  fixture.assets.waitForLoads();
  world.progress(0.25f);
  REQUIRE(strip.has<world::SkinPose>());
  REQUIRE(instance.get<world::Animator>().time == Approx(0.25f));
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(22.5f));
  REQUIRE(fixture.device->bufferCount() > buffersBefore);
}

TEST_CASE("assigning a clip with a fade crossfades from the one it replaces", "[world][animation]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const core::Uuid bend = fixture.clip("Bend");
  const core::Uuid reach = fixture.clip("Reach");
  const flecs::entity instance = world.instantiate(fixture.prefab, "Fader");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  world.setPlaying(true);

  // Both clips held at half a second: Bend turns the tip +45 degrees there, Reach -45.
  instance.set<world::Animator>({.clip = bend, .time = 0.5f, .speed = 0.0f});
  world.progress(0.1f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(45.0f));

  instance.set<world::Animator>({.clip = reach, .time = 0.5f, .speed = 0.0f, .fade = 1.0f});
  world.progress(0.5f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(0.0f).margin(1e-3));
  // The clip left is the system's to fade out, not one of the script's layers (ADR-0038).
  REQUIRE(instance.get<world::Animator>().layers.empty());

  world.progress(0.25f); // three quarters across: Reach has 0.75 of the blend
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) ==
          Approx(-22.5f).margin(1.5)); // a normalised blend, a little past the arc's middle;

  world.progress(0.5f); // the fade is over: the old clip is gone
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(-45.0f));
  REQUIRE(instance.get<world::Animator>().layers.empty());

  // Without a fade the clip is swapped at once.
  instance.set<world::Animator>({.clip = bend, .time = 0.5f, .speed = 0.0f});
  world.progress(0.1f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(45.0f));
  REQUIRE(instance.get<world::Animator>().layers.empty());
}

TEST_CASE("animation events are recorded when playback crosses them, across a loop too", "[world][animation]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Walker");
  world.setPlaying(true);

  world.progress(0.2f);
  REQUIRE(animation.events().empty());
  world.progress(0.2f); // 0.2 to 0.4 crosses the step at 0.3
  REQUIRE(animation.events().size() == 1);
  REQUIRE(animation.events()[0].entity == instance.id());
  REQUIRE(animation.events()[0].name == "step");
  REQUIRE(animation.events()[0].argument == "left");
  world.progress(0.1f);
  REQUIRE(animation.events().empty()); // the list is the last Update's
  world.progress(0.5f);                // 0.5 to 1.0 reaches 0.8 and wraps
  REQUIRE(animation.events().size() == 1);
  REQUIRE(animation.events()[0].argument == "right");
  world.progress(0.4f); // 0 to 0.4 on the next lap
  REQUIRE(animation.events().size() == 1);
  REQUIRE(animation.events()[0].argument == "left");
}

TEST_CASE("root motion moves the entity instead of the root bone", "[world][animation]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Runner");
  const flecs::entity root = fixture.node(instance, "Rig/Root");
  world.setPlaying(true);
  auto animator = instance.get<world::Animator>();
  animator.rootMotion = true;
  instance.set<world::Animator>(animator);

  world.progress(0.25f); // before the step at half a second
  REQUIRE(instance.get<world::Transform>().position.x == Approx(0.0f));
  world.progress(0.5f); // across it: the bone's metre becomes the entity's
  REQUIRE(instance.get<world::Transform>().position.x == Approx(1.0f));
  REQUIRE(root.get<world::Transform>().position.x == Approx(0.0f));
  world.progress(0.5f); // the loop's wrap takes the bone back without taking the entity back
  REQUIRE(instance.get<world::Transform>().position.x == Approx(1.0f));
  world.progress(0.5f); // the next lap's step: another metre
  REQUIRE(instance.get<world::Transform>().position.x == Approx(2.0f));
}

TEST_CASE("a clip animates a mesh's morph weights, and the draw list hands them to the renderer",
          "[world][animation]") {
  platform::Platform platform{{.headless = true}};
  const std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase database{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_world_morph";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  constexpr const char *model =
      R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Face","mesh":0}],"meshes":[{"name":"FaceMesh","weights":[0.25],"primitives":[{"attributes":{"POSITION":0},"indices":1,"targets":[{"POSITION":2}]}]}],"animations":[{"name":"Smile","samplers":[{"input":3,"output":4}],"channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}]}],"buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAACAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAACAPw==","byteLength":100}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},{"buffer":0,"byteOffset":48,"byteLength":36},{"buffer":0,"byteOffset":84,"byteLength":8},{"buffer":0,"byteOffset":92,"byteLength":8}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"}]})json";
  REQUIRE(
      core::writeFile(root / "assets" / "face.gltf", std::as_bytes(std::span{model, std::strlen(model)})).has_value());
  const std::vector<std::string> roots{"assets"};
  database.open(root, roots);
  world::World world;
  {
    const auto models = database.assets(assets::AssetType::Model);
    REQUIRE(models.size() == 1);
    const assets::Model *loaded = database.model(models[0]->uuid);
    REQUIRE(loaded != nullptr);
    const flecs::entity prefab = world::loadModelPrefab(world, *loaded, models[0]->uuid, "Face");
    static_cast<void>(database.requestAnimation(prefab.get<world::Animator>().clip));
    database.waitForLoads();
    world::AnimationSystem animation{world, database};
    const flecs::entity instance = world.instantiate(prefab, "Smiler");
    const flecs::entity face = world.findByPath(instance, "Face");
    REQUIRE(face);
    REQUIRE(face.get<world::MorphWeights>().weights == std::vector<float>{0.25f}); // the file's weight

    world.setPlaying(true);
    world.progress(0.5f);
    REQUIRE(face.get<world::MorphWeights>().weights.at(0) == Approx(0.5f));

    std::vector<renderer::DrawItem> draws;
    std::vector<glm::mat4> joints;
    std::vector<float> morphWeights;
    world::buildDrawList(world, database, draws, joints, morphWeights);
    database.waitForLoads();
    world::buildDrawList(world, database, draws, joints, morphWeights);
    REQUIRE(draws.size() == 1);
    REQUIRE(morphWeights == std::vector<float>{0.5f});
    REQUIRE(draws[0].morphWeightCount == 1);
    REQUIRE(draws[0].skinInstance == face.id());
    REQUIRE(draws[0].jointCount == 0);
  }
  world.clearScene();
  database.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

// ADR-0038: the Animator's own clip weighs `weight` times its crossfade ramp, and the blend
// normalises by the weights, so one weight at 1 and the rest at 0 is that clip alone.
TEST_CASE("an Animator's weight blends its clip with its layers, from pure to mixed and back",
          "[world][animation][weights]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const core::Uuid bend = fixture.clip("Bend");
  const core::Uuid reach = fixture.clip("Reach");
  const flecs::entity instance = world.instantiate(fixture.prefab, "Mixer");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  world.setPlaying(true);

  // Both clips held at half a second: Bend turns the tip +45 degrees there, Reach -45.
  const auto hold = [&](float weight, float layerWeight) {
    instance.set<world::Animator>({.clip = bend,
                                   .time = 0.5f,
                                   .speed = 0.0f,
                                   .weight = weight,
                                   .layers = {{.clip = reach, .time = 0.5f, .speed = 0.0f, .weight = layerWeight}}});
    world.progress(0.1f);
    return angleAboutZ(tip.get<world::Transform>().rotation);
  };
  REQUIRE(world::Animator{}.weight == 1.0f);
  REQUIRE_FALSE(world::AnimationLayer{}.sync);
  REQUIRE(hold(1.0f, 0.0f) == Approx(45.0f));  // pure primary
  REQUIRE(hold(0.0f, 1.0f) == Approx(-45.0f)); // pure layer
  REQUIRE(hold(0.5f, 0.5f) == Approx(0.0f).margin(1e-3));
  REQUIRE(hold(0.25f, 0.25f) == Approx(0.0f).margin(1e-3)); // normalised: the sum need not be 1
  REQUIRE(hold(2.0f, 1.0f) == Approx(0.0f).margin(1e-3));   // the Animator's weight stops at 1

  // Moving the weight from one clip to the other moves the pose with it, a little at a time.
  float last = hold(1.0f, 0.0f);
  for (int i = 1; i <= 20; ++i) {
    const float w = static_cast<float>(i) / 20.0f;
    const float angle = hold(1.0f - w, w);
    REQUIRE(angle < last);
    REQUIRE(last - angle < 6.0f);
    last = angle;
  }
  REQUIRE(last == Approx(-45.0f));

  // Nothing weighs: the targets keep the pose they last had.
  REQUIRE(hold(1.0f, 0.0f) == Approx(45.0f));
  REQUIRE(hold(0.0f, 0.0f) == Approx(45.0f));
}

TEST_CASE("layers without sync play by their own time, speed and loop as before", "[world][animation][sync]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Free");
  world.setPlaying(true);
  instance.set<world::Animator>(
      {.clip = fixture.clip("Bend"),
       .layers = {{.clip = fixture.clip("Stride"), .time = 1.0f, .speed = 0.5f},
                  {.clip = fixture.clip("Reach"), .time = 0.9f, .speed = 1.0f, .loop = false}}});

  world.progress(0.4f);
  const world::Animator &animator = instance.get<world::Animator>();
  REQUIRE(animator.time == Approx(0.4f));
  REQUIRE(animator.layers.size() == 2);
  REQUIRE(animator.layers[0].time == Approx(1.2f));
  REQUIRE(animator.layers[1].time == Approx(1.0f)); // stopped at its end
  REQUIRE_FALSE(animator.layers[1].playing);

  auto paused = instance.get<world::Animator>();
  paused.playing = false;
  instance.set<world::Animator>(paused);
  world.progress(0.4f); // the layer does not follow the Animator's pause
  REQUIRE(instance.get<world::Animator>().time == Approx(0.4f));
  REQUIRE(instance.get<world::Animator>().layers[0].time == Approx(1.4f));
}

// Stride lasts two seconds and Bend one: synchronised, Stride is at twice Bend's time. Bend's
// weight is 0, so the tip shows Stride alone, -45 degrees per second of its own time.
TEST_CASE("a synchronised layer follows the primary's phase through loops, pauses, reversal and ends",
          "[world][animation][sync]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Synced");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  world.setPlaying(true);
  instance.set<world::Animator>({.clip = fixture.clip("Bend"),
                                 .weight = 0.0f,
                                 .layers = {{.clip = fixture.clip("Stride"),
                                             .time = 1.9f, // its own time, speed, playing and loop are ignored
                                             .speed = 0.0f,
                                             .playing = false,
                                             .loop = false,
                                             .sync = true}}});
  const auto layerTime = [&] { return instance.get<world::Animator>().layers.at(0).time; };
  const auto angle = [&] { return angleAboutZ(tip.get<world::Transform>().rotation); };

  world.progress(0.25f);
  REQUIRE(instance.get<world::Animator>().time == Approx(0.25f));
  REQUIRE(layerTime() == Approx(0.5f)); // the same phase, a quarter
  REQUIRE(angle() == Approx(-22.5f));
  world.progress(0.5f);
  REQUIRE(layerTime() == Approx(1.5f));
  REQUIRE(angle() == Approx(-67.5f));
  world.progress(0.5f); // the primary wraps, and the layer with it
  REQUIRE(instance.get<world::Animator>().time == Approx(0.25f));
  REQUIRE(layerTime() == Approx(0.5f));
  const world::AnimationLayer &layer = instance.get<world::Animator>().layers.at(0);
  REQUIRE(layer.speed == 0.0f);
  REQUIRE_FALSE(layer.playing);
  REQUIRE_FALSE(layer.loop);

  auto animator = instance.get<world::Animator>();
  animator.playing = false; // paused: the layer holds
  instance.set<world::Animator>(animator);
  world.progress(0.3f);
  REQUIRE(layerTime() == Approx(0.5f));

  animator.playing = true;
  animator.speed = -1.0f; // backwards, wrapping below zero
  instance.set<world::Animator>(animator);
  world.progress(0.5f);
  REQUIRE(instance.get<world::Animator>().time == Approx(0.75f));
  REQUIRE(layerTime() == Approx(1.5f));

  animator = instance.get<world::Animator>();
  animator.speed = 1.0f;
  animator.loop = false;
  animator.time = 0.5f;
  instance.set<world::Animator>(animator);
  world.progress(1.0f); // the primary stops at its end; the layer sits at its own
  REQUIRE(instance.get<world::Animator>().time == Approx(1.0f));
  REQUIRE_FALSE(instance.get<world::Animator>().playing);
  REQUIRE(layerTime() == Approx(2.0f));
  REQUIRE(angle() == Approx(-90.0f));
}

TEST_CASE("a synchronised layer holds without a primary and takes a new primary's phase", "[world][animation][sync]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Holder");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  world.setPlaying(true);
  const auto layerTime = [&] { return instance.get<world::Animator>().layers.at(0).time; };

  // No primary clip: the layer holds its time and is still posed there.
  instance.set<world::Animator>(
      {.clip = core::Uuid{}, .layers = {{.clip = fixture.clip("Stride"), .time = 0.5f, .sync = true}}});
  world.progress(0.3f);
  REQUIRE(layerTime() == Approx(0.5f));
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(-22.5f));

  // A primary of zero duration has no phase either.
  auto animator = instance.get<world::Animator>();
  animator.clip = fixture.clip("Still");
  instance.set<world::Animator>(animator);
  world.progress(0.3f);
  REQUIRE(layerTime() == Approx(0.5f));

  // The primary is back: the layer jumps to its phase.
  animator = instance.get<world::Animator>();
  animator.clip = fixture.clip("Bend");
  animator.time = 0.5f;
  instance.set<world::Animator>(animator);
  world.progress(0.1f);
  REQUIRE(layerTime() == Approx(1.2f));

  // A new primary keeping the time: the layer follows the new clip's phase.
  animator = instance.get<world::Animator>();
  animator.clip = fixture.clip("Reach");
  instance.set<world::Animator>(animator);
  world.progress(0.1f);
  REQUIRE(instance.get<world::Animator>().time == Approx(0.7f));
  REQUIRE(layerTime() == Approx(1.4f));

  // A layer without a clip waits; one of zero duration sits at zero.
  animator = instance.get<world::Animator>();
  animator.weight = 0.0f;
  animator.layers = {{.clip = core::Uuid{}, .sync = true}, {.clip = fixture.clip("Still"), .time = 0.3f, .sync = true}};
  instance.set<world::Animator>(animator);
  world.progress(0.1f);
  REQUIRE(instance.get<world::Animator>().layers.size() == 2);
  REQUIRE(instance.get<world::Animator>().layers[1].time == 0.0f);
  REQUIRE(angleAboutZ(tip.get<world::Transform>().rotation) == Approx(30.0f));
}

// ADR-0038: the clip a crossfade leaves starts at the weight it had and fades in the system's
// own state, so a script that writes `layers` whole every frame keeps it, and an interrupted
// crossfade hands over without a jump.
TEST_CASE("a crossfade keeps fading while the script replaces the layers, and an interrupted one is continuous",
          "[world][animation][weights]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const core::Uuid bend = fixture.clip("Bend");
  const core::Uuid stride = fixture.clip("Stride");
  const flecs::entity instance = world.instantiate(fixture.prefab, "Fader");
  const flecs::entity tip = fixture.node(instance, "Rig/Root/Tip");
  world.setPlaying(true);
  const auto angle = [&] { return angleAboutZ(tip.get<world::Transform>().rotation); };
  // What a locomotion script writes every frame: Reach held at -45 degrees, at half weight.
  const std::vector<world::AnimationLayer> layers{
      {.clip = fixture.clip("Reach"), .time = 0.5f, .speed = 0.0f, .weight = 0.5f}};
  const auto step = [&](float dt) {
    auto animator = instance.get<world::Animator>();
    animator.layers = layers;
    instance.set<world::Animator>(animator);
    world.progress(dt);
  };

  // Bend held at +45 at half weight against Reach.
  instance.set<world::Animator>({.clip = bend, .time = 0.5f, .speed = 0.0f, .weight = 0.5f, .layers = layers});
  world.progress(0.1f);
  REQUIRE(angle() == Approx(0.0f).margin(1e-3));

  // To Stride at half its length (-45) over a second: Bend leaves at its weight, no jump.
  instance.set<world::Animator>(
      {.clip = stride, .time = 1.0f, .speed = 0.0f, .weight = 0.5f, .fade = 1.0f, .layers = layers});
  world.progress(0.001f);
  REQUIRE(angle() == Approx(0.0f).margin(0.2));
  REQUIRE(instance.get<world::Animator>().layers.size() == 1);
  float last = angle();
  for (int i = 0; i < 499; ++i) {
    step(0.001f);
    REQUIRE(std::abs(angle() - last) < 0.2f);
    last = angle();
  }
  // Half way: Bend 0.25, Stride 0.5 x 0.5, Reach 0.5.
  REQUIRE(angle() == Approx(-22.5f).margin(1.5));
  REQUIRE(instance.get<world::Animator>().layers.size() == 1);

  // Interrupted: back to Bend, and Stride leaves at the 0.25 it had reached.
  auto animator = instance.get<world::Animator>();
  animator.clip = bend;
  animator.time = 0.5f;
  instance.set<world::Animator>(animator);
  step(0.001f);
  REQUIRE(angle() == Approx(last).margin(0.2));

  // Both outgoing clips gone, Bend back at its half weight against Reach.
  for (int i = 0; i < 11; ++i) {
    step(0.1f);
  }
  REQUIRE(angle() == Approx(0.0f).margin(1e-3));
  REQUIRE(instance.get<world::Animator>().layers.size() == 1);
}

TEST_CASE("events cross the primary's interval on synchronised layers, without footsteps when the phase resets",
          "[world][animation][sync]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Stepper");
  world.setPlaying(true);
  const auto arguments = [&] {
    std::vector<std::string> result;
    for (const world::AnimationEventRecord &event : animation.events()) {
      result.push_back(std::format("{}@{:.1f}", event.argument, event.time));
    }
    return result;
  };
  using Events = std::vector<std::string>;

  // Bend at weight 0 is silent; Stride, synchronised, steps at 0.6 and 1.6 of its own time.
  instance.set<world::Animator>(
      {.clip = fixture.clip("Bend"), .weight = 0.0f, .layers = {{.clip = fixture.clip("Stride"), .sync = true}}});
  world.progress(0.2f);
  REQUIRE(arguments().empty());
  world.progress(0.2f); // phase 0.2 to 0.4: Stride 0.4 to 0.8
  REQUIRE(arguments() == Events{"left@0.6"});
  world.progress(0.5f);
  REQUIRE(arguments() == Events{"right@1.6"});
  world.progress(0.3f); // wraps: 1.8 to 0.4 crosses nothing
  REQUIRE(arguments().empty());
  world.progress(0.2f);
  REQUIRE(arguments() == Events{"left@0.6"});

  // The script moves the primary on to 0.9: the layer, last at 0.8, does not cross 1.6 getting there.
  auto animator = instance.get<world::Animator>();
  animator.time = 0.9f;
  instance.set<world::Animator>(animator);
  world.progress(0.05f);
  REQUIRE(arguments().empty());
  REQUIRE(instance.get<world::Animator>().layers[0].time == Approx(1.9f));

  // Under the event weight a synchronised layer is as quiet as any other.
  animator = instance.get<world::Animator>();
  animator.time = 0.2f;
  animator.layers[0].weight = 0.04f;
  instance.set<world::Animator>(animator);
  world.progress(0.2f);
  REQUIRE(arguments().empty());

  // A gait switch keeping a time past the new clip's end crosses no lap it never played.
  instance.set<world::Animator>({.clip = fixture.clip("Stride"), .time = 1.5f});
  world.progress(0.01f);
  REQUIRE(arguments().empty());
  animator = instance.get<world::Animator>();
  animator.clip = fixture.clip("Bend");
  instance.set<world::Animator>(animator);
  world.progress(0.1f); // 0.51 to 0.61 of Bend
  REQUIRE(arguments().empty());
  REQUIRE(instance.get<world::Animator>().time == Approx(0.61f));
}

TEST_CASE("a crossfade's outgoing clip and the incoming one both fire their events", "[world][animation][sync]") {
  Fixture fixture;
  fixture.loadPayloads();
  world::World &world = fixture.world;
  world::AnimationSystem animation{world, fixture.assets};
  const flecs::entity instance = world.instantiate(fixture.prefab, "Changer");
  world.setPlaying(true);
  const auto arguments = [&] {
    std::vector<std::string> result;
    for (const world::AnimationEventRecord &event : animation.events()) {
      result.push_back(std::format("{}@{:.1f}", event.argument, event.time));
    }
    return result;
  };
  using Events = std::vector<std::string>;

  instance.set<world::Animator>({.clip = fixture.clip("Bend"), .time = 0.1f});
  world.progress(0.05f);
  REQUIRE(arguments().empty());
  auto animator = instance.get<world::Animator>();
  animator.clip = fixture.clip("Stride"); // from 0.15, over a second
  animator.fade = 1.0f;
  instance.set<world::Animator>(animator);
  world.progress(0.2f); // Bend, leaving, crosses its 0.3; Stride is at 0.35
  REQUIRE(arguments() == Events{"left@0.3"});
  world.progress(0.3f); // Stride, at half weight, crosses its 0.6
  REQUIRE(arguments() == Events{"left@0.6"});
  world.progress(0.4f); // Bend, at 0.1, still crosses its 0.8
  REQUIRE(arguments() == Events{"right@0.8"});
}

namespace {

// Leg IK (ADR-0042) on the leg rig: a model standing at `origin`, turned 30 degrees about Y, with
// the pass's component on it. `ground` writes what a game's query would, as heights above the
// model's origin under each foot.
struct LegRig {
  Fixture fixture{true};
  world::World &world = fixture.world;
  world::AnimationSystem animation{fixture.world, fixture.assets};
  glm::vec3 origin{1.0f, 0.5f, -2.0f};
  flecs::entity instance;
  flecs::entity pelvis, thighL, calfL, footL, ballL, thighR, calfR, footR, strip;

  explicit LegRig(float weight = 1.0f) {
    fixture.loadPayloads();
    instance = world.instantiate(fixture.prefab, "Humanoid");
    instance.set<world::Transform>(
        {.position = origin, .rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 1.0f, 0.0f})});
    pelvis = fixture.node(instance, "Rig/pelvis");
    thighL = fixture.node(instance, "Rig/pelvis/thigh_l");
    calfL = fixture.node(instance, "Rig/pelvis/thigh_l/calf_l");
    footL = fixture.node(instance, "Rig/pelvis/thigh_l/calf_l/foot_l");
    ballL = fixture.node(instance, "Rig/pelvis/thigh_l/calf_l/foot_l/ball_l");
    thighR = fixture.node(instance, "Rig/pelvis/thigh_r");
    calfR = fixture.node(instance, "Rig/pelvis/thigh_r/calf_r");
    footR = fixture.node(instance, "Rig/pelvis/thigh_r/calf_r/foot_r");
    strip = fixture.node(instance, "Rig/Mesh");
    world::LegIk ik{.weight = weight, .pelvis = "Rig/pelvis"};
    ik.legs = {{.thigh = "Rig/pelvis/thigh_l",
                .calf = "Rig/pelvis/thigh_l/calf_l",
                .foot = "Rig/pelvis/thigh_l/calf_l/foot_l"},
               {.thigh = "Rig/pelvis/thigh_r",
                .calf = "Rig/pelvis/thigh_r/calf_r",
                .foot = "Rig/pelvis/thigh_r/calf_r/foot_r"}};
    instance.set<world::LegIk>(ik);
    world.progress(1.0f / 60.0f);
  }

  void ground(float left, float right, const glm::vec3 &leftNormal = {0.0f, 1.0f, 0.0f},
              const glm::vec3 &rightNormal = {0.0f, 1.0f, 0.0f}, bool leftGrounded = true, bool rightGrounded = true) {
    world::LegIk ik = instance.get<world::LegIk>();
    ik.legs[0].groundPoint = {origin.x - 0.1f, origin.y + left, origin.z};
    ik.legs[0].groundNormal = leftNormal;
    ik.legs[0].grounded = leftGrounded;
    ik.legs[1].groundPoint = {origin.x + 0.1f, origin.y + right, origin.z};
    ik.legs[1].groundNormal = rightNormal;
    ik.legs[1].grounded = rightGrounded;
    instance.set<world::LegIk>(ik);
  }
  void frames(int count, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < count; ++i) {
      world.progress(dt);
    }
  }
  // The joint's place in the world, and its height above the model's origin.
  static glm::vec3 at(flecs::entity joint) {
    return glm::vec3{joint.get<world::WorldTransform>().matrix[3]};
  }
  float height(flecs::entity joint) const {
    return at(joint).y - origin.y;
  }
  // The way the model faces, on the ground: its -Z turned 30 degrees.
  glm::vec3 forward() const {
    return glm::vec3{instance.get<world::WorldTransform>().matrix * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};
  }
  // How far in front of its hip a leg's knee is, along the model's forward.
  float kneeForward(flecs::entity thigh, flecs::entity calf) const {
    return glm::dot(at(calf) - at(thigh), forward());
  }
  std::vector<flecs::entity> joints() const {
    return {pelvis, thighL, calfL, footL, ballL, thighR, calfR, footR};
  }
  std::vector<glm::mat4> matrices() const {
    std::vector<glm::mat4> result;
    for (const flecs::entity joint : joints()) {
      result.push_back(joint.get<world::WorldTransform>().matrix);
    }
    return result;
  }
};

bool sameBits(const std::vector<glm::mat4> &a, const std::vector<glm::mat4> &b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::mat4)) == 0;
}

} // namespace

TEST_CASE("leg IK with a weight of 0, or on flat ground, leaves every world matrix and the palette as they were",
          "[world][animation][legik]") {
  LegRig rig{0.0f};
  rig.frames(2);
  REQUIRE(rig.strip.has<world::SkinPose>());
  const std::vector<glm::mat4> before = rig.matrices();
  const std::vector<glm::mat4> palette = rig.strip.get<world::SkinPose>().joints;
  REQUIRE(palette.size() == 9);

  // Ground 10 cm down and tilted, with a weight of 0: nothing.
  rig.ground(-0.1f, 0.05f, glm::normalize(glm::vec3{0.3f, 1.0f, 0.0f}));
  rig.frames(30);
  REQUIRE(sameBits(rig.matrices(), before));
  REQUIRE(std::memcmp(rig.strip.get<world::SkinPose>().joints.data(), palette.data(),
                      palette.size() * sizeof(glm::mat4)) == 0);

  // A weight of 1 on level ground at the model's feet: the same, bit for bit.
  world::LegIk ik = rig.instance.get<world::LegIk>();
  ik.weight = 1.0f;
  rig.instance.set<world::LegIk>(ik);
  rig.ground(0.0f, 0.0f);
  rig.frames(120);
  REQUIRE(sameBits(rig.matrices(), before));
  REQUIRE(std::memcmp(rig.strip.get<world::SkinPose>().joints.data(), palette.data(),
                      palette.size() * sizeof(glm::mat4)) == 0);

  // And ground that the query missed counts for nothing, whatever its point says.
  rig.ground(-0.2f, -0.2f, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, false, false);
  rig.frames(30);
  REQUIRE(sameBits(rig.matrices(), before));
}

TEST_CASE("leg IK puts a foot on higher ground, bending the knee the way the clip bent it",
          "[world][animation][legik]") {
  LegRig rig;
  const glm::mat4 flat = rig.footL.get<world::WorldTransform>().matrix;
  rig.ground(0.1f, 0.0f);
  rig.frames(60);
  // The foot keeps the orientation the animation gave it in the world, whatever the shin does: a
  // flat sole stays flat when the knee bends.
  const glm::mat4 raised = rig.footL.get<world::WorldTransform>().matrix;
  for (int column = 0; column < 3; ++column) {
    REQUIRE(glm::length(glm::vec3{raised[column]} - glm::vec3{flat[column]}) < 1e-4f);
  }
  // The raised foot reaches the ground within a millimetre; the other stays; the pelvis does not rise.
  REQUIRE(rig.height(rig.footL) == Approx(0.18f).margin(1e-3f));
  REQUIRE(rig.height(rig.footR) == Approx(0.08f).margin(1e-3f));
  REQUIRE(rig.height(rig.pelvis) == Approx(1.0f).margin(1e-4f));
  // The knee stays in front, bent further than before (it was 10 cm ahead of the hip).
  REQUIRE(rig.kneeForward(rig.thighL, rig.calfL) > 0.1f + 0.02f);
  REQUIRE(rig.kneeForward(rig.thighR, rig.calfR) == Approx(0.1f).margin(1e-3f));
  // The bones keep their lengths.
  REQUIRE(glm::length(LegRig::at(rig.calfL) - LegRig::at(rig.thighL)) ==
          Approx(glm::length(glm::vec3{0.0f, 0.5f, 0.1f})).margin(1e-4f));
  REQUIRE(glm::length(LegRig::at(rig.footL) - LegRig::at(rig.calfL)) ==
          Approx(glm::length(glm::vec3{0.0f, 0.42f, 0.1f})).margin(1e-4f));

  // A clip that bent the knee backwards is followed: the pole is the animated knee.
  LegRig backwards;
  backwards.calfL.set<world::Transform>({.position = {0.0f, -0.5f, 0.1f}});
  backwards.footL.set<world::Transform>({.position = {0.0f, -0.42f, -0.1f}});
  backwards.ground(0.1f, 0.0f);
  backwards.frames(60);
  REQUIRE(backwards.height(backwards.footL) == Approx(0.18f).margin(1e-3f));
  REQUIRE(backwards.kneeForward(backwards.thighL, backwards.calfL) < -0.1f - 0.02f);
}

TEST_CASE("leg IK lowers the pelvis to the lower foot, never raises it, and caps the drop",
          "[world][animation][legik]") {
  LegRig rig;
  rig.ground(-0.1f, 0.0f);
  rig.frames(60);
  REQUIRE(rig.height(rig.pelvis) == Approx(0.9f).margin(1e-3f));
  REQUIRE(rig.height(rig.footL) == Approx(-0.02f).margin(1e-3f)); // 8 cm up from a ground 10 cm down
  REQUIRE(rig.height(rig.footR) == Approx(0.08f).margin(1e-3f));  // the raised foot bends its knee instead
  REQUIRE(rig.kneeForward(rig.thighR, rig.calfR) > 0.1f + 0.01f);

  // Both feet on ground above the model's origin: the pelvis stays.
  rig.ground(0.05f, 0.08f);
  rig.frames(60);
  REQUIRE(rig.height(rig.pelvis) == Approx(1.0f).margin(1e-4f));

  // Ground half a metre down: the drop stops at 30 cm, and a foot the leg cannot reach even so is
  // left as animated, 8 cm above the model's origin, not claiming ground it cannot touch.
  rig.ground(-0.5f, 0.0f);
  rig.frames(60);
  REQUIRE(rig.height(rig.pelvis) == Approx(0.7f).margin(1e-3f));
  REQUIRE(rig.height(rig.footL) == Approx(0.08f).margin(2e-3f));
  REQUIRE(std::isfinite(LegRig::at(rig.calfL).x));
  REQUIRE(rig.kneeForward(rig.thighL, rig.calfL) > 0.0f); // no flip
  REQUIRE(rig.height(rig.footR) == Approx(0.08f).margin(1e-3f));
}

TEST_CASE("leg IK turns a foot to the ground's normal, held to the limit", "[world][animation][legik]") {
  LegRig rig;
  const auto normal = [](float degrees) {
    return glm::vec3{std::sin(glm::radians(degrees)), std::cos(glm::radians(degrees)), 0.0f};
  };
  const auto tilt = [&](flecs::entity foot) {
    const glm::vec3 up{foot.get<world::WorldTransform>().matrix[1]};
    return glm::degrees(std::acos(glm::dot(glm::normalize(up), glm::vec3{0.0f, 1.0f, 0.0f})));
  };
  rig.ground(0.0f, 0.0f, normal(20.0f));
  rig.frames(60);
  const glm::vec3 up{rig.footL.get<world::WorldTransform>().matrix[1]};
  REQUIRE(glm::dot(glm::normalize(up), glm::normalize(normal(20.0f))) > std::cos(glm::radians(0.5f)));
  REQUIRE(tilt(rig.footR) == Approx(0.0f).margin(1e-3f));
  // The ball goes with the foot.
  REQUIRE(LegRig::at(rig.ballL).x > LegRig::at(rig.footL).x - 0.2f);

  // Ground steeper than maxFootAngle (30 degrees) turns the foot that far and no further.
  rig.ground(0.0f, 0.0f, normal(50.0f));
  rig.frames(60);
  REQUIRE(tilt(rig.footL) == Approx(30.0f).margin(0.3f));
}

TEST_CASE("leg IK leaves a foot the animation lifted to the animation", "[world][animation][legik]") {
  LegRig rig;
  // The left foot swings 20 cm above its planted height, past the 10 cm where it lets go.
  rig.footL.set<world::Transform>({.position = {0.0f, -0.42f + 0.2f, 0.1f}});
  rig.ground(-0.05f, -0.05f);
  rig.frames(60);
  // The right foot, planted, goes down 5 cm with the pelvis; the lifted one stays where it was.
  REQUIRE(rig.height(rig.footR) == Approx(0.03f).margin(1e-3f));
  REQUIRE(rig.height(rig.pelvis) == Approx(0.95f).margin(1e-3f));
  REQUIRE(rig.height(rig.footL) == Approx(0.28f).margin(1e-3f));
}

TEST_CASE("leg IK eases at the same rate at any frame rate and holds when no time passes",
          "[world][animation][legik]") {
  LegRig rig;
  rig.ground(0.0f, 0.0f);
  rig.frames(30);

  // A sixth of a second after the ground drops 10 cm, at 30 and at 144 frames a second.
  const auto settle = [&](int frames, float dt) {
    rig.ground(0.0f, 0.0f);
    rig.frames(120);
    rig.ground(-0.1f, 0.0f);
    rig.frames(frames, dt);
    return rig.height(rig.pelvis);
  };
  const float slow = settle(5, 1.0f / 30.0f);
  const float fast = settle(24, 1.0f / 144.0f);
  CAPTURE(slow, fast);
  // Eased: 1 - exp(-1/6 / 0.08) = 0.875 of the way, 12.5 mm short.
  REQUIRE(slow == Approx(1.0f - 0.1f * (1.0f - std::exp(-(1.0f / 6.0f) / 0.08f))).margin(1e-3f));
  REQUIRE(slow == Approx(fast).margin(1e-3f));

  // No game time (a time scale of 0, which gives the systems a zero delta): the eased values stay,
  // whatever the ground does.
  const float held = rig.height(rig.pelvis);
  rig.ground(-0.3f, -0.2f);
  rig.world.setTimeScale(0.0f);
  rig.frames(10);
  REQUIRE(rig.height(rig.pelvis) == held);
  rig.world.setTimeScale(1.0f);
  rig.frames(1);
  REQUIRE(rig.height(rig.pelvis) < held);
}

TEST_CASE("leg IK moves world matrices and the palette, never the local Transforms", "[world][animation][legik]") {
  LegRig rig;
  rig.frames(2);
  const auto locals = [&] {
    std::vector<world::Transform> result;
    for (const flecs::entity joint : rig.joints()) {
      result.push_back(joint.get<world::Transform>());
    }
    return result;
  };
  const std::vector<world::Transform> before = locals();
  const std::vector<glm::mat4> world = rig.matrices();
  const glm::mat4 bindFoot = rig.strip.get<world::SkinPose>().joints[3];

  rig.ground(-0.1f, 0.05f, glm::normalize(glm::vec3{0.2f, 1.0f, 0.1f}));
  rig.frames(60);
  const std::vector<world::Transform> after = locals();
  REQUIRE(after.size() == before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    REQUIRE(after[i].position == before[i].position);
    REQUIRE(after[i].rotation == before[i].rotation);
    REQUIRE(after[i].scale == before[i].scale);
  }
  REQUIRE_FALSE(sameBits(rig.matrices(), world));

  // The skin pose is built from the rewritten world matrices: each joint's matrix times its inverse
  // bind, brought into the mesh's space.
  const glm::mat4 toMesh = glm::inverse(rig.strip.get<world::WorldTransform>().matrix);
  const std::vector<glm::mat4> &palette = rig.strip.get<world::SkinPose>().joints;
  // The skin's joints are the nodes 1 to 9: pelvis, thigh_l, calf_l, foot_l, ball_l, thigh_r, ...
  const glm::mat4 footBind = glm::translate(glm::mat4{1.0f}, glm::vec3{0.1f, -0.08f, 0.0f});
  const glm::mat4 expected = toMesh * rig.footL.get<world::WorldTransform>().matrix * footBind;
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      REQUIRE(palette[3][c][r] == Approx(expected[c][r]).margin(1e-5f));
    }
  }
  REQUIRE(palette[3] != bindFoot);
  // The pelvis moved with the drop; the bind pose put a vertex of the foot at the foot, so it is
  // not where it was.
  REQUIRE(glm::length(glm::vec3{palette[3] * glm::vec4{-0.1f, 0.08f, 0.0f, 1.0f}} - glm::vec3{-0.1f, 0.08f, 0.0f}) >
          0.01f);
}

TEST_CASE("leg IK skips a leg whose bones are not there and carries on with the others", "[world][animation][legik]") {
  LegRig rig;
  world::LegIk ik = rig.instance.get<world::LegIk>();
  ik.legs[0].calf = "Rig/pelvis/nowhere";
  rig.instance.set<world::LegIk>(ik);
  rig.ground(0.0f, -0.1f);
  rig.frames(60);
  // The right foot and the pelvis follow the lower ground; the unresolved leg is not touched.
  REQUIRE(rig.height(rig.pelvis) == Approx(0.9f).margin(1e-3f));
  REQUIRE(rig.height(rig.footR) == Approx(-0.02f).margin(1e-3f));
  // The skipped leg is only carried down by the pelvis, finite and in one piece.
  REQUIRE(rig.height(rig.footL) == Approx(-0.02f).margin(1e-3f));

  // Fixing the path binds the leg again, which bends to keep its foot where it was, and a pelvis
  // that is not there moves nothing.
  ik.legs[0].calf = "Rig/pelvis/thigh_l/calf_l";
  rig.instance.set<world::LegIk>(ik);
  rig.frames(60);
  REQUIRE(rig.height(rig.footL) == Approx(0.08f).margin(1e-3f));
  ik.pelvis = "Rig/missing";
  rig.instance.set<world::LegIk>(ik);
  rig.frames(10);
  REQUIRE(rig.height(rig.pelvis) == Approx(1.0f).margin(1e-4f)); // back to the animated pose
  REQUIRE(rig.height(rig.footL) == Approx(0.08f).margin(1e-4f));
}
