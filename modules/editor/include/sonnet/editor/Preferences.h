#pragma once

#include <sonnet/core/Error.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sonnet::editor {

// The scene tabs a project had open when the editor last closed it (docs/editor.md, "Scene tabs").
struct OpenScenes {
  std::vector<std::filesystem::path> scenes; // in tab order; untitled scenes are not kept
  std::size_t active{0};                     // index into `scenes`

  [[nodiscard]] bool operator==(const OpenScenes &) const = default;
};

// How the Assets panel shows a project's folder: the list, or a grid of tiles (docs/editor.md, "Grid view").
struct AssetsView {
  static constexpr float MinTileSize = 48.0f;
  // Unscaled pixels (the display scale is applied where the tile is drawn). 128 is the thumbnails' size, so a tile
  // does not magnify one at scale 1; a larger display scale does (docs/editor.md, "Display scale").
  static constexpr float MaxTileSize = 128.0f;
  static constexpr float DefaultTileSize = 96.0f;

  bool grid{false};
  float tileSize{DefaultTileSize};

  [[nodiscard]] bool operator==(const AssetsView &) const = default;
};

// Per-user editor settings, kept as JSON in the platform's preferences directory.
struct Preferences {
  static constexpr std::size_t MaxRecentProjects = 10;

  std::vector<std::filesystem::path> recentProjects; // most recent first
  // Per project root; only the recent projects keep an entry.
  std::map<std::filesystem::path, OpenScenes> openScenes;
  // Per project root, kept the same way.
  std::map<std::filesystem::path, AssetsView> assetsView;
  // The command that opens a source location from the log panel; {file} and {line} are
  // substituted. The file is repository-relative (docs/conventions.md, "Logging"), so
  // sourceRoot, when set, is put in front of it.
  std::string externalEditor{"code --goto {file}:{line}"};
  std::string sourceRoot;
  // Whether an editor started without a project folder opens the most recent project.
  bool reopenLastProject{true};
  bool floorGrid{true};
  float smoothViewDuration{0.2f}; // seconds; zero disables view transitions

  [[nodiscard]] static Preferences load(const std::filesystem::path &file);
  [[nodiscard]] core::Result<void> save(const std::filesystem::path &file) const;

  void addRecentProject(const std::filesystem::path &project);
  void removeRecentProject(const std::filesystem::path &project);
  // The project an editor started without one opens: the most recent, unless the setting is off.
  [[nodiscard]] std::optional<std::filesystem::path> projectToReopen() const;
  // The external editor command for a location, with the placeholders filled in; `file` is
  // what locateSource returned.
  [[nodiscard]] std::string editorCommand(const std::filesystem::path &file, int line) const;
};

// Finds the source file a log record names. `file` is repository-relative (docs/conventions.md,
// "Logging"). `sourceRoot`, when set, is put in front of it; otherwise the directories from
// `basePath` (the executable's) upwards are tried, which finds the checkout a build directory
// lives in. Nothing when the file exists in none of them.
[[nodiscard]] std::optional<std::filesystem::path> locateSource(const std::filesystem::path &file,
                                                                const std::filesystem::path &sourceRoot,
                                                                const std::filesystem::path &basePath);

} // namespace sonnet::editor
