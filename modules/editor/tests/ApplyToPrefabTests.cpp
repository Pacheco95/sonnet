// "Apply to prefab" (docs/editor.md, "Applying to a prefab"): an instance's change written into the
// prefab file the target rule picks, the instances rebuilt, and undo and redo of both.
#include <sonnet/editor/Editor.h>
#include <sonnet/editor/EntityCommands.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/scripting/Components.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/Scene.h>

#include <TestSupport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using Catch::Approx;
using nlohmann::json;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "editor_tests", .size = {800, 600}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "editor_tests"});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan 1.4 device: " << e.what());
    }
    const rhi::DeviceInfo &info = device->info();
    if (info.loaderVersion < VK_API_VERSION_1_4 && info.driverName != "llvmpipe") {
      SKIP("headless surfaces are not trusted on this loader and driver");
    }
    try {
      swapchain = device->createSwapchain(*window);
    } catch (const core::Exception &e) {
      SKIP("headless surfaces are not supported here: " << e.what());
    }
  }

  void frame(editor::Editor &editor) {
    editor.update(1.0f / 60.0f);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    editor.render(commands, image);
    device->endFrame();
    editor.afterPresent();
  }
};

std::filesystem::path scratch(const char *name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path.parent_path());
  return path;
}

void writeDocument(const std::filesystem::path &file, const json &entities) {
  std::filesystem::create_directories(file.parent_path());
  REQUIRE(
      core::writeFile(file, world::sceneText({{"version", world::SceneVersion}, {"entities", entities}})).has_value());
}

std::string text(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  std::string out(bytes->size(), '\0');
  std::ranges::transform(*bytes, out.begin(), [](std::byte b) { return static_cast<char>(b); });
  return out;
}

// The components a prefab file stores for the entity with that identity.
json stored(const std::filesystem::path &file, const core::Uuid &uuid) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  const json document = assets::parseJson(*bytes);
  for (const json &entry : document["entities"]) {
    if (entry["uuid"] == uuid.toString()) {
      return entry["components"];
    }
  }
  return nullptr;
}

// The members of `entity`'s component that differ from its source: what the inspector marks.
json marked(const world::World &world, flecs::entity entity, std::string_view component) {
  const world::ComponentInfo &info = *world.findComponent(component);
  const flecs::entity source = world.sourceOf(entity);
  return world::componentPatch(info, world.componentToJson(entity, info.id), world.componentToJson(source, info.id));
}

// Lamp: a root and a "Bulb" child with a point light and Static.
struct Lamp {
  core::Uuid root = core::Uuid::generate();
  core::Uuid bulb = core::Uuid::generate();

  void write(const std::filesystem::path &file) const {
    writeDocument(
        file,
        json::array({{{"uuid", root.toString()}, {"name", "Lamp"}, {"components", json::object()}},
                     {{"uuid", bulb.toString()},
                      {"name", "Bulb"},
                      {"parent", root.toString()},
                      {"components", {{"PointLight", {{"intensity", 5.0}, {"range", 10.0}}}, {"Static", nullptr}}}}}));
  }
};

} // namespace

