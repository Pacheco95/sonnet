#include <sonnet/core/JobSystem.h>
#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/AssetCommands.h>
#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sonnet;
using Catch::Approx;

TEST_CASE("the selection keeps order, a primary and prunes dead entities", "[editor][selection]") {
  world::World world;
  const core::Uuid a = world.uuidOf(world.createEntity("a"));
  const core::Uuid b = world.uuidOf(world.createEntity("b"));
  editor::Selection selection;
  REQUIRE(selection.empty());
  REQUIRE(selection.primary().isNil());
  selection.select(a);
  selection.select(b, editor::Selection::Mode::Add);
  REQUIRE(selection.items().size() == 2);
  REQUIRE(selection.primary() == b);
  selection.select(a, editor::Selection::Mode::Add); // moves to the end
  REQUIRE(selection.primary() == a);
  selection.select(a, editor::Selection::Mode::Toggle);
  REQUIRE(!selection.contains(a));
  REQUIRE(selection.primary() == b);
  selection.select(a);
  REQUIRE(selection.items().size() == 1);
  world.destroyEntity(world.find(a));
  selection.prune(world);
  REQUIRE(selection.empty());
  selection.select({});
  REQUIRE(selection.empty());
}

TEST_CASE("inspecting an asset keeps the selection until it is touched again", "[editor][selection][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const core::Uuid entity = world.uuidOf(world.createEntity("a"));
  editor::Selection selection;
  editor::AssetBrowserPanel browser{assets, selection};
  const core::Uuid asset = core::Uuid::generate();

  selection.select(entity);
  browser.inspect(asset);
  REQUIRE(browser.inspected() == asset);
  REQUIRE(selection.primary() == entity); // the click did not deselect the object

  selection.select(entity); // selecting again, even the same entity, hands the inspector back
  REQUIRE(browser.inspected().isNil());
  REQUIRE(selection.primary() == entity);

  browser.inspect(asset);
  selection.clear();
  REQUIRE(browser.inspected().isNil());
}

TEST_CASE("the history is saved exactly where the last save was", "[editor][commands]") {
  world::World world;
  editor::CommandStack commands;
  const core::Uuid uuid = world.uuidOf(world.createEntity("a"));
  const auto rename = [&](const char *before, const char *after) {
    commands.push(editor::renameCommand(uuid, before, after), world);
  };
  REQUIRE(commands.isSaved()); // a new history is clean

  rename("a", "b");
  REQUIRE(!commands.isSaved());
  REQUIRE(commands.undo(world)); // back to the saved point
  REQUIRE(commands.isSaved());
  REQUIRE(commands.redo(world));
  REQUIRE(!commands.isSaved());

  commands.markSaved();
  REQUIRE(commands.isSaved());
  rename("b", "c");
  REQUIRE(!commands.isSaved());
  REQUIRE(commands.undo(world));
  REQUIRE(commands.isSaved());
  REQUIRE(commands.undo(world)); // past the save
  REQUIRE(!commands.isSaved());
  REQUIRE(commands.redo(world));
  REQUIRE(commands.isSaved());

  // A push that drops the redo list the save sat in leaves it unreachable until the next save.
  commands.markSaved();
  REQUIRE(commands.undo(world));
  rename("a", "d"); // same size as at the save, different history
  REQUIRE(!commands.isSaved());
  REQUIRE(commands.undo(world));
  REQUIRE(!commands.isSaved());
  REQUIRE(commands.redo(world));
  REQUIRE(!commands.isSaved());
  commands.markSaved();
  REQUIRE(commands.isSaved());

  // Clearing forgets the save; the caller decides what the remaining state is.
  commands.clear();
  REQUIRE(!commands.isSaved());
  commands.markSaved();
  REQUIRE(commands.isSaved());
}

TEST_CASE("the history limit moves the saved position or loses it", "[editor][commands]") {
  world::World world;
  const core::Uuid uuid = world.uuidOf(world.createEntity("a"));
  editor::CommandStack commands;
  const auto rename = [&] { commands.push(editor::renameCommand(uuid, "a", "a"), world); };

  // Saved with room to spare: the position slides down as the oldest commands drop off.
  rename();
  commands.markSaved();
  for (std::size_t i = 0; i < editor::CommandStack::Limit - 1; ++i) {
    rename();
  }
  REQUIRE(commands.size() == editor::CommandStack::Limit);
  REQUIRE(!commands.isSaved());
  for (std::size_t i = 0; i < editor::CommandStack::Limit - 1; ++i) {
    REQUIRE(commands.undo(world));
  }
  REQUIRE(commands.isSaved());

  // One more push drops the oldest command; the state the save followed is now the bottom.
  while (commands.redo(world)) {
  }
  rename();
  REQUIRE(commands.size() == editor::CommandStack::Limit);
  while (commands.undo(world)) {
  }
  REQUIRE(commands.isSaved());

  // Saved before the first command, which then drops off: that state is gone.
  editor::CommandStack fresh;
  for (std::size_t i = 0; i <= editor::CommandStack::Limit; ++i) {
    fresh.push(editor::renameCommand(uuid, "a", "a"), world);
  }
  while (fresh.undo(world)) {
  }
  REQUIRE(!fresh.isSaved());
}

