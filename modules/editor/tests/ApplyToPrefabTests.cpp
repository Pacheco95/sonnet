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
#include <format>
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
  // The crate's lid colour no longer reaches the scene, so the pallet is the only choice.
  REQUIRE(editor.applyChoices(colour).size() == 1);
  CHECK(editor.applyChoices(colour)[0].prefab == pallet);
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

  // An outer prefab instead (#549): the rule picks the crate, the pallet is offered after it, and
  // choosing it writes the pallet's file with the same confirmation, undo and rebuild.
  const std::vector<editor::ApplyChoice> choices = editor.applyChoices(visible);
  REQUIRE(choices.size() == 2);
  CHECK(choices[0].prefab == crate);
  CHECK(choices[0].name == "Crate");
  CHECK(choices[1].prefab == pallet);
  CHECK(choices[1].name == "Pallet");
  editor::ApplyRequest outer = visible;
  outer.prefab = pallet;
  CHECK(editor.applyTargetName(outer) == "Pallet");
  // A prefab that is not on the chain is not a target.
  editor::ApplyRequest stranger = visible;
  stranger.prefab = core::Uuid::generate();
  CHECK(!editor.applyTargetName(stranger).has_value());
  editor.requestApplyToPrefab(outer);
  REQUIRE(editor.applyDialogOpen());
  // What places the pallet: the closed scene and the open tab, no other prefab.
  CHECK(editor.applyUsers().prefabs.empty());
  REQUIRE(editor.applyUsers().scenes.size() == 1);
  CHECK(editor.applyUsers().scenes[0].filename() == "closed.scene.json");
  fixture.frame(editor); // the dialog draws, naming the pallet
  const std::string crateBefore = text(crateFile);
  const std::string palletBefore = text(palletFile);
  const std::size_t rebuilds = editor.prefabRebuilds();
  editor.confirmApply();
  CHECK(!editor.applyDialogOpen());
  CHECK(editor.prefabRebuilds() == rebuilds + 1);
  CHECK(text(crateFile) == crateBefore);
  CHECK(stored(palletFile, world::World::instancedIdentity(crateB, lid))["MeshRenderer"]["visible"] == true);
  CHECK(sceneLid().get<world::MeshRenderer>().visible);
  CHECK(marked(world, sceneLid(), "MeshRenderer").empty());
  // The crate still hides its lid; only the pallet's crate shows it.
  CHECK(stored(crateFile, lid)["MeshRenderer"]["visible"] == false);
  REQUIRE(editor.commands().undo(world));
  CHECK(text(palletFile) == palletBefore);
  CHECK(sceneLid().get<world::MeshRenderer>().visible);
  CHECK(marked(world, sceneLid(), "MeshRenderer") == json{{"visible", true}});
  REQUIRE(editor.commands().redo(world));
  CHECK(stored(palletFile, world::World::instancedIdentity(crateB, lid))["MeshRenderer"]["visible"] == true);
  // Now the pallet sets the visibility, so it is the rule's pick and the only choice.
  REQUIRE(editor.applyChoices(visible).size() == 1);
  CHECK(editor.applyChoices(visible)[0].prefab == pallet);
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

namespace {

// Beacon: a root with a point light and a "Bulb" child with a mesh renderer.
struct Beacon {
  core::Uuid root = core::Uuid::generate();
  core::Uuid bulb = core::Uuid::generate();

