#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Uuid.h>

#include <nlohmann/json.hpp>

#include <cstddef>
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
// An instance of the prefab with that identity, under `parent`, with `uuid` or a fresh identity.
[[nodiscard]] std::unique_ptr<ICommand> instantiatePrefabCommand(core::Uuid prefab, std::string name, core::Uuid parent,
                                                                 core::Uuid uuid = {});
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
// Several commands as one undo step, applied in order and reverted in reverse.
[[nodiscard]] std::unique_ptr<ICommand> compositeCommand(std::string description,
                                                         std::vector<std::unique_ptr<ICommand>> commands);

} // namespace sonnet::editor
