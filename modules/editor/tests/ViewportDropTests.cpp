#include <sonnet/editor/ViewportDrop.h>

#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>

#include <TestSupport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using editor::DropAction;

namespace {

bool approxEqual(glm::vec3 a, glm::vec3 b) {
  return glm::length(a - b) < 1e-4f;
}

// A database on a scratch folder holding one file of each kind, as AssetMenuTests' fixtures make one.
struct DropFixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 0}};
  assets::AssetDatabase database{renderer, jobs};
  world::World world;
  std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_viewport_drop";

  DropFixture() {
    std::filesystem::remove_all(root);
    const std::filesystem::path folder = root / "assets";
    std::filesystem::create_directories(folder / "crate");
    std::filesystem::create_directories(folder / "barrel");
    for (const char *model : {"crate/crate.gltf", "barrel/barrel.gltf"}) {
      REQUIRE(core::writeFile(folder / model, std::string_view{R"({"asset": {"version": "2.0"}})"}).has_value());
    }
    REQUIRE(core::writeFile(folder / "steel.material.json", std::string_view{R"({"version": 1})"}).has_value());
    REQUIRE(core::writeFile(folder / "turret.lua", std::string_view{"return {}"}).has_value());
    REQUIRE(core::writeFile(folder / "sky.hdr", std::string_view{"hdr"}).has_value());
    REQUIRE(core::writeFile(folder / "wood.png", std::string_view{"png"}).has_value());
    REQUIRE(core::writeFile(folder / "beep.wav", std::string_view{"wav"}).has_value());
    database.open(root, std::vector<std::string>{"assets"});
  }
  ~DropFixture() {
    test::removeScratch(root);
  }
  DropFixture(const DropFixture &) = delete;
  DropFixture &operator=(const DropFixture &) = delete;

  [[nodiscard]] core::Uuid uuidOf(std::string_view file) const {
    const assets::AssetInfo *info = database.findByPath(root / "assets" / file);
    REQUIRE(info != nullptr);
    return info->uuid;
  }

  // A built-in box at `position` scaled by `scale`, its world matrix set as the transform system would.
  flecs::entity box(std::string_view name, glm::vec3 position, glm::vec3 scale = glm::vec3{1.0f},
                    flecs::entity parent = {}) {
    const flecs::entity entity = world.createEntity(name, parent);
    const world::Transform transform{.position = position, .rotation = {1.0f, 0.0f, 0.0f, 0.0f}, .scale = scale};
    const glm::mat4 parentMatrix = parent ? parent.get<world::WorldTransform>().matrix : glm::mat4{1.0f};
    entity.set(transform);
    entity.set(world::WorldTransform{parentMatrix * transform.matrix()});
    entity.set(world::MeshRenderer{});
    return entity;
  }

  // Loads every requested mesh, after a first cast asked for them.
  std::optional<editor::SceneHit> cast(const renderer::Ray &ray) {
    (void)editor::raycastScene(world, database, ray);
    database.waitForLoads();
    return editor::raycastScene(world, database, ray);
  }
};

// Clang warns about designated initialisers that skip a field, so draw items are built by assignment.
renderer::DrawItem drawItem(renderer::MeshHandle mesh, const glm::mat4 &transform = glm::mat4{1.0f}) {
  renderer::DrawItem item;
  item.mesh = mesh;
  item.transform = transform;
  return item;
}

const renderer::Ray Forward{.origin = {0.0f, 0.0f, 0.0f}, .direction = {0.0f, 0.0f, -1.0f}};

} // namespace