TEST_CASE("create, delete and duplicate commands undo and redo with stable identities", "[editor][commands]") {
  world::World world;
  editor::CommandStack commands;
  const core::Uuid uuid = core::Uuid::generate();
  commands.push(editor::createEntityCommand("Box", {},
                                            {{"MeshRenderer", {{"mesh", assets::builtin::sphere().toString()}}}}, uuid),
                world);
  REQUIRE(commands.canUndo());
  REQUIRE(commands.undoDescription() == "create Box");
  flecs::entity box = world.find(uuid);
  REQUIRE(box.is_valid());
  REQUIRE(box.get<world::MeshRenderer>().mesh == assets::builtin::sphere());
  const core::Uuid child = core::Uuid::generate();
  commands.push(editor::createEntityCommand("Child", uuid, {}, child), world);
  REQUIRE(world.parentOf(world.find(child)) == box);

  REQUIRE(commands.undo(world));
  REQUIRE(!world.find(child).is_valid());
  REQUIRE(commands.redo(world));
  REQUIRE(world.find(child).is_valid());
  REQUIRE(world.parentOf(world.find(child)) == world.find(uuid));

  commands.push(editor::deleteEntityCommand(uuid), world);
  REQUIRE(!world.find(uuid).is_valid());
  REQUIRE(!world.find(child).is_valid());
  REQUIRE(commands.undoDescription() == "delete Box");
  REQUIRE(commands.undo(world));
  box = world.find(uuid);
  REQUIRE(box.is_valid());
  REQUIRE(box.get<world::MeshRenderer>().mesh == assets::builtin::sphere());
  REQUIRE(world.parentOf(world.find(child)) == box);

  const core::Uuid copy = core::Uuid::generate();
  commands.push(editor::duplicateEntityCommand(uuid, copy), world);
  const flecs::entity duplicate = world.find(copy);
  REQUIRE(duplicate.is_valid());
  REQUIRE(duplicate.get<world::Name>().value == "Box copy");
  REQUIRE(world.children(duplicate).size() == 1);
  REQUIRE(world.uuidOf(world.children(duplicate)[0]) != child);
  REQUIRE(world.roots().size() == 2);
  REQUIRE(commands.undo(world));
  REQUIRE(!world.find(copy).is_valid());
  REQUIRE(world.roots().size() == 1);
  REQUIRE(commands.redo(world));
  REQUIRE(world.find(copy).get<world::Name>().value == "Box copy");

  // A push after undo drops the redo list.
  REQUIRE(commands.undo(world));
  REQUIRE(commands.canRedo());
  commands.push(editor::renameCommand(uuid, "Box", "Crate"), world);
  REQUIRE(!commands.canRedo());
  REQUIRE(world.find(uuid).get<world::Name>().value == "Crate");
  commands.clear();
  REQUIRE(!commands.canUndo());
}