  void write(const std::filesystem::path &file) const {
    writeDocument(file, json::array({{{"uuid", root.toString()},
                                      {"name", "Beacon"},
                                      {"components", {{"PointLight", {{"intensity", 5.0}, {"range", 10.0}}}}}},
                                     {{"uuid", bulb.toString()},
                                      {"name", "Bulb"},
                                      {"parent", root.toString()},
                                      {"components", {{"MeshRenderer", {{"visible", true}}}}}}}));
  }
};

std::size_t changesTo(const editor::ApplyAllPlan &plan, const std::filesystem::path &file) {
  const auto it = std::ranges::find(plan.files, file, &editor::ApplyAllPlan::File::path);
  return it == plan.files.end() ? 0 : it->changes.size();
}

bool lists(const editor::ApplyAllPlan &plan, std::string_view text) {
  return std::ranges::any_of(plan.kept, [&](const std::string &kept) { return kept.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("apply all overrides writes the root's and the descendants' changes in one undo step",
          "[editor][gpu][prefab][apply][all]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-all");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Beacon beacon;
  const std::filesystem::path file = directory / "prefabs" / "beacon.prefab.json";
  beacon.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  const core::Uuid c = core::Uuid::generate();
  for (const core::Uuid &uuid : {a, b, c}) {
    editor.commands().push(editor::instantiatePrefabCommand(beacon.root, "Beacon", {}, uuid), world);
  }
  const auto bulb = [&](const core::Uuid &instance) { return world.findByPath(world.find(instance), "Bulb"); };
  const auto set = [&](flecs::entity entity, std::string_view component, const auto &change) {
    const auto id = world.findComponent(component)->id;
    const json before = world.componentToJson(entity, id);
    json after = before;
    change(after);
    editor.commands().push(editor::componentCommand(world.uuidOf(entity), std::string{component}, std::optional{before},
                                                    std::optional{after}, "edit"),
                           world);
  };
  // A changes both of the root's light members and hides its bulb's mesh; C sets its own range.
  set(world.find(a), "PointLight", [](json &value) {
    value["intensity"] = 8.0;
    value["range"] = 20.0;
  });
  set(bulb(a), "MeshRenderer", [](json &value) { value["visible"] = false; });
  set(world.find(c), "PointLight", [](json &value) { value["range"] = 3.0; });
  REQUIRE(editor.saveSceneAs(directory / "scenes" / "beacons.scene.json").has_value());
  fixture.frame(editor);
  const std::string original = text(file);

  // Offered on the instance root only, and for B, which changes nothing, not at all.
  const editor::ApplyAllPlan plan = editor.applyAllPlan(a);
  REQUIRE(plan.files.size() == 1);
  CHECK(plan.files[0].path == file);
  CHECK(plan.files[0].prefab == "Beacon");
  CHECK(changesTo(plan, file) == 3);
  CHECK(editor.applyAllPlan(b).files.empty());
  CHECK(editor.applyAllPlan(world.uuidOf(bulb(a))).files.empty());

  const std::size_t undoBefore = editor.commands().size();
  const std::size_t rebuildsBefore = editor.prefabRebuilds();
  REQUIRE(editor.applyAllOverrides(a).has_value());
  CHECK(editor.commands().size() == undoBefore + 1);
  CHECK(editor.prefabRebuilds() == rebuildsBefore + 1);
  CHECK(stored(file, beacon.root)["PointLight"]["intensity"] == 8.0);
  CHECK(stored(file, beacon.root)["PointLight"]["range"] == 20.0);
  CHECK(stored(file, beacon.bulb)["MeshRenderer"]["visible"] == false);
  // B takes everything, A follows its prefab again (its root drops the override), C keeps its range.
  CHECK(world.find(b).get<world::PointLight>().intensity == Approx(8.0f));
  CHECK(world.find(b).get<world::PointLight>().range == Approx(20.0f));
  CHECK(!bulb(b).get<world::MeshRenderer>().visible);
  CHECK(!world.find(a).owns<world::PointLight>());
  CHECK(marked(world, bulb(a), "MeshRenderer").empty());
  CHECK(world.find(c).get<world::PointLight>().intensity == Approx(8.0f));
  CHECK(world.find(c).get<world::PointLight>().range == Approx(3.0f));
  CHECK(editor.applyAllPlan(a).files.empty()); // nothing left to apply

  // One undo puts the file and A's values back; redo applies again.
  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(world.find(a).get<world::PointLight>().intensity == Approx(8.0f));
  CHECK(world.find(b).get<world::PointLight>().intensity == Approx(5.0f));
  CHECK(bulb(b).get<world::MeshRenderer>().visible);
  CHECK(!bulb(a).get<world::MeshRenderer>().visible);
  CHECK(changesTo(editor.applyAllPlan(a), file) == 3);
  REQUIRE(editor.commands().redo(world));
  CHECK(stored(file, beacon.root)["PointLight"]["intensity"] == 8.0);
  CHECK(world.find(b).get<world::PointLight>().intensity == Approx(8.0f));

  // Saved and opened again, the instances read the new prefab: B takes what it did not override,
  // C keeps its own range.
  REQUIRE(editor.saveScene().has_value());
  REQUIRE(editor.openProject(directory).has_value());
  REQUIRE(editor.openScene(directory / "scenes" / "beacons.scene.json").has_value());
  world::World &reloaded = editor.world();
  CHECK(reloaded.find(b).get<world::PointLight>().intensity == Approx(8.0f));
  CHECK(!reloaded.findByPath(reloaded.find(b), "Bulb").get<world::MeshRenderer>().visible);
  CHECK(reloaded.find(c).get<world::PointLight>().intensity == Approx(8.0f));
  CHECK(reloaded.find(c).get<world::PointLight>().range == Approx(3.0f));
  CHECK(!reloaded.find(a).owns<world::PointLight>());
}

TEST_CASE("apply all overrides of a nested instance writes each change to the file its target rule picks",
          "[editor][gpu][prefab][apply][all][nested]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-all-nested");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  // The crate and pallet of the single apply's nested case.
  const core::Uuid crate = core::Uuid::generate();
  const core::Uuid lid = core::Uuid::generate();
  const core::Uuid pallet = core::Uuid::generate();
  const core::Uuid crateB = core::Uuid::derive(pallet, "b");
  const core::Uuid palletLid = world::World::instancedIdentity(crateB, lid);
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
                             {{"uuid", palletLid.toString()},
                              {"instanced", true},
                              {"components", {{"MeshRenderer", {{"color", json::array({0.0, 0.0, 1.0, 1.0})}}}}}}}));
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid placed = core::Uuid::generate();
  const core::Uuid other = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, placed), world);
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, other), world);
  fixture.frame(editor);
  const auto sceneLid = [&](const core::Uuid &instance) {
    return world.findByPath(world.find(instance), "Crate B/Lid");
  };
  REQUIRE(sceneLid(placed));
  const auto id = world.findComponent("MeshRenderer")->id;
  const json before = world.componentToJson(sceneLid(placed), id);
  json after = before;
  after["color"] = json::array({1.0, 0.0, 0.0, 1.0});
  after["visible"] = false;
  editor.commands().push(editor::componentCommand(world.uuidOf(sceneLid(placed)), "MeshRenderer", std::optional{before},
                                                  std::optional{after}, "lid"),
                         world);
  const std::string crateText = text(crateFile);
  const std::string palletText = text(palletFile);

  // The colour goes to the pallet, which sets it; the visibility to the crate, which the pallet
  // leaves it to. The confirmation lists both files with one change each.
  editor.requestApplyAllOverrides(placed);
  REQUIRE(editor.applyAllDialogOpen());
  const editor::ApplyAllPlan &plan = editor.pendingApplyAll();
  REQUIRE(plan.files.size() == 2);
  CHECK(changesTo(plan, palletFile) == 1);
  CHECK(changesTo(plan, crateFile) == 1);
  fixture.frame(editor); // the dialog draws
  editor.confirmApply();
  CHECK(!editor.applyAllDialogOpen());
  CHECK(stored(palletFile, palletLid)["MeshRenderer"]["color"] == json::array({1.0, 0.0, 0.0, 1.0}));
  CHECK(stored(crateFile, lid)["MeshRenderer"]["visible"] == false);
  CHECK(sceneLid(other).get<world::MeshRenderer>().color.r == Approx(1.0f));
  CHECK(!sceneLid(other).get<world::MeshRenderer>().visible);
  CHECK(marked(world, sceneLid(placed), "MeshRenderer").empty());

  // One undo restores both files, with one rebuild.
  const std::size_t rebuildsBefore = editor.prefabRebuilds();
  REQUIRE(editor.commands().undo(world));
  CHECK(editor.prefabRebuilds() == rebuildsBefore + 1);
  CHECK(text(crateFile) == crateText);
  CHECK(text(palletFile) == palletText);
  CHECK(sceneLid(other).get<world::MeshRenderer>().color.b == Approx(1.0f));
  CHECK(sceneLid(placed).get<world::MeshRenderer>().color.r == Approx(1.0f));
  CHECK(!sceneLid(placed).get<world::MeshRenderer>().visible);
  REQUIRE(editor.commands().redo(world));
  CHECK(stored(crateFile, lid)["MeshRenderer"]["visible"] == false);

  // The other pallet has nothing to apply, so no confirmation opens.
  editor.requestApplyAllOverrides(other);
  CHECK(!editor.applyAllDialogOpen());
}