TEST_CASE("dropAction says what a drop on the viewport does", "[editor][drop]") {
  DropFixture fixture;
  // A prefab file's prefab is in the world under its identity, as a model's is once something placed it.
  const flecs::entity prefab = fixture.world.createEntity("Lamp");
  fixture.world.makePrefab(prefab);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.world.uuidOf(prefab)) == DropAction::Instance);
  const core::Uuid crate = fixture.uuidOf("crate/crate.gltf");
  (void)world::loadModelPrefab(fixture.world, assets::Model{}, crate, "crate");
  CHECK(editor::dropAction(fixture.world, fixture.database, crate) == DropAction::Instance);
  // A model whose prefab is not in the world is not a drop yet.
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("barrel/barrel.gltf")) == DropAction::None);
  CHECK(editor::dropAction(fixture.world, fixture.database, assets::builtin::box()) == DropAction::Mesh);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("steel.material.json")) ==
        DropAction::Material);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("turret.lua")) == DropAction::Script);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("sky.hdr")) == DropAction::Environment);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("wood.png")) == DropAction::None);
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.uuidOf("beep.wav")) == DropAction::None);
  CHECK(editor::dropAction(fixture.world, fixture.database, core::Uuid::generate()) == DropAction::None);
  // A scene entity under the identity is not a prefab.
  const flecs::entity entity = fixture.world.createEntity("Crate 1");
  CHECK(editor::dropAction(fixture.world, fixture.database, fixture.world.uuidOf(entity)) == DropAction::None);
}

TEST_CASE("raycastScene finds the nearest shown triangle", "[editor][drop]") {
  DropFixture fixture;

  SECTION("the front face of a box") {
    const flecs::entity box = fixture.box("Box", {0.0f, 0.0f, -5.0f}, {2.0f, 2.0f, 2.0f});
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == box);
    CHECK(approxEqual(hit->point, {0.0f, 0.0f, -4.0f}));
    CHECK(approxEqual(hit->normal, {0.0f, 0.0f, 1.0f}));
  }

  SECTION("the normal faces the ray from inside") {
    (void)fixture.box("Room", {0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f});
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(approxEqual(hit->point, {0.0f, 0.0f, -2.0f}));
    CHECK(approxEqual(hit->normal, {0.0f, 0.0f, 1.0f}));
  }

  SECTION("a rotated and scaled box takes its own face's normal") {
    const flecs::entity box = fixture.world.createEntity("Wedge");
    const world::Transform transform{.position = {0.0f, 0.0f, -5.0f},
                                     .rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
                                     .scale = {1.0f, 1.0f, 3.0f}};
    box.set(transform);
    box.set(world::WorldTransform{transform.matrix()});
    box.set(world::MeshRenderer{});
    const renderer::Ray ray{.origin = {0.0f, 0.0f, 0.0f}, .direction = glm::normalize(glm::vec3{0.3f, 0.0f, -1.0f})};
    const auto hit = fixture.cast(ray);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == box);
    // On the surface the ray crosses, and with a normal at a right angle to that surface's edges.
    const glm::mat4 matrix = transform.matrix();
    const glm::vec3 local{glm::inverse(matrix) * glm::vec4{hit->point, 1.0f}};
    const glm::vec3 absolute = glm::abs(local);
    CHECK(std::abs(std::max({absolute.x, absolute.y, absolute.z}) - 0.5f) < 1e-4f);
    CHECK(std::abs(glm::length(hit->normal) - 1.0f) < 1e-4f);
    CHECK(glm::dot(hit->normal, ray.direction) < 0.0f);
    const glm::vec3 xEdge{matrix * glm::vec4{1.0f, 0.0f, 0.0f, 0.0f}};
    const glm::vec3 yEdge{matrix * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::vec3 zEdge{matrix * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}};
    const int edgesAlong = (std::abs(glm::dot(hit->normal, xEdge)) < 1e-4f ? 1 : 0) +
                           (std::abs(glm::dot(hit->normal, yEdge)) < 1e-4f ? 1 : 0) +
                           (std::abs(glm::dot(hit->normal, zEdge)) < 1e-4f ? 1 : 0);
    CHECK(edgesAlong == 2);
  }

  SECTION("the nearer of two boxes") {
    (void)fixture.box("Far", {0.0f, 0.0f, -10.0f});
    const flecs::entity nearBox = fixture.box("Near", {0.0f, 0.0f, -5.0f});
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == nearBox);
    CHECK(approxEqual(hit->point, {0.0f, 0.0f, -4.5f}));
  }

  SECTION("a box under a Disabled parent is skipped") {
    const flecs::entity farBox = fixture.box("Far", {0.0f, 0.0f, -10.0f});
    const flecs::entity group = fixture.world.createEntity("Group");
    (void)fixture.box("Near", {0.0f, 0.0f, -5.0f}, glm::vec3{1.0f}, group);
    group.add<world::Disabled>();
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == farBox);
    CHECK(approxEqual(hit->point, {0.0f, 0.0f, -9.5f}));
  }

  SECTION("a Disabled box is skipped") {
    const flecs::entity farBox = fixture.box("Far", {0.0f, 0.0f, -10.0f});
    fixture.box("Near", {0.0f, 0.0f, -5.0f}).add<world::Disabled>();
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == farBox);
  }

  SECTION("an invisible box is skipped") {
    const flecs::entity farBox = fixture.box("Far", {0.0f, 0.0f, -10.0f});
    const flecs::entity nearBox = fixture.box("Near", {0.0f, 0.0f, -5.0f});
    world::MeshRenderer hidden;
    hidden.visible = false;
    nearBox.set(hidden);
    const auto hit = fixture.cast(Forward);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == farBox);
  }

  SECTION("a miss") {
    (void)fixture.box("Box", {0.0f, 0.0f, -5.0f});
    CHECK_FALSE(fixture.cast({.origin = {2.0f, 0.0f, 0.0f}, .direction = {0.0f, 0.0f, -1.0f}}).has_value());
    // Behind the ray's origin.
    CHECK_FALSE(fixture.cast({.origin = {0.0f, 0.0f, 0.0f}, .direction = {0.0f, 0.0f, 1.0f}}).has_value());
  }

  SECTION("an empty scene") {
    CHECK_FALSE(fixture.cast(Forward).has_value());
  }
}

