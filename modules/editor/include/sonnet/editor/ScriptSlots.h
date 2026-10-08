#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Uuid.h>
#include <sonnet/scripting/Components.h>
#include <sonnet/scripting/ScriptRuntime.h>
#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace sonnet::editor {

// What the inspector does to an entity's script slots and their properties (docs/editor.md,
// "Script slots"), as functions on values so they can be tested without a UI. A slot's
// `properties` holds only the values that differ from the class's default; setting one back to
// the default removes it, so the scene stays as small as the author's changes.

// Out-of-range indices leave the list as it was.
[[nodiscard]] scripting::Scripts appendSlot(scripting::Scripts scripts, const core::Uuid &script);
[[nodiscard]] scripting::Scripts removeSlot(scripting::Scripts scripts, std::size_t index);
// Moves the slot at `from` so it ends at `to`, the ones between shifting by one.
[[nodiscard]] scripting::Scripts moveSlot(scripting::Scripts scripts, std::size_t from, std::size_t to);

// The slot's value for a property: the changed one, or the class's default.
[[nodiscard]] nlohmann::json propertyValue(const scripting::ScriptSlot &slot, const scripting::PropertyDecl &property);
[[nodiscard]] bool propertyChanged(const scripting::ScriptSlot &slot, const scripting::PropertyDecl &property);
[[nodiscard]] scripting::ScriptSlot setProperty(scripting::ScriptSlot slot, const scripting::PropertyDecl &property,
                                                const nlohmann::json &value);
[[nodiscard]] scripting::ScriptSlot revertProperty(scripting::ScriptSlot slot, const scripting::PropertyDecl &property);
// The names in the slot's JSON that the declarations do not include, kept in the scene.
[[nodiscard]] std::vector<std::string> undeclaredProperties(const scripting::ScriptSlot &slot,
                                                            const std::vector<scripting::PropertyDecl> &properties);

// A slot for `script` appended to the entity's `Scripts`, which is added when the entity has
// none: one undo step. Null when the entity does not exist.
[[nodiscard]] std::unique_ptr<ICommand> appendScriptCommand(const world::World &world, const core::Uuid &entity,
                                                            const core::Uuid &script);

} // namespace sonnet::editor
