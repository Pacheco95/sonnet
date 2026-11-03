# ADR-0037: An instance stores the fields it changed, and the rest follow the prefab

- **Status:** Accepted
- **Amends:** [ADR-0028](0028-nested-scene-instances.md)
- **Date:** 2026-11-03

## Context

[ADR-0028](0028-nested-scene-instances.md) keeps an instance's overrides as whole components and names per-field overrides, starting with `Scripts`, as its first follow-up ([#380](https://github.com/Pacheco95/sonnet/issues/380)). The [1.0.0](../roadmap.md#100) rule wants the scene format settled before the freeze, and this changes it.

What the code does today:

- An instance root saves every component it owns whole (`saveEntity` in `world/src/Scene.cpp`). A descendant of an instance saves whole each component whose value differs from its source's (`saveInstanced`, through `World::differs`, which compares the two values as `componentToJson` writes them). Loading hands each stored value to `World::componentFromJson`.
- `componentFromJson` calls `ensure` and lets flecs assign the members the value holds. On an instance root `ensure` starts from the inherited value, and a descendant already holds its source's copy, so a plain struct member left out of the value keeps the prefab's. A `std::vector` member (`Scripts.slots`, `Animator.layers`, `MorphWeights.weights`) takes the array's length and is assigned element by element at the same index (the opaque type's `resize` on pop, `src/addons/meta/cursor.c` in flecs 4.1.6), and an embedded JSON member (`ScriptSlot.properties`, a string flecs sees as one value) is replaced whole. So flecs' assignment is a merge only by accident, and not where `Scripts` needs one.
- `Scripts` is `slots: [{script, properties}]`. `World::embedJson("Scripts", "properties")` writes `properties` as the JSON object it holds, so a member-by-member diff of JSON reaches single script properties without code specific to `Scripts`. A slot's `properties` holds only the values that differ from the script's declared defaults ([scripting.md](../scripting.md)): a missing name means the default, and a cleared `entity` or `asset` property is `null`. The inspector's per-property `revert` removes the name (`revertProperty` in `editor/src/ScriptSlots.cpp`), which goes back to the script's default, not to the prefab's value. Slots can be added, removed, reordered and pointed at another script, and two slots may run the same script.
- The inspector marks a component "(changed)" when it differs from the prefab and offers "Revert to prefab" for the whole component. An instance root's `Transform` is never marked: it places the instance and is its own.
- The world, the undo commands (`componentCommand` keeps whole before and after values), the inspector and Lua all work on whole component values. Only scene and prefab files hold overrides.
- Every editor round trip goes through `saveScene` and `loadScene` or `saveSubtree`: switching scene tabs, play and stop (the snapshot), undoing a delete, duplicating, saving as a prefab, the recovery file, rebuilding a re-imported model's instances, and cooking a bundle that the player loads.

## Decision

**A scene or prefab file stores, for each component of an instance that differs from its source, only the members that differ. On load they are merged onto the source's value, so every member the file leaves out follows the prefab. This applies to every registered component, except that an instance root's `Transform` stays whole.**

- *What is compared.* The same pairs as today: an instance root's owned components against its prefab's, and a descendant's components against its source entity's, both as `componentToJson` writes them. A member is overridden when its value differs from the source's, as a component is today (Godot's rule); nothing records which members were edited. The world keeps whole values, so flecs, the undo commands, the inspector's editing and Lua do not change: the patch exists only in files, written by the save and applied by the load.
- *Storage: a partial value.* An entry's `components` holds, for each component that differs, a patch: an object with only the members that differ, recursing into nested objects, which are reflected structs and embedded JSON alike. A component the source lacks has nothing to merge onto and is written whole, as today, and `removed` is unchanged. An instance whose slot sets `speed` to 6 and clears `target`, with everything else as in the prefab:

  ```json
  { "uuid": "41a8...", "instanced": true,
    "components": {
      "Light": { "intensity": 4 },
      "Scripts": { "slots": { "9c2e...": [ { "properties": { "speed": 6, "target": null } } ] } } } }
  ```