TEST_CASE("applying a member writes the prefab file and the other instances follow it, undoably",
          "[editor][gpu][prefab][apply]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-member");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Lamp lamp;
  const std::filesystem::path file = directory / "prefabs" / "lamp.prefab.json";
  lamp.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  const core::Uuid c = core::Uuid::generate();
  for (const core::Uuid &uuid : {a, b, c}) {
    editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, uuid), world);
  }
  const auto bulb = [&](const core::Uuid &instance) { return world.findByPath(world.find(instance), "Bulb"); };
  const auto light = [&](const core::Uuid &instance) { return bulb(instance).get<world::PointLight>(); };
  const auto setLight = [&](const core::Uuid &instance, float intensity, float range) {
    const auto id = world.findComponent("PointLight")->id;
    const json before = world.componentToJson(bulb(instance), id);
    json after = before;
    after["intensity"] = intensity;
    after["range"] = range;
    editor.commands().push(editor::componentCommand(world.uuidOf(bulb(instance)), "PointLight", std::optional{before},
                                                    std::optional{after}, "light"),
                           world);
  };
  // A changes the intensity and the range; C sets an intensity of its own.
  setLight(a, 8.0f, 20.0f);
  setLight(c, 3.0f, 10.0f);
  REQUIRE(editor.saveScene().has_value());
  fixture.frame(editor);
  const std::string original = text(file);

  const editor::ApplyRequest request{
      .entity = world.uuidOf(bulb(a)), .component = "PointLight", .member = json::json_pointer{"/intensity"}};
  REQUIRE(editor.applyTargetName(request) == "Lamp");
  // An entity outside any instance, and an instance root's transform, offer nothing.
  CHECK(!editor.applyTargetName({.entity = a, .component = "Transform"}).has_value());
  REQUIRE(editor.applyToPrefab(request).has_value());

  // The file holds the value, B takes it, A no longer marks it and keeps the range it did not
  // apply, and C keeps its own intensity.
  CHECK(stored(file, lamp.bulb)["PointLight"]["intensity"] == 8.0);
  CHECK(stored(file, lamp.bulb)["PointLight"]["range"] == 10.0);
  CHECK(light(b).intensity == Approx(8.0f));
  CHECK(light(b).range == Approx(10.0f));
  CHECK(light(a).intensity == Approx(8.0f));
  CHECK(marked(world, bulb(a), "PointLight") == json{{"range", 20.0}});
  CHECK(light(c).intensity == Approx(3.0f));
  CHECK(marked(world, bulb(c), "PointLight") == json{{"intensity", 3.0}});
  // The scene follows the prefab now, which is an edit of the scene.
  CHECK(editor.isDirty());

  // The editor's own write does not make the next rescan rebuild the instances.
  const flecs::entity_t built = bulb(a).id();
  editor.event(platform::WindowFocusChanged{.focused = true});
  fixture.frame(editor);
  fixture.frame(editor);
  CHECK(bulb(a).id() == built);

  // Undo puts the file back as it was and A's change with it; B follows the old value again.
  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(light(a).intensity == Approx(8.0f));
  CHECK(marked(world, bulb(a), "PointLight") == json{{"intensity", 8.0}, {"range", 20.0}});
  CHECK(light(b).intensity == Approx(5.0f));
  CHECK(light(c).intensity == Approx(3.0f));
  CHECK(!editor.isDirty());
  // Redo applies again.
  REQUIRE(editor.commands().redo(world));
  CHECK(stored(file, lamp.bulb)["PointLight"]["intensity"] == 8.0);
  CHECK(light(b).intensity == Approx(8.0f));
  CHECK(marked(world, bulb(a), "PointLight") == json{{"range", 20.0}});

  // The inspector draws the changed instance with the apply on offer.
  editor.selection().select(world.uuidOf(bulb(a)));
  fixture.frame(editor);
  fixture.frame(editor);
}

