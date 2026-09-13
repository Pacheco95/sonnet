#pragma once

#include <sonnet/core/Error.h>

#include <filesystem>
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
  // The external editor command for a location, with the placeholders filled in.
  [[nodiscard]] std::string editorCommand(const std::string &file, int line) const;
};

} // namespace sonnet::editor