TEST_CASE("reparent and component commands restore exactly what they changed", "[editor][commands]") {
  world::World world;
  editor::CommandStack commands;
  const flecs::entity parent = world.createEntity("Parent");
  parent.set<world::Transform>({.position = {10.0f, 0.0f, 0.0f}});
  const flecs::entity entity = world.createEntity("Entity");
  entity.set<world::Transform>({.position = {12.0f, 0.0f, 0.0f}});
  const core::Uuid parentUuid = world.uuidOf(parent);
  const core::Uuid uuid = world.uuidOf(entity);

  commands.push(editor::reparentCommand(uuid, parentUuid), world);
  REQUIRE(world.parentOf(entity) == parent);
  REQUIRE(entity.get<world::Transform>().position.x == Approx(2.0f)); // world position kept
  commands.undo(world);
  REQUIRE(!world.parentOf(entity).is_valid());
  REQUIRE(entity.get<world::Transform>().position.x == Approx(12.0f));
  commands.redo(world);
  REQUIRE(world.parentOf(entity) == parent);

  const world::ComponentInfo *spin = world.findComponent("Spin");
  commands.push(editor::componentCommand(uuid, "Spin", std::nullopt, std::make_optional(nlohmann::json{}), "add Spin"),
                world);
  REQUIRE(entity.has<world::Spin>());
  REQUIRE(entity.get<world::Spin>().speed == Approx(1.0f)); // the default value
  const nlohmann::json before = world.componentToJson(entity, spin->id);
  nlohmann::json after = before;
  after["speed"] = 3.0f;
  commands.push(
      editor::componentCommand(uuid, "Spin", std::make_optional(before), std::make_optional(after), "edit Spin"),
      world);
  REQUIRE(entity.get<world::Spin>().speed == Approx(3.0f));
  commands.undo(world);
  REQUIRE(entity.get<world::Spin>().speed == Approx(1.0f));
  commands.undo(world);
  REQUIRE(!entity.has<world::Spin>());
  commands.redo(world);
  commands.redo(world);
  REQUIRE(entity.get<world::Spin>().speed == Approx(3.0f));

  commands.push(
      editor::componentCommand(uuid, "Static", std::nullopt, std::make_optional(nlohmann::json{}), "add Static"),
      world);
  REQUIRE(entity.has<world::Static>());
  commands.push(
      editor::componentCommand(uuid, "Static", std::make_optional(nlohmann::json{}), std::nullopt, "remove Static"),
      world);
  REQUIRE(!entity.has<world::Static>());
  commands.undo(world);
  REQUIRE(entity.has<world::Static>());

  // Instantiation is a command too.
  const flecs::entity prefab = world.createEntity("Crate");
  prefab.add(flecs::Prefab);
  prefab.set<world::MeshRenderer>({});
  const core::Uuid instance = core::Uuid::generate();
  std::vector<std::unique_ptr<editor::ICommand>> batch;
  batch.push_back(editor::instantiatePrefabCommand(world.uuidOf(prefab), "Crate 1", {}, instance));
  batch.push_back(editor::deleteEntityCommand(uuid));
  commands.push(editor::compositeCommand("batch", std::move(batch)), world);
  REQUIRE(world.isInstance(world.find(instance)));
  REQUIRE(!world.find(uuid).is_valid());
  commands.undo(world);
  REQUIRE(!world.find(instance).is_valid());
  REQUIRE(world.find(uuid).is_valid());
}

TEST_CASE("material and texture settings commands act on the database and undo", "[editor][commands][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_asset_commands";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  world::World world;
  editor::CommandStack commands;

  assets::MaterialSource before;
  before.roughness = 0.2f;
  const auto material = assets.createMaterial(root / "assets" / "steel.material.json", before);
  REQUIRE(material.has_value());
  assets::MaterialSource after = before;
  after.roughness = 0.9f;
  after.alphaMode = renderer::AlphaMode::Mask;
  commands.push(editor::materialEditCommand(assets, *material, before, after), world);
  REQUIRE(commands.undoDescription() == "edit material steel");
  REQUIRE(assets.materialSource(*material)->roughness == Catch::Approx(0.9f));
  REQUIRE(renderer.material(assets.material(*material)).alphaMode == renderer::AlphaMode::Mask);
  REQUIRE(commands.undo(world));
  REQUIRE(assets.materialSource(*material)->roughness == Catch::Approx(0.2f));
  REQUIRE(commands.redo(world));
  REQUIRE(assets.materialSource(*material)->roughness == Catch::Approx(0.9f));

  // A texture's settings: the command rewrites the sidecar through the database. Without a
  // real image the re-import fails and is logged, but the settings still round-trip.
  REQUIRE(core::writeFile(root / "assets" / "noise.png", std::string_view{"not a png"}).has_value());
  assets.open(root, roots);
  const assets::AssetInfo *noise = assets.findByPath(root / "assets" / "noise.png");
  REQUIRE(noise != nullptr);
  const assets::TextureSettings defaults = assets.textureSettings(noise->uuid);
  const assets::TextureSettings linear{.srgb = false, .mipmaps = false, .compress = false};
  commands.push(editor::textureSettingsCommand(assets, noise->uuid, defaults, linear), world);
  REQUIRE(assets.textureSettings(noise->uuid) == linear);
  REQUIRE(commands.undo(world));
  REQUIRE(assets.textureSettings(noise->uuid) == defaults);
  std::filesystem::remove_all(root);
}