TEST_CASE("applying inside a prefab of prefabs writes the file the target rule picks and lists the users",
          "[editor][gpu][prefab][apply][nested]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-nested");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  // Crate: a root with a grey Lid. Pallet: a root holding "Crate B", a crate whose lid it paints blue.
  const core::Uuid crate = core::Uuid::generate();
  const core::Uuid lid = core::Uuid::generate();
  const core::Uuid pallet = core::Uuid::generate();
  const core::Uuid crateB = core::Uuid::derive(pallet, "b");
  const std::filesystem::path crateFile = directory / "prefabs" / "crate.prefab.json";
  const std::filesystem::path palletFile = directory / "prefabs" / "pallet.prefab.json";
  writeDocument(
      crateFile,
      json::array(
          {{{"uuid", crate.toString()}, {"name", "Crate"}, {"components", json::object()}},
           {{"uuid", lid.toString()},
            {"name", "Lid"},
            {"parent", crate.toString()},
            {"components", {{"MeshRenderer", {{"color", json::array({0.5, 0.5, 0.5, 1.0})}, {"visible", true}}}}}}}));
  writeDocument(palletFile,
                json::array({{{"uuid", pallet.toString()}, {"name", "Pallet"}, {"components", json::object()}},
                             {{"uuid", crateB.toString()},
                              {"name", "Crate B"},
                              {"parent", pallet.toString()},
                              {"prefab", crate.toString()},
                              {"components", json::object()}},
                             {{"uuid", world::World::instancedIdentity(crateB, lid).toString()},
                              {"instanced", true},
                              {"components", {{"MeshRenderer", {{"color", json::array({0.0, 0.0, 1.0, 1.0})}}}}}}}));
  // A closed scene that places a pallet.
  writeDocument(directory / "scenes" / "closed.scene.json", json::array({{{"uuid", core::Uuid::generate().toString()},
                                                                          {"name", "Stored pallet"},
                                                                          {"prefab", pallet.toString()},
                                                                          {"components", json::object()}}}));
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid placed = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, placed), world);
  fixture.frame(editor);
  const auto sceneLid = [&] { return world.findByPath(world.find(placed), "Crate B/Lid"); };
  REQUIRE(sceneLid());
  const auto id = world.findComponent("MeshRenderer")->id;
  const auto edit = [&](const auto &change) {
    const json before = world.componentToJson(sceneLid(), id);
    json after = before;
    change(after);
    editor.commands().push(editor::componentCommand(world.uuidOf(sceneLid()), "MeshRenderer", std::optional{before},
                                                    std::optional{after}, "lid"),
                           world);
  };

  // The colour: the pallet sets it, so the pallet's file is the one written.
  edit([](json &value) { value["color"] = json::array({1.0, 0.0, 0.0, 1.0}); });
  const editor::ApplyRequest colour{
      .entity = world.uuidOf(sceneLid()), .component = "MeshRenderer", .member = json::json_pointer{"/color"}};
  REQUIRE(editor.applyTargetName(colour) == "Pallet");
  const std::string crateText = text(crateFile);
  REQUIRE(editor.applyToPrefab(colour).has_value());
  CHECK(text(crateFile) == crateText);
  CHECK(stored(palletFile, world::World::instancedIdentity(crateB, lid))["MeshRenderer"]["color"] ==
        json::array({1.0, 0.0, 0.0, 1.0}));
  CHECK(sceneLid().get<world::MeshRenderer>().color.r == Approx(1.0f));
  CHECK(marked(world, sceneLid(), "MeshRenderer").empty());

  // The visibility: the pallet leaves it to the crate, so the crate's file is written. The
  // confirmation lists the nesting prefab, the closed scene and the open tab before it does.
  edit([](json &value) { value["visible"] = false; });
  const editor::ApplyRequest visible{
      .entity = world.uuidOf(sceneLid()), .component = "MeshRenderer", .member = json::json_pointer{"/visible"}};
  REQUIRE(editor.applyTargetName(visible) == "Crate");
  editor.requestApplyToPrefab(visible);
  REQUIRE(editor.applyDialogOpen());
  const editor::PrefabUsers &users = editor.applyUsers();
  REQUIRE(users.prefabs.size() == 1);
  CHECK(users.prefabs[0].filename() == "pallet.prefab.json");
  REQUIRE(users.scenes.size() == 1);
  CHECK(users.scenes[0].filename() == "closed.scene.json");
  CHECK(users.tabs.size() == 1);
  fixture.frame(editor); // the dialog draws
  const std::string palletText = text(palletFile);
  editor.confirmApply();
  CHECK(!editor.applyDialogOpen());
  CHECK(stored(crateFile, lid)["MeshRenderer"]["visible"] == false);
  CHECK(text(palletFile) == palletText);
  CHECK(!sceneLid().get<world::MeshRenderer>().visible);
  CHECK(marked(world, sceneLid(), "MeshRenderer").empty());

  // Cancelling writes nothing.
  edit([](json &value) { value["visible"] = true; });
  editor.requestApplyToPrefab(visible);
  REQUIRE(editor.applyDialogOpen());
  editor.cancelApply();
  CHECK(stored(crateFile, lid)["MeshRenderer"]["visible"] == false);
}

TEST_CASE("applying a script property writes it into the prefab's slot", "[editor][gpu][prefab][apply][scripts]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-script");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  world::World &world = editor.world();
  const auto mover = editor.assets().createScript(directory / "scripts" / "mover.lua", R"lua(
      return { properties = { speed = { type = "number", default = 1 }, range = { type = "number", default = 1 } } }
    )lua");
  REQUIRE(mover.has_value());
  const flecs::entity turret = world.createEntity("Turret");
  turret.set<scripting::Scripts>({.slots = {{.script = *mover, .properties = R"({"range":5,"speed":2})"}}});
  // Save as prefab replaces the entity by an instance under the same identity.
  const core::Uuid first = world.uuidOf(turret);
  const auto file = editor.saveAsPrefab(first);
  REQUIRE(file.has_value());
  fixture.frame(editor);
  const core::Uuid prefab = world.uuidOf(world.prefabOf(world.find(first)));
  const core::Uuid second = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(prefab, "Turret 2", {}, second), world);
  const auto properties = [&](const core::Uuid &uuid) {
    return json::parse(world.find(uuid).get<scripting::Scripts>().slots[0].properties);
  };

  // The first changes both properties and applies the speed.
  const world::ComponentInfo *info = world.findComponent("Scripts");
  const json before = world.componentToJson(world.find(first), info->id);
  scripting::Scripts edited = world.find(first).get<scripting::Scripts>();
  edited.slots[0].properties = R"({"range":7,"speed":6})";
  editor.commands().push(editor::componentCommand(first, "Scripts", std::optional{before},
                                                  std::optional{world.valueToJson(info->id, &edited)}, "edit"),
                         world);
  const editor::ApplyRequest request{.entity = first,
                                     .component = "Scripts",
                                     .member = json::json_pointer{"/slots/0/properties/speed"},
                                     .slot = 0,
                                     .property = "speed"};
  REQUIRE(editor.applyTargetName(request).has_value());
  REQUIRE(editor.applyToPrefab(request).has_value());
  const json slot = stored(*file, prefab)["Scripts"]["slots"][0]["properties"];
  CHECK(slot["speed"] == 6);
  CHECK(slot["range"] == 5);
  CHECK(properties(second)["speed"] == 6);
  CHECK(properties(second)["range"] == 5);
  CHECK(properties(first)["speed"] == 6);
  CHECK(properties(first)["range"] == 7);

  // A slot list the instance reshaped is not paired with the prefab's.
  scripting::Scripts reshaped = world.find(first).get<scripting::Scripts>();
  reshaped.slots.push_back({.script = *mover});
  world.find(first).set<scripting::Scripts>(reshaped);
  CHECK(!editor.applyTargetName(request).has_value());
}

