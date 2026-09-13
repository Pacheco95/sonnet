#pragma once

#include <sonnet/core/Error.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::editor {

// A project folder with its project.json (docs/assets.md, "Project file"). Everything in the
// project is referenced relative to the folder.
struct Project {
  std::filesystem::path root;
  std::string name;
  std::string engineVersion; // as last saved
  std::string startScene{"scenes/main.scene.json"};
  std::vector<std::string> assetRoots{"assets", "shaders", "scripts"};

  [[nodiscard]] static core::Result<Project> open(const std::filesystem::path &directory);
  // Creates the folder, project.json and the start scene with the starter content.
  [[nodiscard]] static core::Result<Project> create(const std::filesystem::path &directory, std::string name);
  [[nodiscard]] core::Result<void> save() const;

  [[nodiscard]] std::filesystem::path file() const {
    return root / "project.json";
  }
  [[nodiscard]] std::filesystem::path resolve(std::string_view relative) const {
    return root / std::filesystem::path{relative};
  }
  // "scenes/main.scene.json" for a path inside the project, generic form; the path itself otherwise.
  [[nodiscard]] std::string relative(const std::filesystem::path &path) const;
  // Files under the project ending in `suffix` (".scene.json", ".prefab.json"), sorted.
  [[nodiscard]] std::vector<std::filesystem::path> files(std::string_view suffix) const;
};

// The scene a new project or an empty editor starts from: a ground plane, a box, a sun and a camera.
[[nodiscard]] nlohmann::json starterScene();

} // namespace sonnet::editor
