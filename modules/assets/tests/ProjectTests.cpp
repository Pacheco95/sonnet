#include "AssetTestSupport.h"

#include <sonnet/assets/Json.h>
#include <sonnet/assets/Project.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <string>

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

TEST_CASE("a project file carries a schema version, a missing one is 1 and a newer one is refused",
          "[assets][project]") {
  const std::filesystem::path root = test::freshDirectory("sonnet_assets_project_version");
  Project written;
  written.root = root;
  written.name = "Versioned";
  REQUIRE(written.save().has_value());
  const auto saved = core::readFile(root / "project.json");
  REQUIRE(saved.has_value());
  REQUIRE(parseJson(*saved).value("version", 0) == ProjectFileVersion);

  // A file from before the field existed opens as version 1.
  REQUIRE(core::writeFile(root / "project.json", std::string_view{R"({"name": "Old", "engineVersion": "0.14.0"})"})
              .has_value());
  const auto old = Project::open(root);
  REQUIRE(old.has_value());
  REQUIRE(old->name == "Old");

  // A newer one is an error naming both versions, and the file is not touched.
  const std::string newer = R"({"version": 99, "name": "Newer"})";
  REQUIRE(core::writeFile(root / "project.json", std::string_view{newer}).has_value());
  const auto refused = Project::open(root);
  REQUIRE(!refused.has_value());
  REQUIRE(refused.error().message.find("version 99") != std::string::npos);
  REQUIRE(refused.error().message.find(std::format("{}", ProjectFileVersion)) != std::string::npos);
  for (const char *invalid : {R"({"version": 0})", R"({"version": "one"})"}) {
    REQUIRE(core::writeFile(root / "project.json", std::string_view{invalid}).has_value());
    REQUIRE(!Project::open(root).has_value());
  }
  std::filesystem::remove_all(root);
}
