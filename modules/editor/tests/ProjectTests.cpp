#include <sonnet/editor/Preferences.h>
#include <sonnet/editor/Project.h>

#include <sonnet/world/DrawList.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace sonnet;

namespace {

std::filesystem::path scratch(const char *name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path.parent_path());
  return path;
}

} // namespace

TEST_CASE("a project is created with a start scene and reopened from its folder", "[editor][project]") {
  const std::filesystem::path directory = scratch("basic");
  const auto created = editor::Project::create(directory, "Basic");
  REQUIRE(created.has_value());
  REQUIRE(created->name == "Basic");
  REQUIRE(std::filesystem::exists(created->file()));
  REQUIRE(std::filesystem::exists(created->resolve(created->startScene)));
  REQUIRE(!editor::Project::create(directory, "Again").has_value()); // never overwrites

  const auto opened = editor::Project::open(directory);
  REQUIRE(opened.has_value());
  REQUIRE(opened->name == "Basic");
  REQUIRE(opened->startScene == "scenes/main.scene.json");
  REQUIRE(opened->assetRoots.size() == 3);
  REQUIRE(!opened->engineVersion.empty());
  REQUIRE(opened->relative(opened->resolve("scenes/main.scene.json")) == "scenes/main.scene.json");
  REQUIRE(opened->files(".scene.json").size() == 1);
  REQUIRE(opened->files(".prefab.json").empty());

  world::World world;
  const auto loaded = world::loadSceneFile(world, opened->resolve(opened->startScene));
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->size() == 4); // ground, box, sun, camera
  world.progress(0.016f);
  REQUIRE(world::sceneCamera(world).has_value());
  REQUIRE(world::sceneLight(world).has_value());

  REQUIRE(!editor::Project::open(scratch("missing")).has_value());
  std::filesystem::remove_all(directory);
}

TEST_CASE("preferences round-trip and format the external editor command", "[editor][project]") {
  const std::filesystem::path file = scratch("preferences.json");
  editor::Preferences preferences;
  preferences.addRecentProject("/projects/a");
  preferences.addRecentProject("/projects/b");
  preferences.addRecentProject("/projects/a"); // moves to the front without a duplicate
  REQUIRE(preferences.recentProjects.size() == 2);
  REQUIRE(preferences.recentProjects[0] == "/projects/a");
  preferences.sourceRoot = "/src/sonnet";
  REQUIRE(preferences.editorCommand("modules/rhi/src/VulkanDevice.cpp", 42) ==
          "code --goto /src/sonnet/modules/rhi/src/VulkanDevice.cpp:42");
  REQUIRE(preferences.save(file).has_value());

  const editor::Preferences loaded = editor::Preferences::load(file);
  REQUIRE(loaded.recentProjects == preferences.recentProjects);
  REQUIRE(loaded.sourceRoot == "/src/sonnet");
  REQUIRE(editor::Preferences::load(scratch("nothing.json")).recentProjects.empty());
  for (std::size_t i = 0; i < editor::Preferences::MaxRecentProjects + 3; ++i) {
    preferences.addRecentProject("/projects/" + std::to_string(i));
  }
  REQUIRE(preferences.recentProjects.size() == editor::Preferences::MaxRecentProjects);
  std::filesystem::remove(file);
}
