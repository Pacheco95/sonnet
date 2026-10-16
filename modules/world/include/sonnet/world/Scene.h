#pragma once

#include <sonnet/world/World.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/core/Error.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sonnet::world {

// The scene and prefab file format (docs/world.md, "Scenes"): a versioned envelope around flecs'
// JSON for each component. Migrations run on load; the current version is what is written.
// Version 2 references meshes by asset identity where version 1 named a primitive; version 3
// replaces the `Script` component by `Scripts`, a list of slots; version 4 stores what an
// instance's descendants changed as "instanced" entries (ADR-0028); version 5 writes vector and
// quaternion members as arrays (ADR-0029).
// Version 6 moves exposure from Environment to Camera and adds Camera bloom strength.
// Version 7 adds camera projection kind, orthographic height and far plane.
constexpr int SceneVersion = 7;

// Every scene entity, parents before children, prefab instances as their overrides and what
// their descendants changed.
[[nodiscard]] nlohmann::json saveScene(const World &world);
// One entity and its descendants in the same format, whatever they are, for the editor's delete
// and duplicate commands; the root's parent is not recorded. loadScene brings them back.
[[nodiscard]] nlohmann::json saveSubtree(const World &world, flecs::entity root);
// Adds the file's entities to the world. Referenced prefabs must already be loaded.
[[nodiscard]] core::Result<std::vector<flecs::entity>> loadScene(World &world, const nlohmann::json &scene);

// A scene or prefab document as its file holds it: nlohmann's two-space layout, except that an
// array of numbers, a vector, stays on one line (ADR-0029). Ends with a newline.
[[nodiscard]] std::string sceneText(const nlohmann::json &document);

[[nodiscard]] core::Result<void> saveSceneFile(const World &world, const std::filesystem::path &path);
[[nodiscard]] core::Result<std::vector<flecs::entity>> loadSceneFile(World &world, const std::filesystem::path &path);

// A prefab file holds one root entity and its descendants in the scene format. Saving writes
// `root`'s subtree; loading creates them as flecs prefabs and returns the root, which a file
// load marks with PrefabFile.
[[nodiscard]] nlohmann::json savePrefab(const World &world, flecs::entity root);
[[nodiscard]] core::Result<flecs::entity> loadPrefab(World &world, const nlohmann::json &prefab);
[[nodiscard]] core::Result<void> savePrefabFile(const World &world, flecs::entity root,
                                                const std::filesystem::path &path);
[[nodiscard]] core::Result<flecs::entity> loadPrefabFile(World &world, const std::filesystem::path &path);

// A prefab document to load with others, and where it came from, for messages. `file`, when not
// empty, is the file it was read from and becomes the root's PrefabFile.
struct PrefabDocument {
  std::string source;
  nlohmann::json document;
  std::filesystem::path file;
};
// Loads prefab documents in dependency order, so a prefab may place instances of the others
// whatever the order they come in. Prefabs referenced from outside the set, the models', must be
// loaded first. A document that fails to load, or that contains itself through others, is
// refused with the reason (the chain, for a cycle) and the others load; returns the refusals.
[[nodiscard]] std::vector<core::Error> loadPrefabs(World &world, std::vector<PrefabDocument> documents);
// The same for prefab files; one that cannot be read is refused too.
[[nodiscard]] std::vector<core::Error> loadPrefabFiles(World &world, std::span<const std::filesystem::path> paths);

// A glTF file's node hierarchy as a prefab (docs/assets.md, "Importers"): the root carries the
// model's identity and name, every node becomes a child with the node's transform and, for a
// node with geometry, a MeshRenderer of its mesh and a SkinnedMesh when a skin deforms it; a
// model with animations has an Animator with the first clip on its root. Child identities derive
// from the model's, so instances save and load the same way across imports.
[[nodiscard]] flecs::entity loadModelPrefab(World &world, const assets::Model &model, const core::Uuid &uuid,
                                            std::string_view name);

} // namespace sonnet::world