TEST_CASE("apply all overrides lists what it cannot apply and leaves it on the instance",
          "[editor][gpu][prefab][apply][all]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-all-kept");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Beacon beacon;
  const std::filesystem::path file = directory / "prefabs" / "beacon.prefab.json";
  beacon.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(beacon.root, "Beacon", {}, a), world);
  const flecs::entity root = world.find(a);
  const auto bulb = [&] { return world.findByPath(world.find(a), "Bulb"); };
  // Applicable: the light's intensity and the bulb's new name. Not: the root's place, an added child.
  const auto lightId = world.findComponent("PointLight")->id;
  json light = world.componentToJson(root, lightId);
  const json lightBefore = light;
  light["intensity"] = 9.0;
  editor.commands().push(
      editor::componentCommand(a, "PointLight", std::optional{lightBefore}, std::optional{light}, "l"), world);
  const auto transformId = world.findComponent("Transform")->id;
  json place = world.componentToJson(root, transformId);
  const json placeBefore = place;
  place["position"] = json::array({4.0, 0.0, 0.0});
  editor.commands().push(
      editor::componentCommand(a, "Transform", std::optional{placeBefore}, std::optional{place}, "move"), world);
  editor.commands().push(editor::renameCommand(world.uuidOf(bulb()), "Bulb", "Lamp bulb"), world);
  world.createEntity("Antenna", world.find(a));

  const editor::ApplyAllPlan plan = editor.applyAllPlan(a);
  REQUIRE(plan.files.size() == 1);
  CHECK(changesTo(plan, file) == 2);
  CHECK(lists(plan, "Transform"));
  CHECK(!lists(plan, "the name Lamp bulb"));
  CHECK(lists(plan, "the added child Antenna"));
  editor.requestApplyAllOverrides(a);
  REQUIRE(editor.applyAllDialogOpen());
  fixture.frame(editor); // the dialog draws the kept list
  editor.confirmApply();

  CHECK(stored(file, beacon.root)["PointLight"]["intensity"] == 9.0);
  const json bulbEntry = [&] {
    const json document = assets::parseJson(*core::readFile(file));
    for (const json &entry : document["entities"]) {
      if (entry["uuid"] == beacon.bulb.toString()) {
        return entry;
      }
    }
    return json{};
  }();
  CHECK(bulbEntry["name"] == "Lamp bulb");
  CHECK(world.find(a).get<world::Transform>().position.x == Approx(4.0f));
  CHECK(world.findByPath(world.find(a), "Lamp bulb"));
  CHECK(world.findByPath(world.find(a), "Antenna"));
  // What stays is still listed, with nothing left to apply.
  const editor::ApplyAllPlan after = editor.applyAllPlan(a);
  CHECK(after.files.empty());
  CHECK(lists(after, "the added child Antenna"));
  // Not offered while playing.
  editor.play();
  fixture.frame(editor);
  CHECK(editor.applyAllPlan(a).files.empty());
  CHECK(editor.applyAllPlan(a).kept.empty());
  CHECK(!editor.applyAllOffered(a));
  editor.stop();
}

TEST_CASE("apply all is offered exactly when its plan lists a file", "[editor][gpu][prefab][apply][all]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-all-offered");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Beacon beacon;
  beacon.write(directory / "prefabs" / "beacon.prefab.json");
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(beacon.root, "Beacon", {}, a), world);
  const core::Uuid bulb = world.uuidOf(world.findByPath(world.find(a), "Bulb"));
  const auto agrees = [&](const core::Uuid &uuid) {
    return editor.applyAllOffered(uuid) == !editor.applyAllPlan(uuid).files.empty();
  };
  const auto edit = [&](flecs::entity entity, std::string_view component, const auto &change) {
    const auto id = world.findComponent(component)->id;
    const json before = world.componentToJson(entity, id);
    json after = before;
    change(after);
    editor.commands().push(editor::componentCommand(world.uuidOf(entity), std::string{component}, std::optional{before},
                                                    std::optional{after}, "edit"),
                           world);
  };

  // Untouched, and with only what stays on the instance (its place, an added child).
  CHECK(!editor.applyAllOffered(a));
  edit(world.find(a), "Transform", [](json &value) { value["position"] = json::array({4.0, 0.0, 0.0}); });
  world.createEntity("Antenna", world.find(a));
  CHECK(!editor.applyAllOffered(a));
  CHECK(!editor.applyAllPlan(a).kept.empty());
  // A rename is offered (#550); undoing it takes the offer back.
  editor.commands().push(editor::renameCommand(bulb, "Bulb", "Lamp bulb"), world);
  CHECK(editor.applyAllOffered(a));
  CHECK(agrees(a));
  REQUIRE(editor.commands().undo(world));
  CHECK(!editor.applyAllOffered(a));
  // A descendant's change is offered; undoing it takes the offer back, redoing brings it again.
  edit(world.find(bulb), "MeshRenderer", [](json &value) { value["visible"] = false; });
  CHECK(editor.applyAllOffered(a));
  CHECK(!editor.applyAllOffered(bulb)); // not an instance root
  REQUIRE(editor.commands().undo(world));
  CHECK(!editor.applyAllOffered(a));
  REQUIRE(editor.commands().redo(world));
  CHECK(editor.applyAllOffered(a));
  CHECK(agrees(a));
  // Applied, nothing is left; the root's own change is offered too.
  REQUIRE(editor.applyAllOverrides(a).has_value());
  CHECK(!editor.applyAllOffered(a));
  edit(world.find(a), "PointLight", [](json &value) { value["range"] = 2.0; });
  CHECK(editor.applyAllOffered(a));
  CHECK(agrees(a));
}

TEST_CASE("the cost of deciding whether to offer apply all on a large instance", "[.][benchmark][apply][all]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-all-benchmark");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  // Tower: a root with a point light and 300 parts, each with a place and a mesh renderer.
  constexpr int parts = 300;
  const core::Uuid tower = core::Uuid::generate();
  json entities = json::array({{{"uuid", tower.toString()},
                                {"name", "Tower"},
                                {"components", {{"PointLight", {{"intensity", 5.0}, {"range", 10.0}}}}}}});
  for (int i = 0; i < parts; ++i) {
    entities.push_back({{"uuid", core::Uuid::generate().toString()},
                        {"name", std::format("Part {}", i)},
                        {"parent", tower.toString()},
                        {"components",
                         {{"Transform", {{"position", json::array({0.0, static_cast<double>(i), 0.0})}}},
                          {"MeshRenderer", {{"visible", true}}}}}});
  }
  writeDocument(directory / "prefabs" / "tower.prefab.json", entities);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(tower, "Tower", {}, a), world);
  fixture.frame(editor);

  using Clock = std::chrono::steady_clock;
  const auto best = [](const auto &work) {
    double fastest = 1e9;
    for (int run = 0; run < 20; ++run) {
      const auto start = Clock::now();
      work();
      fastest = std::min(fastest, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
    return fastest;
  };
  const auto offered = [&] { return editor.applyAllOffered(a); };
  const auto edit = [&](flecs::entity entity, std::string_view component, const auto &change) {
    const auto id = world.findComponent(component)->id;
    const json before = world.componentToJson(entity, id);
    json after = before;
    change(after);
    editor.commands().push(editor::componentCommand(world.uuidOf(entity), std::string{component}, std::optional{before},
                                                    std::optional{after}, "edit"),
                           world);
  };
  const auto part = [&](int i) { return world.findByPath(world.find(a), std::format("Part {}", i)); };
  REQUIRE(part(parts - 1));

  CHECK(!offered());
  const double untouched = best(offered);
  // One change, on the last part the walk reaches.
  edit(part(parts - 1), "MeshRenderer", [](json &value) { value["visible"] = false; });
  CHECK(offered());
  const double lastChanged = best(offered);
  // Every part moved.
  for (int i = 0; i < parts; ++i) {
    edit(part(i), "Transform", [i](json &value) { value["position"] = json::array({1.0, i, 0.0}); });
  }
  CHECK(offered());
  const double allChanged = best(offered);
  const double plan = best([&] { return editor.applyAllPlan(a); });
  CHECK(offered() == !editor.applyAllPlan(a).files.empty());
  WARN(std::format("{} parts: offered untouched {:.3f} ms, last part changed {:.3f} ms, every part changed {:.3f} ms; "
                   "full plan with every part changed {:.3f} ms",
                   parts, untouched, lastChanged, allChanged, plan));
}

namespace {

// The entry a document stores for the entity with that identity, null when it stores none.
json entryOf(const json &document, const core::Uuid &uuid) {
  for (const json &entry : document["entities"]) {
    if (entry["uuid"] == uuid.toString()) {
      return entry;
    }
  }
  return nullptr;
}

json entryOf(const std::filesystem::path &file, const core::Uuid &uuid) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  return entryOf(assets::parseJson(*bytes), uuid);
}

// Whether the scene stores a name of its own for the entity: a rename of a descendant, or an
// instance root's name, which it always stores.
bool storesName(const world::World &world, const core::Uuid &uuid) {
  const json entry = entryOf(world::saveScene(world), uuid);
  return entry.is_object() && entry.contains("name");
}

editor::ApplyRequest nameRequest(const core::Uuid &entity) {
  return {.entity = entity, .component = {}, .kind = editor::ApplyKind::Name};
}

} // namespace