TEST_CASE("land puts a drop on a surface, the grid's plane or in the air", "[editor][drop]") {
  const glm::vec3 up{0.0f, 1.0f, 0.0f};

  SECTION("on a surface hit") {
    const editor::SceneHit hit{.entity = {}, .point = {1.0f, 2.0f, -3.0f}, .normal = {1.0f, 0.0f, 0.0f}};
    const editor::Landing landing = editor::land(Forward, hit, up, 10.0f, std::nullopt);
    CHECK(approxEqual(landing.point, hit.point));
    REQUIRE(landing.normal.has_value());
    CHECK(approxEqual(*landing.normal, hit.normal));
  }

  SECTION("on the floor from above") {
    const renderer::Ray ray{.origin = {0.0f, 5.0f, 5.0f}, .direction = glm::normalize(glm::vec3{0.0f, -1.0f, -1.0f})};
    const editor::Landing landing = editor::land(ray, std::nullopt, up, 10.0f, std::nullopt);
    CHECK(approxEqual(landing.point, {0.0f, 0.0f, 0.0f}));
    REQUIRE(landing.normal.has_value());
    CHECK(approxEqual(*landing.normal, up));
  }

  SECTION("on the floor from below, its normal facing the camera") {
    const renderer::Ray ray{.origin = {0.0f, -5.0f, 5.0f}, .direction = glm::normalize(glm::vec3{0.0f, 1.0f, -1.0f})};
    const editor::Landing landing = editor::land(ray, std::nullopt, up, 10.0f, std::nullopt);
    CHECK(approxEqual(landing.point, {0.0f, 0.0f, 0.0f}));
    REQUIRE(landing.normal.has_value());
    CHECK(approxEqual(*landing.normal, -up));
  }

  SECTION("above the horizon: fallback metres out, with no normal") {
    const renderer::Ray ray{.origin = {0.0f, 1.0f, 0.0f}, .direction = glm::normalize(glm::vec3{0.0f, 0.5f, -1.0f})};
    const editor::Landing landing = editor::land(ray, std::nullopt, up, 10.0f, std::nullopt);
    CHECK(approxEqual(landing.point, ray.origin + ray.direction * 10.0f));
    CHECK_FALSE(landing.normal.has_value());
  }

  SECTION("snapping rounds X and Z and keeps a table top's height") {
    const editor::SceneHit table{.entity = {}, .point = {1.3f, 0.75f, -2.6f}, .normal = up};
    const editor::Landing landing = editor::land(Forward, table, up, 10.0f, 0.5f);
    CHECK(approxEqual(landing.point, {1.5f, 0.75f, -2.5f}));
  }

  SECTION("in an orthographic Front view, snapping rounds X and Y") {
    const glm::vec3 front{0.0f, 0.0f, 1.0f};
    const renderer::Ray ray{.origin = {1.3f, 2.7f, 50.0f}, .direction = {0.0f, 0.0f, -1.0f}};
    const editor::Landing landing = editor::land(ray, std::nullopt, front, 10.0f, 1.0f);
    CHECK(approxEqual(landing.point, {1.0f, 3.0f, 0.0f}));
    REQUIRE(landing.normal.has_value());
    CHECK(approxEqual(*landing.normal, front));
  }
}

