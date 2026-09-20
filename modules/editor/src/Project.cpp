#include <sonnet/editor/Project.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <format>

namespace sonnet::editor {

using nlohmann::json;

core::Result<assets::Project> createStarterProject(const std::filesystem::path &directory, std::string name) {
  assets::Project project;
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
  if (auto scene = core::writeFile(project.resolve(project.startScene), starterScene().dump(2) + "\n"); !scene) {
    return std::unexpected(scene.error());
  }
  SONNET_LOG_INFO("created project \"{}\" at {}", project.name, project.root.string());
  return project;
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