TEST_CASE("applying a descendant's rename renames the prefab entity and the instances that follow it, undoably",
          "[editor][gpu][prefab][apply][name]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-rename");
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
  const core::Uuid d = core::Uuid::generate();
  for (const core::Uuid &uuid : {a, b, c, d}) {
    editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, uuid), world);
  }
  const auto bulbOf = [&](const core::Uuid &instance) { return world::World::instancedIdentity(instance, lamp.bulb); };
  const auto nameOf = [&](const core::Uuid &instance) { return world.find(bulbOf(instance)).get<world::Name>().value; };
  // A renames its bulb Globe; C names its own Lightbulb, and D happens to name its own Globe too.
  editor.commands().push(editor::renameCommand(bulbOf(a), "Bulb", "Globe"), world);
  editor.commands().push(editor::renameCommand(bulbOf(c), "Bulb", "Lightbulb"), world);
  editor.commands().push(editor::renameCommand(bulbOf(d), "Bulb", "Globe"), world);
  REQUIRE(editor.saveSceneAs(directory / "scenes" / "lamps.scene.json").has_value());
  fixture.frame(editor);
  const std::string original = text(file);
  editor.selection().select(bulbOf(a));

  const editor::ApplyRequest request = nameRequest(bulbOf(a));
  REQUIRE(editor.applyTargetName(request) == "Lamp");
  REQUIRE(editor.applyChoices(request).size() == 1);
  // B's bulb has the prefab's name, so it has nothing to apply.
  CHECK(!editor.applyTargetName(nameRequest(bulbOf(b))));
  // An entity outside any instance has nowhere to go.
  const flecs::entity loose = world.createEntity("Loose");
  CHECK(!editor.applyTargetName(nameRequest(world.uuidOf(loose))));
  world.destroyEntity(loose);

  // The confirmation names the scene and the open tab placing the prefab; cancelling writes nothing.
  editor.requestApplyToPrefab(request);
  REQUIRE(editor.applyDialogOpen());
  CHECK(editor.applyUsers().tabs.size() == 1);
  REQUIRE(editor.applyUsers().scenes.size() == 1);
  CHECK(editor.applyUsers().scenes[0].filename() == "lamps.scene.json");
  fixture.frame(editor); // the dialog draws, with the warning about script paths
  editor.cancelApply();
  CHECK(text(file) == original);
  CHECK(nameOf(b) == "Bulb");

  const std::size_t undoBefore = editor.commands().size();
  editor.requestApplyToPrefab(request);
  REQUIRE(editor.applyDialogOpen());
  editor.confirmApply();
  CHECK(editor.commands().size() == undoBefore + 1);
  // The prefab's bulb is renamed in place: same identity and parent, its components as they were.
  CHECK(entryOf(file, lamp.bulb)["name"] == "Globe");
  CHECK(entryOf(file, lamp.bulb)["parent"] == lamp.root.toString());
  CHECK(stored(file, lamp.bulb)["PointLight"]["intensity"] == 5.0);
  CHECK(stored(file, lamp.bulb).contains("Static"));
  CHECK(entryOf(file, lamp.root)["name"] == "Lamp");
  // B follows, A follows the prefab now and stores no rename, C keeps its own, and D's equal
  // rename becomes the inherited name.
  CHECK(nameOf(a) == "Globe");
  CHECK(nameOf(b) == "Globe");
  CHECK(nameOf(c) == "Lightbulb");
  CHECK(nameOf(d) == "Globe");
  CHECK(!storesName(world, bulbOf(a)));
  CHECK(!storesName(world, bulbOf(b)));
  CHECK(storesName(world, bulbOf(c)));
  CHECK(!storesName(world, bulbOf(d)));
  // Identity and selection stay, and a path finds the bulb by its new name.
  CHECK(editor.selection().contains(bulbOf(a)));
  CHECK(world.findByPath(world.find(b), "Globe") == world.find(bulbOf(b)));
  CHECK(!editor.applyTargetName(request));
  CHECK(editor.isDirty());

  // Undo puts the file and every instance back, D's own rename included.
  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(nameOf(a) == "Globe");
  CHECK(nameOf(b) == "Bulb");
  CHECK(nameOf(c) == "Lightbulb");
  CHECK(nameOf(d) == "Globe");
  CHECK(storesName(world, bulbOf(a)));
  CHECK(storesName(world, bulbOf(d)));
  CHECK(editor.selection().contains(bulbOf(a)));
  CHECK(!editor.isDirty());
  // Redo applies again.
  REQUIRE(editor.commands().redo(world));
  CHECK(entryOf(file, lamp.bulb)["name"] == "Globe");
  CHECK(nameOf(b) == "Globe");
  CHECK(nameOf(c) == "Lightbulb");

  // Saved and opened again, the instances read the renamed prefab.
  REQUIRE(editor.saveScene().has_value());
  REQUIRE(editor.openProject(directory).has_value());
  REQUIRE(editor.openScene(directory / "scenes" / "lamps.scene.json").has_value());
  world::World &reloaded = editor.world();
  CHECK(reloaded.find(bulbOf(b)).get<world::Name>().value == "Globe");
  CHECK(reloaded.find(bulbOf(c)).get<world::Name>().value == "Lightbulb");
  CHECK(reloaded.find(bulbOf(d)).get<world::Name>().value == "Globe");
  CHECK(!storesName(reloaded, bulbOf(a)));
}