TEST_CASE("restingPosition pushes an object out until its bounds touch the surface", "[editor][drop]") {
  DropFixture fixture;
  const renderer::MeshHandle box = fixture.database.mesh(assets::builtin::box());
  REQUIRE(box);
  const std::vector<renderer::DrawItem> items{drawItem(box)};

  SECTION("a unit box on the floor") {
    const editor::Landing floor{.point = {2.0f, 0.0f, -3.0f}, .normal = glm::vec3{0.0f, 1.0f, 0.0f}};
    CHECK(approxEqual(editor::restingPosition(floor, fixture.renderer, items, glm::mat4{1.0f}), {2.0f, 0.5f, -3.0f}));
  }

  SECTION("half its scaled size off a +X wall") {
    const editor::Landing wall{.point = {3.0f, 1.0f, 0.0f}, .normal = glm::vec3{1.0f, 0.0f, 0.0f}};
    const glm::mat4 pose = glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 1.0f, 1.0f});
    CHECK(approxEqual(editor::restingPosition(wall, fixture.renderer, items, pose), {4.0f, 1.0f, 0.0f}));
  }

  SECTION("an item's own transform counts") {
    const std::vector<renderer::DrawItem> raised{
        drawItem(box, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 2.0f, 0.0f}))};
    const editor::Landing floor{.point = {0.0f, 0.0f, 0.0f}, .normal = glm::vec3{0.0f, 1.0f, 0.0f}};
    CHECK(approxEqual(editor::restingPosition(floor, fixture.renderer, raised, glm::mat4{1.0f}), {0.0f, -1.5f, 0.0f}));
  }

  SECTION("unchanged in the air, or with nothing loaded") {
    const editor::Landing air{.point = {1.0f, 2.0f, 3.0f}, .normal = std::nullopt};
    CHECK(approxEqual(editor::restingPosition(air, fixture.renderer, items, glm::mat4{1.0f}), air.point));
    const editor::Landing floor{.point = {1.0f, 0.0f, 3.0f}, .normal = glm::vec3{0.0f, 1.0f, 0.0f}};
    CHECK(approxEqual(editor::restingPosition(floor, fixture.renderer, {}, glm::mat4{1.0f}), floor.point));
    const std::vector<renderer::DrawItem> pending{drawItem(renderer::MeshHandle{})};
    CHECK(approxEqual(editor::restingPosition(floor, fixture.renderer, pending, glm::mat4{1.0f}), floor.point));
  }
}
