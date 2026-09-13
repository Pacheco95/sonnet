#pragma once

#include <sonnet/core/Error.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sonnet::editor {

// Per-user editor settings, kept as JSON in the platform's preferences directory.
struct Preferences {
  static constexpr std::size_t MaxRecentProjects = 10;

  std::vector<std::filesystem::path> recentProjects; // most recent first
  // The command that opens a source location from the log panel; {file} and {line} are
  // substituted. The file is repository-relative (docs/conventions.md, "Logging"), so
  // sourceRoot, when set, is put in front of it.
  std::string externalEditor{"code --goto {file}:{line}"};
  std::string sourceRoot;

  [[nodiscard]] static Preferences load(const std::filesystem::path &file);
  [[nodiscard]] core::Result<void> save(const std::filesystem::path &file) const;

  void addRecentProject(const std::filesystem::path &project);
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