TEST_CASE("applying an instance root's name renames the prefab's root and every instance keeps its own",
          "[editor][gpu][prefab][apply][name]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-root-name");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Lamp lamp;
  const std::filesystem::path file = directory / "prefabs" / "lamp.prefab.json";
  lamp.write(file);
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  // A is renamed Desk lamp, B keeps the prefab's name, C has a name of its own and renames its bulb.
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  const core::Uuid c = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, a), world);
  editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Lamp", {}, b), world);
  editor.commands().push(editor::instantiatePrefabCommand(lamp.root, "Floor lamp", {}, c), world);
  editor.commands().push(editor::renameCommand(a, "Lamp", "Desk lamp"), world);
  const core::Uuid bulbB = world::World::instancedIdentity(b, lamp.bulb);
  const core::Uuid bulbC = world::World::instancedIdentity(c, lamp.bulb);
  editor.commands().push(editor::renameCommand(bulbC, "Bulb", "Globe"), world);
  fixture.frame(editor);
  const std::string original = text(file);
  const auto name = [&](const core::Uuid &uuid) { return world.find(uuid).get<world::Name>().value; };

  const editor::ApplyRequest request = nameRequest(a);
  REQUIRE(editor.applyTargetName(request) == "Lamp");
  // B is named like the prefab's root: nothing to apply.
  CHECK(!editor.applyTargetName(nameRequest(b)));
  // Apply all takes the root's name and the descendant's rename alike.
  const editor::ApplyAllPlan plan = editor.applyAllPlan(c);
  REQUIRE(plan.files.size() == 1);
  CHECK(changesTo(plan, file) == 2);

  editor.requestApplyToPrefab(request);
  REQUIRE(editor.applyDialogOpen());
  fixture.frame(editor); // the dialog draws, saying instances keep their names
  editor.confirmApply();
  CHECK(entryOf(file, lamp.root)["name"] == "Desk lamp");
  CHECK(world.find(lamp.root).get<world::Name>().value == "Desk lamp");
  // Unlike a descendant's name, a root's is the instance's own: B and C keep theirs, B's even
  // though it was the prefab's old name, and every root still stores its name.
  CHECK(name(a) == "Desk lamp");
  CHECK(name(b) == "Lamp");
  CHECK(name(c) == "Floor lamp");
  CHECK(storesName(world, a));
  CHECK(storesName(world, b));
  CHECK(name(bulbC) == "Globe");
  CHECK(name(bulbB) == "Bulb");

  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(world.find(lamp.root).get<world::Name>().value == "Lamp");
  CHECK(name(a) == "Desk lamp");
  CHECK(name(b) == "Lamp");
  REQUIRE(editor.commands().redo(world));
  CHECK(entryOf(file, lamp.root)["name"] == "Desk lamp");
  CHECK(name(b) == "Lamp");

  // Apply all on C writes both of its names in one step: the root's and the bulb's.
  editor.requestApplyAllOverrides(c);
  REQUIRE(editor.applyAllDialogOpen());
  fixture.frame(editor); // the dialog draws, with the warning about script paths
  editor.confirmApply();
  CHECK(entryOf(file, lamp.root)["name"] == "Floor lamp");
  CHECK(entryOf(file, lamp.bulb)["name"] == "Globe");
  CHECK(name(a) == "Desk lamp");
  CHECK(name(b) == "Lamp");
  CHECK(name(bulbB) == "Globe");
  REQUIRE(editor.commands().undo(world));
  CHECK(entryOf(file, lamp.root)["name"] == "Desk lamp");
  CHECK(entryOf(file, lamp.bulb)["name"] == "Bulb");
  CHECK(name(bulbC) == "Globe");
  CHECK(name(bulbB) == "Bulb");

  // Not offered while playing.
  editor.play();
  fixture.frame(editor);
  CHECK(!editor.applyTargetName(nameRequest(c)));
  CHECK(!editor.applyToPrefab(nameRequest(c)).has_value());
  CHECK(editor.applyAllPlan(c).files.empty());
  editor.stop();
}

TEST_CASE("applying a rename inside a prefab of prefabs offers the files the name reaches the instance from",
          "[editor][gpu][prefab][apply][name][nested]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-rename-nested");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  // The crate and pallet of the nested cases: the pallet holds Crate B and leaves its lid's name alone.
  const core::Uuid crate = core::Uuid::generate();
  const core::Uuid lid = core::Uuid::generate();
  const core::Uuid pallet = core::Uuid::generate();
  const core::Uuid crateB = core::Uuid::derive(pallet, "b");
  const core::Uuid palletLid = world::World::instancedIdentity(crateB, lid);
  const std::filesystem::path crateFile = directory / "prefabs" / "crate.prefab.json";
  const std::filesystem::path palletFile = directory / "prefabs" / "pallet.prefab.json";
  writeDocument(crateFile, json::array({{{"uuid", crate.toString()}, {"name", "Crate"}, {"components", json::object()}},
                                        {{"uuid", lid.toString()},
                                         {"name", "Lid"},
                                         {"parent", crate.toString()},
                                         {"components", {{"MeshRenderer", {{"visible", true}}}}}}}));
  writeDocument(palletFile,
                json::array({{{"uuid", pallet.toString()}, {"name", "Pallet"}, {"components", json::object()}},
                             {{"uuid", crateB.toString()},
                              {"name", "Crate B"},
                              {"parent", pallet.toString()},
                              {"prefab", crate.toString()},
                              {"components", json::object()}}}));
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid placed = core::Uuid::generate();
  const core::Uuid other = core::Uuid::generate();
  const core::Uuid loose = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, placed), world);
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, other), world);
  editor.commands().push(editor::instantiatePrefabCommand(crate, "Crate", {}, loose), world);
  const auto sceneLid = [&](const core::Uuid &instance) {
    return world::World::instancedIdentity(instance, palletLid);
  };
  const core::Uuid looseLid = world::World::instancedIdentity(loose, lid);
  const auto name = [&](const core::Uuid &uuid) { return world.find(uuid).get<world::Name>().value; };
  REQUIRE(world.find(sceneLid(placed)));
  editor.commands().push(editor::renameCommand(sceneLid(placed), "Lid", "Cover"), world);
  fixture.frame(editor);
  const std::string crateText = text(crateFile);
  const std::string palletText = text(palletFile);

  // The pallet leaves the name to the crate, so both can take it, the crate first.
  const editor::ApplyRequest request = nameRequest(sceneLid(placed));
  const std::vector<editor::ApplyChoice> choices = editor.applyChoices(request);
  REQUIRE(choices.size() == 2);
  CHECK(choices[0].prefab == crate);
  CHECK(choices[1].prefab == pallet);

  // The default goes to the crate: every crate's lid follows, the loose one included.
  REQUIRE(editor.applyToPrefab(request).has_value());
  CHECK(entryOf(crateFile, lid)["name"] == "Cover");
  CHECK(text(palletFile) == palletText);
  CHECK(name(sceneLid(other)) == "Cover");
  CHECK(name(looseLid) == "Cover");
  REQUIRE(editor.commands().undo(world));
  CHECK(text(crateFile) == crateText);
  CHECK(name(sceneLid(other)) == "Lid");
  CHECK(name(sceneLid(placed)) == "Cover");

  // The pallet instead: only the pallets' lids follow, and the pallet stores the rename.
  editor::ApplyRequest outer = request;
  outer.prefab = pallet;
  editor.requestApplyToPrefab(outer);
  REQUIRE(editor.applyDialogOpen());
  CHECK(editor.applyUsers().tabs.size() == 1);
  fixture.frame(editor);
  editor.confirmApply();
  CHECK(text(crateFile) == crateText);
  CHECK(entryOf(palletFile, palletLid)["name"] == "Cover");
  CHECK(name(sceneLid(other)) == "Cover");
  CHECK(name(looseLid) == "Lid");
  CHECK(!storesName(world, sceneLid(placed)));

  // Now the pallet names the lid, so a further rename stops there: the crate's name no longer reaches it.
  editor.commands().push(editor::renameCommand(sceneLid(placed), "Cover", "Top"), world);
  REQUIRE(editor.applyChoices(request).size() == 1);
  CHECK(editor.applyChoices(request)[0].prefab == pallet);
  // Crate B is an instance root inside the pallet and stores its own name: only the pallet takes it.
  const core::Uuid sceneCrate = world::World::instancedIdentity(placed, crateB);
  editor.commands().push(editor::renameCommand(sceneCrate, "Crate B", "Crate X"), world);
  REQUIRE(editor.applyChoices(nameRequest(sceneCrate)).size() == 1);
  CHECK(editor.applyChoices(nameRequest(sceneCrate))[0].prefab == pallet);
  // Apply all uses the default target for each: both renames go to the pallet's file.
  const editor::ApplyAllPlan plan = editor.applyAllPlan(placed);
  REQUIRE(plan.files.size() == 1);
  CHECK(changesTo(plan, palletFile) == 2);
  REQUIRE(editor.applyAllOverrides(placed).has_value());
  CHECK(entryOf(palletFile, palletLid)["name"] == "Top");
  CHECK(entryOf(palletFile, crateB)["name"] == "Crate X");
  CHECK(name(sceneLid(other)) == "Top");
  CHECK(name(world::World::instancedIdentity(other, crateB)) == "Crate X");
  CHECK(text(crateFile) == crateText);
}

