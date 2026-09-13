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
  const json document = json::parse(bytes->begin(), bytes->end(), nullptr, false);
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

std::string Preferences::editorCommand(const std::string &file, int line) const {
  std::string command = externalEditor;
  const std::string path = sourceRoot.empty() ? file : (std::filesystem::path{sourceRoot} / file).generic_string();
  replaceAll(command, "{file}", path);
  replaceAll(command, "{line}", std::to_string(line));
  return command;
}

} // namespace sonnet::editor
