#include <sonnet/assets/Project.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Version.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>

namespace sonnet::assets {

namespace {

using nlohmann::json;

} // namespace

core::Result<Project> Project::open(const std::filesystem::path &directory) {
  Project project;
  project.root = std::filesystem::absolute(directory).lexically_normal();
  const auto bytes = core::readFile(project.file());
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  const json document = parseJson(*bytes);
  if (document.is_discarded() || !document.is_object()) {
    return std::unexpected(
        core::Error{std::format("{}: not valid JSON", project.file().string()), core::ErrorCategory::Io});
  }
  project.name = document.value("name", project.root.filename().string());
  project.engineVersion = document.value("engineVersion", std::string{});
  project.startScene = document.value("startScene", project.startScene);
  if (document.contains("assetRoots") && document["assetRoots"].is_array()) {
    project.assetRoots = document["assetRoots"].get<std::vector<std::string>>();
  }
  SONNET_LOG_INFO("opened project \"{}\" at {}", project.name, project.root.string());
  return project;
}

core::Result<void> Project::save() const {
  const json document{
      {"name", name},
      {"engineVersion", core::engineVersion().toString()},
      {"startScene", startScene},
      {"assetRoots", assetRoots},
  };
  return core::writeFile(file(), document.dump(2) + "\n");
}

std::string Project::relative(const std::filesystem::path &path) const {
  const std::filesystem::path relativePath =
      std::filesystem::absolute(path).lexically_normal().lexically_relative(root);
  if (relativePath.empty() || relativePath.native().starts_with(std::filesystem::path{".."}.native())) {
    return path.generic_string();
  }
  return relativePath.generic_string();
}

std::vector<std::filesystem::path> Project::files(std::string_view suffix) const {
  std::vector<std::filesystem::path> result;
  std::error_code error;
  for (const auto &entry : std::filesystem::recursive_directory_iterator{root, error}) {
    if (entry.is_regular_file() && entry.path().filename().string().ends_with(suffix)) {
      result.push_back(entry.path());
    }
  }
  std::ranges::sort(result);
  return result;
}

} // namespace sonnet::assets