namespace {

// Crate: a root with the children A, B and C.
struct Crate {
  core::Uuid root = core::Uuid::generate();

  [[nodiscard]] core::Uuid child(const char *name) const {
    return core::Uuid::derive(root, name);
  }
  [[nodiscard]] json entities() const {
    json out = json::array({{{"uuid", root.toString()}, {"name", "Crate"}, {"components", json::object()}}});
    for (const char *name : {"A", "B", "C"}) {
      out.push_back({{"uuid", child(name).toString()},
                     {"name", name},
                     {"parent", root.toString()},
                     {"components", json::object()}});
    }
    return out;
  }
};

// The names of an entity's children in sibling order.
std::vector<std::string> childNames(const world::World &world, const core::Uuid &parent) {
  std::vector<std::string> names;
  for (const flecs::entity child : world.children(world.find(parent))) {
    names.push_back(child.get<world::Name>().value);
  }
  return names;
}

// The identities a document lists, in file order, of the entities whose parent is `parent`.
std::vector<std::string> filedChildren(const std::filesystem::path &file, const core::Uuid &parent) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  const json document = assets::parseJson(*bytes);
  std::vector<std::string> out;
  for (const json &entry : document["entities"]) {
    if (entry.contains("parent") && entry["parent"] == parent.toString()) {
      out.push_back(entry["uuid"].get<std::string>());
    }
  }
  return out;
}

// Whether the scene stores an order for the entity.
bool storesOrder(const world::World &world, const core::Uuid &uuid) {
  const json entry = entryOf(world::saveScene(world), uuid);
  return entry.is_object() && entry.contains("order");
}

editor::ApplyRequest orderRequest(const core::Uuid &entity) {
  return {.entity = entity, .component = {}, .kind = editor::ApplyKind::ChildOrder};
}

using Names = std::vector<std::string>;

} // namespace

