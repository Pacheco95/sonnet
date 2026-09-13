#include <sonnet/editor/Project.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Version.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace sonnet::editor {

namespace {

using nlohmann::json;

core::Result<void> writeText(const std::filesystem::path &path, const std::string &text) {
  std::ofstream file{path};
  if (!file) {
    return std::unexpected(
        core::Error{std::format("{}: cannot open for writing", path.string()), core::ErrorCategory::Io});
  }
  file << text;
  if (!file) {
    return std::unexpected(core::Error{std::format("{}: write failed", path.string()), core::ErrorCategory::Io});
  }
  return {};
}

} // namespace

core::Result<Project> Project::open(const std::filesystem::path &directory) {
  Project project;
  project.root = std::filesystem::absolute(directory).lexically_normal();
  const auto bytes = core::readFile(project.file());
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  const json document = json::parse(bytes->begin(), bytes->end(), nullptr, false);
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

core::Result<Project> Project::create(const std::filesystem::path &directory, std::string name) {
  Project project;
  project.root = std::filesystem::absolute(directory).lexically_normal();
  project.name = std::move(name);
  if (project.name.empty()) {
    project.name = project.root.filename().string();
  }
  if (std::filesystem::exists(project.file())) {
    return std::unexpected(
        core::Error{std::format("{} already exists", project.file().string()), core::ErrorCategory::Io});
  }
  std::error_code error;
  std::filesystem::create_directories(project.root / "scenes", error);
  if (error) {
    return std::unexpected(
        core::Error{std::format("{}: {}", project.root.string(), error.message()), core::ErrorCategory::Io});
  }
  if (auto saved = project.save(); !saved) {
    return std::unexpected(saved.error());
  }
  if (auto scene = writeText(project.resolve(project.startScene), starterScene().dump(2) + "\n"); !scene) {
    return std::unexpected(scene.error());
  }
  SONNET_LOG_INFO("created project \"{}\" at {}", project.name, project.root.string());
  return project;
}

core::Result<void> Project::save() const {
  const json document{
      {"name", name},
      {"engineVersion", core::engineVersion().toString()},
      {"startScene", startScene},
      {"assetRoots", assetRoots},
  };
  return writeText(file(), document.dump(2) + "\n");
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

json starterScene() {
  world::World world;
  const flecs::entity ground = world.createEntity("Ground");
  ground.set<world::Transform>({.scale = {12.0f, 1.0f, 12.0f}});
  ground.set<world::MeshRenderer>({.mesh = assets::builtin::plane(), .color = {0.45f, 0.47f, 0.5f, 1.0f}});
  ground.add<world::Static>();
  const flecs::entity box = world.createEntity("Box");
  box.set<world::Transform>({.position = {0.0f, 0.5f, 0.0f}});
  box.set<world::MeshRenderer>({.mesh = assets::builtin::box(), .color = {0.9f, 0.35f, 0.25f, 1.0f}});
  const flecs::entity sun = world.createEntity("Sun");
  sun.set<world::Transform>(
      {.position = {0.0f, 5.0f, 0.0f},
       .rotation = glm::quatLookAt(glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f}), glm::vec3{0.0f, 1.0f, 0.0f})});
  sun.set<world::DirectionalLight>({});
  const flecs::entity camera = world.createEntity("Camera");
  camera.set<world::Transform>(
      {.position = {5.0f, 3.5f, 7.0f},
       .rotation = glm::quatLookAt(glm::normalize(glm::vec3{0.0f, 0.5f, 0.0f} - glm::vec3{5.0f, 3.5f, 7.0f}),
                                   glm::vec3{0.0f, 1.0f, 0.0f})});
  camera.set<world::Camera>({});
  return world::saveScene(world);
}

} // namespace sonnet::editor