TEST_CASE("an added and a removed component are applied to the prefab", "[editor][gpu][prefab][apply]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-components");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Lamp lamp;
  const std::filesystem::path file = directory / "prefabs" / "lamp.prefab.json";
  lamp.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  for (const core::Uuid &uuid : {a, b}) {
    editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, uuid), world);
  }
  const auto bulb = [&](const core::Uuid &instance) { return world.findByPath(world.find(instance), "Bulb"); };
  const core::Uuid bulbA = world.uuidOf(bulb(a));
  // A hides its bulb (Disabled is a component) and drops its Static.
  editor.commands().push(editor::componentCommand(bulbA, "Disabled", std::nullopt, std::optional{json{}}, "hide"),
                         world);
  editor.commands().push(editor::componentCommand(bulbA, "Static", std::optional{json{}}, std::nullopt, "remove"),
                         world);
  REQUIRE(editor.applyToPrefab({.entity = bulbA, .component = "Disabled"}).has_value());
  CHECK(stored(file, lamp.bulb).contains("Disabled"));
  CHECK(bulb(b).has<world::Disabled>());
  REQUIRE(editor.applyToPrefab({.entity = bulbA, .component = "Static"}).has_value());
  CHECK(!stored(file, lamp.bulb).contains("Static"));
  CHECK(!bulb(b).has<world::Static>());
  CHECK(!bulb(a).has<world::Static>());
  // Both now follow the prefab: A's bulb stores nothing of its own.
  const json scene = world::saveScene(world);
  CHECK(std::ranges::none_of(scene["entities"], [&](const json &entry) { return entry["uuid"] == bulbA.toString(); }));
  // A removal is not offered where the prefab no longer has the component.
  CHECK(!editor.applyTargetName({.entity = bulbA, .component = "Static"}).has_value());

  // Undoing the removal puts Static back in the file and in B, and A keeps it removed.
  REQUIRE(editor.commands().undo(world));
  CHECK(stored(file, lamp.bulb).contains("Static"));
  CHECK(bulb(b).has<world::Static>());
  CHECK(!bulb(a).has<world::Static>());
  REQUIRE(editor.commands().undo(world));
  CHECK(!stored(file, lamp.bulb).contains("Disabled"));
  CHECK(!bulb(b).has<world::Disabled>());
  CHECK(bulb(a).has<world::Disabled>());
}

TEST_CASE("apply to prefab is not offered while playing", "[editor][gpu][prefab][apply][play]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-play");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Lamp lamp;
  const std::filesystem::path file = directory / "prefabs" / "lamp.prefab.json";
  lamp.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, a), world);
  const core::Uuid bulbA = world.uuidOf(world.findByPath(world.find(a), "Bulb"));
  editor.commands().push(editor::componentCommand(bulbA, "Disabled", std::nullopt, std::optional{json{}}, "hide"),
                         world);
  const editor::ApplyRequest request{.entity = bulbA, .component = "Disabled"};
  REQUIRE(editor.applyTargetName(request).has_value());
  const std::string original = text(file);

  editor.play();
  fixture.frame(editor);
  CHECK(!editor.applyTargetName(request).has_value());
  CHECK(!editor.applyToPrefab(request).has_value());
  editor.requestApplyToPrefab(request);
  CHECK(!editor.applyDialogOpen());
  CHECK(text(file) == original);
  editor.stop();
  fixture.frame(editor);
  CHECK(editor.applyTargetName(request).has_value());
}
