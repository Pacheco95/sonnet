#include "AssetTestSupport.h"

#include <sonnet/assets/Project.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace sonnet;
using namespace sonnet::assets;

TEST_CASE("a project file round-trips and resolves paths relative to its folder", "[assets][project]") {
  const std::filesystem::path root = test::freshDirectory("sonnet_assets_project");
  std::filesystem::create_directories(root / "scenes");

  Project written;
  written.root = root;
  written.name = "Playground";
  written.startScene = "scenes/start.scene.json";
  written.assetRoots = {"assets", "scripts"};
  REQUIRE(written.save().has_value());
  REQUIRE(core::writeFile(root / "scenes" / "start.scene.json", std::string_view{"{}"}).has_value());
  REQUIRE(core::writeFile(root / "scenes" / "crate.prefab.json", std::string_view{"{}"}).has_value());

  const auto opened = Project::open(root);
  REQUIRE(opened.has_value());
  REQUIRE(opened->name == "Playground");
  REQUIRE(opened->startScene == "scenes/start.scene.json");
  REQUIRE(opened->assetRoots == std::vector<std::string>{"assets", "scripts"});
  // The version is stamped on save, not carried from the value that was written.
  REQUIRE(!opened->engineVersion.empty());
  REQUIRE(opened->file() == root / "project.json");
  REQUIRE(opened->resolve("scenes/start.scene.json") == root / "scenes" / "start.scene.json");
  REQUIRE(opened->relative(root / "scenes" / "start.scene.json") == "scenes/start.scene.json");
  // A path outside the project stays as it was: it cannot be named relative to the folder.
  REQUIRE(opened->relative("/elsewhere/other.scene.json") == "/elsewhere/other.scene.json");
  REQUIRE(opened->files(".scene.json").size() == 1);
  REQUIRE(opened->files(".prefab.json").size() == 1);
  REQUIRE(opened->files(".material.json").empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("an unreadable or malformed project file is an error, not an exception", "[assets][project]") {
  const std::filesystem::path root = test::freshDirectory("sonnet_assets_project_bad");
  REQUIRE(!Project::open(root / "nowhere").has_value());
  REQUIRE(core::writeFile(root / "project.json", std::string_view{"not json at all"}).has_value());
  REQUIRE(!Project::open(root).has_value());
  std::filesystem::remove_all(root);
}
