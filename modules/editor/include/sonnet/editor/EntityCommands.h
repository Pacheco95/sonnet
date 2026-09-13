#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Uuid.h>

#include <nlohmann/json.hpp>

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
// Deletes the subtree; undo brings it back with the same identities under the same parent.
[[nodiscard]] std::unique_ptr<ICommand> deleteEntityCommand(core::Uuid entity);
// A copy of the subtree with fresh identities next to the original, named "<name> copy"; the
// copy's root gets `copy` as its identity, or a fresh one when nil.
[[nodiscard]] std::unique_ptr<ICommand> duplicateEntityCommand(core::Uuid entity, core::Uuid copy = {});
// Moves the entity under `parent` (nil for the root) keeping its world transform.
[[nodiscard]] std::unique_ptr<ICommand> reparentCommand(core::Uuid entity, core::Uuid parent);
[[nodiscard]] std::unique_ptr<ICommand> renameCommand(core::Uuid entity, std::string before, std::string after);
// Sets a component to `after`, or removes it when `after` is empty; `before` is the value to
// restore, empty when the component was absent. A tag's value is null.
[[nodiscard]] std::unique_ptr<ICommand> componentCommand(core::Uuid entity, std::string component,
                                                         std::optional<nlohmann::json> before,
                                                         std::optional<nlohmann::json> after, std::string description);
// An instance of the prefab with that identity, under `parent`, with `uuid` or a fresh identity.
[[nodiscard]] std::unique_ptr<ICommand> instantiatePrefabCommand(core::Uuid prefab, std::string name, core::Uuid parent,
                                                                 core::Uuid uuid = {});
// Several commands as one undo step, applied in order and reverted in reverse.
[[nodiscard]] std::unique_ptr<ICommand> compositeCommand(std::string description,
                                                         std::vector<std::unique_ptr<ICommand>> commands);

} // namespace sonnet::editor
