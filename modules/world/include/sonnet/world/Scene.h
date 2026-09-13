#pragma once

#include <sonnet/world/World.h>

#include <sonnet/core/Error.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <vector>

namespace sonnet::world {

// The scene and prefab file format (docs/world.md, "Scenes"): a versioned envelope around flecs'
// JSON for each component. Migrations run on load; the current version is what is written.
constexpr int SceneVersion = 1;

// Every scene entity, parents before children, prefab instances with their overrides only.
[[nodiscard]] nlohmann::json saveScene(const World &world);
// One entity and its descendants in the same format, whatever they are, for the editor's delete
// and duplicate commands; the root's parent is not recorded. loadScene brings them back.
[[nodiscard]] nlohmann::json saveSubtree(const World &world, flecs::entity root);
// Adds the file's entities to the world. Referenced prefabs must already be loaded.
[[nodiscard]] core::Result<std::vector<flecs::entity>> loadScene(World &world, const nlohmann::json &scene);

[[nodiscard]] core::Result<void> saveSceneFile(const World &world, const std::filesystem::path &path);
[[nodiscard]] core::Result<std::vector<flecs::entity>> loadSceneFile(World &world, const std::filesystem::path &path);

// A prefab file holds one root entity and its descendants in the scene format. Saving writes
// `root`'s subtree; loading creates them as flecs prefabs and returns the root.
[[nodiscard]] nlohmann::json savePrefab(const World &world, flecs::entity root);
[[nodiscard]] core::Result<flecs::entity> loadPrefab(World &world, const nlohmann::json &prefab);
[[nodiscard]] core::Result<void> savePrefabFile(const World &world, flecs::entity root,
                                                const std::filesystem::path &path);
[[nodiscard]] core::Result<flecs::entity> loadPrefabFile(World &world, const std::filesystem::path &path);

} // namespace sonnet::world
