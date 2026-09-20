#pragma once

#include <sonnet/core/Error.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::assets {

// A project folder with its project.json (docs/assets.md, "Project file"). Everything in the
// project is referenced relative to the folder, so a project is portable. The editor opens one
// to edit it and the player to run it (ADR-0011); creating one, which needs a starter scene, is
// the editor's (docs/editor.md, "Projects and scenes").
struct Project {
  std::filesystem::path root;
  std::string name;
  std::string engineVersion; // as last saved
  std::string startScene{"scenes/main.scene.json"};
  std::vector<std::string> assetRoots{"assets", "shaders", "scripts"};

  [[nodiscard]] static core::Result<Project> open(const std::filesystem::path &directory);
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

} // namespace sonnet::assets