TEST_CASE("applying an instance's child order reorders the prefab and the instances that follow it, undoably",
          "[editor][gpu][prefab][apply][order]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-order");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const Crate crate;
  const std::filesystem::path file = directory / "prefabs" / "crate.prefab.json";
  writeDocument(file, crate.entities());
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  // X reorders its children C, A, B; Y keeps the prefab's order; Z has an order of its own, B, C, A;
  // W adds a child and reorders.
  const core::Uuid x = core::Uuid::generate();
  const core::Uuid y = core::Uuid::generate();
  const core::Uuid z = core::Uuid::generate();
  const core::Uuid w = core::Uuid::generate();
  for (const core::Uuid &uuid : {x, y, z, w}) {
    editor.commands().push(editor::instantiatePrefabCommand(crate.root, "Crate", {}, uuid), world);
  }
  const auto part = [&](const core::Uuid &instance, const char *name) {
    return world::World::instancedIdentity(instance, crate.child(name));
  };
  const auto move = [&](const core::Uuid &instance, const char *name, std::size_t index) {
    editor.commands().push(editor::reparentCommand(part(instance, name), instance, index), world);
  };
  move(x, "C", 0);
  move(z, "B", 0);
  move(z, "A", 2);
  editor.commands().push(editor::createEntityCommand("Extra", w), world);
  move(w, "C", 0);
  REQUIRE(childNames(world, x) == Names{"C", "A", "B"});
  REQUIRE(childNames(world, z) == Names{"B", "C", "A"});
  REQUIRE(editor.saveSceneAs(directory / "scenes" / "crates.scene.json").has_value());
  fixture.frame(editor);
  const std::string original = text(file);
  editor.selection().select(part(x, "C"));

  const editor::ApplyRequest request = orderRequest(x);
  REQUIRE(editor.applyTargetName(request) == "Crate");
  REQUIRE(editor.applyChoices(request).size() == 1);
  // Y is in the prefab's order: nothing to apply. A part without children has none either.
  CHECK(!editor.applyTargetName(orderRequest(y)));
  CHECK(!editor.applyTargetName(orderRequest(part(x, "A"))));
  // W's added child has no counterpart in the prefab: refused, not filtered out, and Apply all
  // keeps the order on the instance with the reason.
  CHECK(!editor.applyTargetName(orderRequest(w)));
  CHECK(editor.applyChoices(orderRequest(w)).empty());
  const editor::ApplyAllPlan kept = editor.applyAllPlan(w);
  CHECK(kept.files.empty());
  CHECK(lists(kept, "the order of its children (it has a child the prefab lacks)"));
  CHECK(lists(kept, "the added child Extra"));

  // The confirmation names the scene and the open tab; cancelling writes nothing.
  editor.requestApplyToPrefab(request);
  REQUIRE(editor.applyDialogOpen());
  REQUIRE(editor.applyUsers().scenes.size() == 1);
  CHECK(editor.applyUsers().scenes[0].filename() == "crates.scene.json");
  CHECK(editor.applyUsers().tabs.size() == 1);
  fixture.frame(editor); // the dialog draws, with the warning about same-name siblings
  editor.cancelApply();
  CHECK(text(file) == original);
  CHECK(childNames(world, y) == Names{"A", "B", "C"});

  const std::size_t undoBefore = editor.commands().size();
  editor.requestApplyToPrefab(request);
  REQUIRE(editor.applyDialogOpen());
  editor.confirmApply();
  CHECK(editor.commands().size() == undoBefore + 1);
  // The prefab's children are reordered in place, with their identities and parent.
  CHECK(filedChildren(file, crate.root) == std::vector<std::string>{crate.child("C").toString(),
                                                                    crate.child("A").toString(),
                                                                    crate.child("B").toString()});
  // Y follows; X follows the prefab now and stores no order; Z keeps its own; the identities stay.
  CHECK(childNames(world, x) == Names{"C", "A", "B"});
  CHECK(childNames(world, y) == Names{"C", "A", "B"});
  CHECK(childNames(world, z) == Names{"B", "C", "A"});
  CHECK(!storesOrder(world, x));
  CHECK(!storesOrder(world, y));
  CHECK(storesOrder(world, z));
  CHECK(world.find(part(y, "C")).parent() == world.find(y));
  CHECK(editor.selection().contains(part(x, "C")));
  CHECK(!editor.applyTargetName(request));
  CHECK(editor.isDirty());

  // Undo puts the file and every instance back; redo applies again.
  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(childNames(world, x) == Names{"C", "A", "B"});
  CHECK(storesOrder(world, x));
  CHECK(childNames(world, y) == Names{"A", "B", "C"});
  CHECK(childNames(world, z) == Names{"B", "C", "A"});
  CHECK(editor.selection().contains(part(x, "C")));
  CHECK(!editor.isDirty());
  REQUIRE(editor.commands().redo(world));
  CHECK(childNames(world, y) == Names{"C", "A", "B"});
  CHECK(!storesOrder(world, x));

  // Saved and opened again, the instances read the reordered prefab.
  REQUIRE(editor.saveScene().has_value());
  REQUIRE(editor.openProject(directory).has_value());
  REQUIRE(editor.openScene(directory / "scenes" / "crates.scene.json").has_value());
  CHECK(childNames(editor.world(), y) == Names{"C", "A", "B"});
  CHECK(childNames(editor.world(), z) == Names{"B", "C", "A"});
  CHECK(childNames(editor.world(), w) == Names{"C", "A", "B", "Extra"});
  CHECK(!storesOrder(editor.world(), x));

  // Not offered while playing.
  move(x, "B", 0);
  REQUIRE(editor.applyTargetName(request));
  editor.play();
  fixture.frame(editor);
  CHECK(!editor.applyTargetName(request));
  CHECK(!editor.applyToPrefab(request).has_value());
  editor.stop();
}

TEST_CASE("applying a descendant's child order inside a prefab of prefabs offers the files the order reaches it from",
          "[editor][gpu][prefab][apply][order][nested]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-order-nested");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  // The pallet holds Crate B, a crate whose children it leaves in the crate's order, and a Board.
  const Crate crate;
  const core::Uuid pallet = core::Uuid::generate();
  const core::Uuid crateB = core::Uuid::derive(pallet, "b");
  const core::Uuid board = core::Uuid::derive(pallet, "board");
  const std::filesystem::path crateFile = directory / "prefabs" / "crate.prefab.json";
  const std::filesystem::path palletFile = directory / "prefabs" / "pallet.prefab.json";
  writeDocument(crateFile, crate.entities());
  writeDocument(palletFile,
                json::array({{{"uuid", pallet.toString()}, {"name", "Pallet"}, {"components", json::object()}},
                             {{"uuid", crateB.toString()},
                              {"name", "Crate B"},
                              {"parent", pallet.toString()},
                              {"prefab", crate.root.toString()},
                              {"components", json::object()}},
                             {{"uuid", board.toString()},
                              {"name", "Board"},
                              {"parent", pallet.toString()},
                              {"components", json::object()}}}));
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid placed = core::Uuid::generate();
  const core::Uuid other = core::Uuid::generate();
  const core::Uuid loose = core::Uuid::generate();
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, placed), world);
  editor.commands().push(editor::instantiatePrefabCommand(pallet, "Pallet", {}, other), world);
  editor.commands().push(editor::instantiatePrefabCommand(crate.root, "Crate", {}, loose), world);
  const auto sceneCrate = [&](const core::Uuid &instance) { return world::World::instancedIdentity(instance, crateB); };
  const auto palletPart = [&](const char *name) { return world::World::instancedIdentity(crateB, crate.child(name)); };
  const auto move = [&](const core::Uuid &parent, const core::Uuid &child, std::size_t index) {
    editor.commands().push(editor::reparentCommand(child, parent, index), world);
  };
  const core::Uuid parent = sceneCrate(placed);
  REQUIRE(world.find(parent));
  move(parent, world::World::instancedIdentity(placed, palletPart("B")), 2);
  REQUIRE(childNames(world, parent) == Names{"A", "C", "B"});
  fixture.frame(editor);
  const std::string crateText = text(crateFile);
  const std::string palletText = text(palletFile);

  // The pallet leaves the order to the crate, so both can take it, the crate first.
  const editor::ApplyRequest request = orderRequest(parent);
  const std::vector<editor::ApplyChoice> choices = editor.applyChoices(request);
  REQUIRE(choices.size() == 2);
  CHECK(choices[0].prefab == crate.root);
  CHECK(choices[1].prefab == pallet);

  // The default goes to the crate: every crate follows, the loose one included.
  REQUIRE(editor.applyToPrefab(request).has_value());
  CHECK(filedChildren(crateFile, crate.root) == std::vector<std::string>{crate.child("A").toString(),
                                                                         crate.child("C").toString(),
                                                                         crate.child("B").toString()});
  CHECK(text(palletFile) == palletText);
  CHECK(childNames(world, sceneCrate(other)) == Names{"A", "C", "B"});
  CHECK(childNames(world, loose) == Names{"A", "C", "B"});
  REQUIRE(editor.commands().undo(world));
  CHECK(text(crateFile) == crateText);
  CHECK(childNames(world, sceneCrate(other)) == Names{"A", "B", "C"});
  CHECK(childNames(world, parent) == Names{"A", "C", "B"});

  // The pallet instead: it stores the order, translated to its own identities for Crate B's
  // children, and only the pallets follow.
  editor::ApplyRequest outer = request;
  outer.prefab = pallet;
  editor.requestApplyToPrefab(outer);
  REQUIRE(editor.applyDialogOpen());
  fixture.frame(editor);
  editor.confirmApply();
  CHECK(text(crateFile) == crateText);
  CHECK(entryOf(palletFile, crateB)["order"] ==
        json::array({palletPart("A").toString(), palletPart("C").toString(), palletPart("B").toString()}));
  CHECK(childNames(world, sceneCrate(other)) == Names{"A", "C", "B"});
  CHECK(childNames(world, loose) == Names{"A", "B", "C"});
  CHECK(!storesOrder(world, parent));

  // Now the pallet orders the crate's children itself, so a further reorder stops there.
  move(parent, world::World::instancedIdentity(placed, palletPart("A")), 2);
  REQUIRE(childNames(world, parent) == Names{"C", "B", "A"});
  REQUIRE(editor.applyChoices(request).size() == 1);
  CHECK(editor.applyChoices(request)[0].prefab == pallet);
  // The pallet's root orders its own children: only the pallet's file takes that.
  move(placed, world::World::instancedIdentity(placed, board), 0);
  REQUIRE(editor.applyChoices(orderRequest(placed)).size() == 1);
  CHECK(editor.applyChoices(orderRequest(placed))[0].prefab == pallet);
  // Apply all writes both orders to the pallet, the root's and Crate B's, in one undo step.
  const editor::ApplyAllPlan plan = editor.applyAllPlan(placed);
  REQUIRE(plan.files.size() == 1);
  CHECK(changesTo(plan, palletFile) == 2);
  editor.requestApplyAllOverrides(placed);
  REQUIRE(editor.applyAllDialogOpen());
  fixture.frame(editor); // the dialog draws, with the warning about same-name siblings
  editor.confirmApply();
  CHECK(filedChildren(palletFile, pallet) == std::vector<std::string>{board.toString(), crateB.toString()});
  CHECK(entryOf(palletFile, crateB)["order"] ==
        json::array({palletPart("C").toString(), palletPart("B").toString(), palletPart("A").toString()}));
  CHECK(childNames(world, other) == Names{"Board", "Crate B"});
  CHECK(childNames(world, sceneCrate(other)) == Names{"C", "B", "A"});
  CHECK(text(crateFile) == crateText);
  CHECK(!storesOrder(world, placed));
  CHECK(!storesOrder(world, parent));
  REQUIRE(editor.commands().undo(world));
  CHECK(childNames(world, other) == Names{"Crate B", "Board"});
  CHECK(childNames(world, sceneCrate(other)) == Names{"A", "C", "B"});
  CHECK(childNames(world, placed) == Names{"Board", "Crate B"});
  CHECK(childNames(world, parent) == Names{"C", "B", "A"});
}

