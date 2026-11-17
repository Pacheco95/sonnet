#pragma once

#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Uuid.h>
#include <sonnet/world/World.h>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
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
// What of an entity "Apply to prefab" applies: one of its components, or its name, which is
// structural data rather than a registered component (World::nameTargets).
enum class ApplyKind : std::uint8_t {
  Component,
  Name,
};
// What "Apply to prefab" applies of an instance's change (docs/editor.md, "Applying to a
// prefab"): one component of `entity`, the member at `member` (a pointer into the value
// componentToJson writes), or the whole component when `member` is empty, the entity holding it or
// not (one it removed). A script property names its slot and property, with `member` pointing at
// the property in the slot. `prefab` names the root of the prefab to write to, one of
// Editor::applyChoices; without it the apply goes where World::applyTarget picks. With `kind`
// Name it is the entity's name instead, and `component` is empty.
struct ApplyRequest {
  core::Uuid entity;
  std::string component;
  nlohmann::json::json_pointer member = nlohmann::json::json_pointer{};
  std::optional<std::size_t> slot = std::nullopt;
  std::string property = std::string{};
  std::optional<core::Uuid> prefab = std::nullopt;
  ApplyKind kind = ApplyKind::Component;
};
// A prefab an apply can write to (Editor::applyChoices): its root's identity and its name.
struct ApplyChoice {
  core::Uuid prefab;
  std::string name;
};
// A prefab file an apply writes, with its text before and after.
struct PrefabFileEdit {
  std::filesystem::path file;
  std::string before;
  std::string after;
};
// An instance's component that an apply writes to a prefab.
struct AppliedComponent {
  core::Uuid entity;
  std::string component;
};
// "Apply to prefab", and "Apply all overrides", as one undo step: apply saves the scene as it is,
// writes every file's `after` and runs `rebuild` with null once, which reloads every prefab and the
// scene's instances from the files, the scene saved against the prefabs as they were. An instance
// root whose component of `applied` then equals its prefab's drops the override. Revert writes
// every `before` and runs `rebuild` with the scene saved by apply, so every instance, the applying
// one and any other whose override became equal to the prefab, is as it was. Not applied again by
// play's Keep: apply is not offered while playing.
[[nodiscard]] std::unique_ptr<ICommand> applyToPrefabCommand(std::string description, std::vector<PrefabFileEdit> files,
                                                             std::vector<AppliedComponent> applied,
                                                             std::function<void(const nlohmann::json *scene)> rebuild);
// Several commands as one undo step, applied in order and reverted in reverse.
[[nodiscard]] std::unique_ptr<ICommand> compositeCommand(std::string description,
                                                         std::vector<std::unique_ptr<ICommand>> commands);

} // namespace sonnet::editor
