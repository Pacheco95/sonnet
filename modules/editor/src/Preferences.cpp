#include <sonnet/editor/Preferences.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>

namespace sonnet::editor {

namespace {

using nlohmann::json;

void replaceAll(std::string &text, std::string_view placeholder, const std::string &value) {
  for (std::size_t position = text.find(placeholder); position != std::string::npos;
       position = text.find(placeholder, position + value.size())) {
    text.replace(position, placeholder.size(), value);
  }
}

} // namespace

Preferences Preferences::load(const std::filesystem::path &file) {
  Preferences preferences;
  const auto bytes = core::readFile(file);
  if (!bytes) {
    return preferences; // first run
  }
  const json document = json::parse(std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}, nullptr, false);
  if (document.is_discarded() || !document.is_object()) {
    SONNET_LOG_WARN("{}: not valid JSON, using defaults", file.string());
    return preferences;
  }
  if (document.contains("recentProjects") && document["recentProjects"].is_array()) {
    for (const json &entry : document["recentProjects"]) {
      if (entry.is_string()) {
        preferences.recentProjects.emplace_back(entry.get<std::string>());
      }
    }
  }
  preferences.externalEditor = document.value("externalEditor", preferences.externalEditor);
  preferences.sourceRoot = document.value("sourceRoot", preferences.sourceRoot);
  return preferences;
}

core::Result<void> Preferences::save(const std::filesystem::path &file) const {
  json recent = json::array();
  for (const std::filesystem::path &project : recentProjects) {
    recent.push_back(project.generic_string());
  }
  const json document{
      {"recentProjects", recent},
      {"externalEditor", externalEditor},
      {"sourceRoot", sourceRoot},
  };
  std::ofstream stream{file};
  if (!stream) {
    return std::unexpected(
        core::Error{std::format("{}: cannot open for writing", file.string()), core::ErrorCategory::Io});
  }
  stream << document.dump(2) << '\n';
  return {};
}

void Preferences::addRecentProject(const std::filesystem::path &project) {
  std::erase(recentProjects, project);
  recentProjects.insert(recentProjects.begin(), project);
  if (recentProjects.size() > MaxRecentProjects) {
    recentProjects.resize(MaxRecentProjects);
  }
}

std::string Preferences::editorCommand(const std::filesystem::path &file, int line) const {
  std::string command = externalEditor;
  replaceAll(command, "{file}", file.generic_string());
  replaceAll(command, "{line}", std::to_string(line));
  return command;
}

std::optional<std::filesystem::path> locateSource(const std::filesystem::path &file,
                                                  const std::filesystem::path &sourceRoot,
                                                  const std::filesystem::path &basePath) {
  std::error_code error;
  if (!sourceRoot.empty()) {
    const std::filesystem::path candidate = sourceRoot / file;
    return std::filesystem::is_regular_file(candidate, error) ? std::optional{candidate.lexically_normal()}
                                                              : std::nullopt;
  }
  if (file.is_absolute()) {
    return std::filesystem::is_regular_file(file, error) ? std::optional{file} : std::nullopt;
  }
  for (std::filesystem::path directory = std::filesystem::absolute(basePath, error); !directory.empty();
       directory = directory.parent_path()) {
    const std::filesystem::path candidate = directory / file;
    if (std::filesystem::is_regular_file(candidate, error)) {
      return candidate.lexically_normal();
    }
    if (directory == directory.root_path()) {
      break;
    }
  }
  return std::nullopt;
}

} // namespace sonnet::editor
