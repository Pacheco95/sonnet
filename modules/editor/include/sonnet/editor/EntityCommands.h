#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Uuid.h>
#include <sonnet/world/World.h>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sonnet::editor {

// The editor's edits as commands (docs/editor.md, "Undo and redo"). Each is idempotent on
// apply, since the inspector and the gizmo change the world live and push the command when the
// edit ends.

// A new scene entity under `parent` (nil for the root) with the given component values by name.
// The identity is `uuid`, or a fresh one when nil; the caller passes one to select the result.
[[nodiscard]] std::unique_ptr<ICommand> createEntityCommand(std::string name, core::Uuid parent,
                                                            nlohmann::json components = nlohmann::json::object(),
                                                            core::Uuid uuid = {});
// Deletes the subtree; undo brings it back with the same identities under the same parent. Not
// for part of a prefab instance (a PrefabSource), which the instance would bring back on load.
[[nodiscard]] std::unique_ptr<ICommand> deleteEntityCommand(core::Uuid entity);
// A copy of the subtree with fresh identities next to the original, named "<name> copy"; the
// copy's root gets `copy` as its identity, or a fresh one when nil. Not for part of a prefab
// instance, whose changes are relative to the instance.
[[nodiscard]] std::unique_ptr<ICommand> duplicateEntityCommand(core::Uuid entity, core::Uuid copy = {});
// Moves the entity under `parent` (nil for the root) keeping its world transform, and to
// `index` among its new siblings when one is given (World::setSiblingIndex); the parent may be
// the one it has, to reorder. Undo puts it back where it was among the old siblings.
[[nodiscard]] std::unique_ptr<ICommand> reparentCommand(core::Uuid entity, core::Uuid parent,
                                                        std::optional<std::size_t> index = std::nullopt);
[[nodiscard]] std::unique_ptr<ICommand> renameCommand(core::Uuid entity, std::string before, std::string after);
// Sets a component to `after`, or removes it when `after` is empty; `before` is the value to
// restore, empty when the component was absent. A tag's value is null.
[[nodiscard]] std::unique_ptr<ICommand> componentCommand(core::Uuid entity, std::string component,
                                                         std::optional<nlohmann::json> before,
                                                         std::optional<nlohmann::json> after, std::string description);
// The value of an instance's component, or of a part of an instance, with the member at `member`
// set back to the `source`'s and every other member kept (ADR-0037): what the inspector's revert
// of one member pushes as the command's after value.
[[nodiscard]] nlohmann::json revertMember(const world::ComponentInfo &info, const nlohmann::json &value,
                                          const nlohmann::json &source, const nlohmann::json::json_pointer &member);
// An instance of the prefab with that identity, under `parent`, with `uuid` or a fresh identity.
// With a `position`, in the parent's space, the instance's Transform stands there, keeping the
// prefab root's rotation and scale; every apply, redo included, puts it back there.
[[nodiscard]] std::unique_ptr<ICommand> instantiatePrefabCommand(core::Uuid prefab, std::string name, core::Uuid parent,
                                                                 core::Uuid uuid = {},
                                                                 std::optional<glm::vec3> position = std::nullopt);
// "Make local" on a prefab instance (ADR-0028): its entities become ordinary ones with the same
// identities (World::makeLocal); undo brings the instance back.
[[nodiscard]] std::unique_ptr<ICommand> makeLocalCommand(core::Uuid instance);
// "Save as prefab": the subtree under `entity` as a prefab document, with fresh identities so the
// prefab and the instance that replaces the subtree can both be loaded. Not for part of an instance.
[[nodiscard]] nlohmann::json prefabFromSubtree(const world::World &world, core::Uuid entity);
// The second half of "Save as prefab": replaces the subtree under `entity` by an instance of the
// loaded prefab `prefab`, under the same parent, with the same name and the root's identity, as
// one undo step.
[[nodiscard]] std::unique_ptr<ICommand> replaceWithInstanceCommand(const world::World &world, core::Uuid entity,
                                                                   core::Uuid prefab);
// What "Apply to prefab" applies of an instance's change (docs/editor.md, "Applying to a
// prefab"): one component of `entity`, the member at `member` (a pointer into the value
// componentToJson writes), or the whole component when `member` is empty, the entity holding it or
// not (one it removed). A script property names its slot and property, with `member` pointing at
// the property in the slot.
struct ApplyRequest {
  core::Uuid entity;
  std::string component;
  nlohmann::json::json_pointer member = nlohmann::json::json_pointer{};
  std::optional<std::size_t> slot = std::nullopt;
  std::string property = std::string{};
};
// "Apply to prefab" as one undo step: apply writes `after` to the prefab file `file` and runs
// `rebuild`, which reloads every prefab and the scene's instances from the files; revert writes
// `before`, rebuilds, and sets the entity's component back to `value`, what it held before the
// apply (absent when empty), since the rebuild made it follow the prefab. Not applied again by
// play's Keep: apply is not offered while playing.
[[nodiscard]] std::unique_ptr<ICommand> applyToPrefabCommand(std::filesystem::path file, std::string before,
                                                             std::string after, core::Uuid entity,
                                                             std::string component, std::optional<nlohmann::json> value,
                                                             std::function<void()> rebuild);
// Several commands as one undo step, applied in order and reverted in reverse.
[[nodiscard]] std::unique_ptr<ICommand> compositeCommand(std::string description,
                                                         std::vector<std::unique_ptr<ICommand>> commands);

} // namespace sonnet::editor
