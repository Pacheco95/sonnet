#pragma once

#include <sonnet/assets/Project.h>
#include <sonnet/core/Error.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace sonnet::editor {

// Creating a project is the editor's half of `assets::Project` (ADR-0011): it needs `world` to
// build the starter scene, which nothing below the editor may depend on. Writes the folder,
// project.json and the start scene; never overwrites an existing project.json. `Editor::
// createProject` is this followed by opening what it wrote.
[[nodiscard]] core::Result<assets::Project> createStarterProject(const std::filesystem::path &directory,
                                                                 std::string name);

// The scene a new project or an empty editor starts from: a ground plane, a box, a sun and a camera.
[[nodiscard]] nlohmann::json starterScene();

} // namespace sonnet::editor