TEST_CASE("applying one material slot writes that slot and leaves the others on the instance, undoably",
          "[editor][gpu][prefab][apply][slots]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("apply-slot");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  const core::Uuid root = core::Uuid::generate();
  const core::Uuid red = core::Uuid::generate();
  const core::Uuid green = core::Uuid::generate();
  const core::Uuid blue = core::Uuid::generate();
  const core::Uuid grey = core::Uuid::generate();
  const std::filesystem::path file = directory / "prefabs" / "figure.prefab.json";
  writeDocument(file, json::array({{{"uuid", root.toString()},
                                    {"name", "Figure"},
                                    {"components",
                                     {{"MeshRenderer",
                                       {{"slotMaterials",
                                         json::array({red.toString(), green.toString(), blue.toString()})}}}}}}}));
  REQUIRE(editor.openProject(directory).has_value());
  world::World &world = editor.world();
  const core::Uuid a = core::Uuid::generate();
  const core::Uuid b = core::Uuid::generate();
  for (const core::Uuid &uuid : {a, b}) {
    editor.commands().push(editor::instantiatePrefabCommand(root, "Figure", {}, uuid), world);
  }
  const auto slots = [&](const core::Uuid &instance) {
    return world.find(instance).get<world::MeshRenderer>().slotMaterials;
  };
  const world::ComponentInfo &info = *world.findComponent("MeshRenderer");
  const auto overrides = [&](const core::Uuid &instance) {
    return world::componentPatch(info, world.componentToJson(world.find(instance), info.id),
                                 world.componentToJson(world.find(root), info.id));
  };
  // A gives slots 0 and 2 a material of its own and keeps the prefab's slot 1.
  const json before = world.componentToJson(world.find(a), info.id);
  json after = before;
  after["slotMaterials"] = json::array({grey.toString(), green.toString(), grey.toString()});
  editor.commands().push(
      editor::componentCommand(a, "MeshRenderer", std::optional{before}, std::optional{after}, "slots"), world);
  CHECK(overrides(a) == json{{"slotMaterials", {{"0", grey.toString()}, {"2", grey.toString()}}}});
  fixture.frame(editor);
  const std::string original = text(file);

  const editor::ApplyRequest request{
      .entity = a, .component = "MeshRenderer", .member = json::json_pointer{"/slotMaterials/0"}};
  REQUIRE(editor.applyTargetName(request) == "Figure");
  REQUIRE(editor.applyToPrefab(request).has_value());
  // The file holds slot 0 only, B follows it, and A still marks slot 2.
  CHECK(stored(file, root)["MeshRenderer"]["slotMaterials"] ==
        json::array({grey.toString(), green.toString(), blue.toString()}));
  CHECK(slots(b) == std::vector<core::Uuid>{grey, green, blue});
  CHECK(overrides(a) == json{{"slotMaterials", {{"2", grey.toString()}}}});

  REQUIRE(editor.commands().undo(world));
  CHECK(text(file) == original);
  CHECK(slots(b) == std::vector<core::Uuid>{red, green, blue});
  CHECK(overrides(a) == json{{"slotMaterials", {{"0", grey.toString()}, {"2", grey.toString()}}}});
  REQUIRE(editor.commands().redo(world));
  CHECK(slots(b) == std::vector<core::Uuid>{grey, green, blue});

  // Apply all offers the one slot left and writes it.
  const editor::ApplyAllPlan plan = editor.applyAllPlan(a);
  CHECK(changesTo(plan, file) == 1);
  REQUIRE(editor.applyAllOverrides(a).has_value());
  CHECK(stored(file, root)["MeshRenderer"]["slotMaterials"] ==
        json::array({grey.toString(), green.toString(), grey.toString()}));
  CHECK(slots(b) == std::vector<core::Uuid>{grey, green, grey});
  CHECK(editor.applyAllPlan(a).files.empty());
}