- *Leaves.* A number, string, boolean, `null` or array is one value. A vector (`"position": [0, 1, 0]`, [ADR-0029](0029-compact-scene-files.md)), `Animator.layers` and `MorphWeights.weights` are written whole when any element differs, and replace the prefab's. The one array matched element by element is `Scripts.slots` (below).
- *Null.* A patch's `null` is a value, never "follow the prefab", unlike a JSON merge patch (RFC 7386): a cleared property is `"target": null`. Following the prefab is written by leaving the member out. A member the source has and the instance lacks is listed in the object's `"$removed"` array. Reflected structs always have every member, so this happens only inside embedded JSON, where it is a script property reset to the script's default while the prefab sets it: `"properties": { "$removed": ["speed"] }`. No C++ member name starts with `$`, and `scripting` reports a property name that does as malformed, so the key cannot be a property.
- *Script slots are matched by their script.* `scripting` registers the match next to the embedded member (`World::matchBy("Scripts", "slots", "script")`), since `world` cannot know its component. When the instance's slots run the same scripts in the same order as its source's, `slots` is written as an object keyed by script identity, each key holding one patch per slot that runs that script, in slot order, with `{}` for an unchanged slot ahead of a changed one; a script none of whose slots changed is left out. Loading applies the n-th patch for a script to the n-th slot that runs it in the prefab's current list. A prefab that later reorders its slots or adds one is followed; a patch whose slot the prefab no longer has is skipped with a warning naming the entity and the script, and is gone after the next save, as ADR-0028's missing targets are. When the instance itself added, removed, reordered or re-pointed a slot, the sequences differ, and `slots` is written whole as an array: the instance's list is its own, and slots the prefab gains later do not reach it. The form tells the loader which it is: an array replaces, an object matches.
- *Two slots running one script* are matched by their position among the slots that run it. Swapping those two in the prefab swaps the instance's overrides between them, the one reordering this does not follow. Nothing else tells two such slots apart.
- *Which components.* Every registered component goes through the same diff, and there is no list of components that take part: the diff has no code specific to a component, and adding components later would be another format version after the freeze. The exception is an instance root's `Transform`, which stays whole, so moving a prefab's root does not move the instances already placed; the inspector already treats it as the instance's own. A descendant's `Transform` is diffed like any other component.
- *Loading.* A version 9 entry's patch is merged in JSON at the envelope: the target's current value (`componentToJson`, which on a root is the inherited one and on a descendant its source's copy) merged with the patch, then given to `componentFromJson` whole. The merge does not rely on flecs' member assignment, which matches vector elements by index and replaces embedded JSON. The diff and the merge live in `world` beside `differs`, so the inspector's marking and the save compare the same way.
- *Older files.* A version 8 or older file stores whole components, and merging one would bring back a property the instance had reset to the script's default. Such a file loads as a replacement, as today: `loadEntities` already passes the file's own version to `setComponents`, which picks replacement below 9 ([ADR-0029](0029-compact-scene-files.md) did the same for vectors). Saving it again writes patches, with the same values; the members equal to the prefab's follow it from then on. As with every migration, nothing is written back on load.
- *Inspector.* A member of an instance's component that differs from its source is marked, beside the component's "(changed)", and offers a revert to the prefab's value for that member alone: one undoable `componentCommand` with the whole value before and after. A script property keeps its `revert`, to the script's default, and gains a revert to the prefab slot's value when the two differ; the prefab slot is the one the matching above pairs it with, and a slot with no match is the instance's own, with only the default revert. An instance root's `Transform` stays unmarked, and the whole-component "Revert to prefab" stays.
- *Formats.* The scene and prefab version becomes 9 and the bundle version 20, since bundles carry the scenes. Version 8 and older files keep loading. The repository's scenes and prefabs are resaved through `loadScene` and `saveScene`.
- *Round trips.* Each path that saves and loads a scene gets a case in [#380](https://github.com/Pacheco95/sonnet/issues/380) in which an instance changes one script property, and after the round trip that property keeps its value while another property, and another slot, take a later prefab change:

  | Path | Where | Test that covers it |
  |---|---|---|
  | Save and load | `saveScene`, `loadScene` | `world_tests`: "a scene keeps the changes inside an instance and takes the prefab's other changes" |
  | Undo of a deleted instance | `saveSubtree`, `loadScene` | `world_tests`: "a deleted instance comes back with its descendants' identities and changes"; `editor_tests`: "create, delete and duplicate commands undo and redo with stable identities" |
  | Duplicate | `saveSubtree` with new identities | `editor_tests`: "an instance's duplicate keeps what its descendants changed, under identities of its own" |
  | Scene tabs | `Editor` tab content | `editor_tests`: "scenes open in tabs that keep their own edits, undo history and selection" |
  | Play and stop | the play snapshot | `editor_tests`: "play mode runs physics and scripts and stop puts everything back", "Keep re-applies the play's edits to the restored scene, unsaved and undoable" |
  | Recovery file | autosave | `editor_tests`: "a killed editor offers its unsaved scene back and restores it exactly" |
  | Save as prefab | prefab file written and placed again | `editor_tests`: "save as prefab writes a prefab file the reopened project places again" |
  | Re-imported model | instances rebuilt | `editor_tests`: "a re-imported model rebuilds its prefab and instances, keeping overrides, after play stops" |
  | Cooked bundle | cook, then the player | `runtime_tests`: "the playground's pallet of crates keeps its changes from a project and from a bundle" |

  The matching rules get `world_tests` cases of their own: a prefab that reorders, adds and removes slots, two slots running one script, a reset to the default against a prefab value (`$removed`), a cleared property (`null`), and a version 8 file that loads as a replacement.

## Consequences

- Changing one script property, or one member of any component, no longer detaches the rest of the component: the other members, and the other slots, keep following the prefab.
- Since a member is overridden only while it differs, setting it back to the prefab's value by hand, or editing the prefab to match it, makes it follow the prefab again. An instance cannot pin a value equal to the prefab's.
- A whole-array leaf, including a slot list the instance reshaped, still stops following the prefab as a whole.
- Saving walks each differing component once more to build the patch, on top of the comparison `differs` already makes; loading merges once per stored component. Neither runs in the frame loop.
- `$removed` is reserved in every object a file holds, and `scripting` rejects property names that start with `$`.
- The scene format and the bundle change once more before the freeze. Older files still load, but a version 9 file needs an engine that knows version 9.
- [#381](https://github.com/Pacheco95/sonnet/issues/381) (apply an instance's changes to its prefab) can write the patch onto the prefab's value with the same merge. [#259](https://github.com/Pacheco95/sonnet/issues/259) (leaving out members equal to the default) can reuse the diff against a default value.
- [#380](https://github.com/Pacheco95/sonnet/issues/380) covers every component, not `Scripts` first: the format applies to all of them at once, and the inspector marks members of every component.

## Alternatives considered

- **Lists of member paths** (Unity's property modifications: `slots[0].properties.speed = 6`). Equally precise, but a path grammar of its own, longer entries, and a file that no longer reads as the component it changes.
- **JSON merge patch** (RFC 7386). It uses `null` to delete, which collides with a cleared property.
- **JSON Patch** (RFC 6902). Ordered operations with paths: verbose, and a hand edit has to respect the order.
- **`Scripts` first, other components later.** Needs a list of components that take part and another format version when it grows, after the freeze.
- **Matching slots by index.** Simple, but a slot the prefab reorders or inserts ahead moves the overrides onto another script.
- **A stable identity per slot.** It would follow every reorder, even of two slots running one script, but it puts an identity in every slot of every file and has to stay right through copies and migrations, for the one case matching by script gets wrong.
- **Recording which members were edited**, as Unity does. It lets an instance pin a value equal to the prefab's, but the world, undo and the files would all carry that state, where ADR-0028 already decided by comparing values.
- **Relying on flecs' member assignment** for the merge. It already keeps plain members, but it matches vector elements by index and replaces embedded JSON whole, which is where `Scripts` needs the merge.
