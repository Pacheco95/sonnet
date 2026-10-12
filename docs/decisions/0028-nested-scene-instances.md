# ADR-0028: Nested scene instances are prefab instances whose children are addressed by derived identities

- **Status:** Proposed
- **Date:** 2026-10-12

## Context

[ADR-0003](0003-ecs-library.md) made nested scene instances prefab instantiation through flecs `IsA`, and M2 built prefabs only as far as one level: [world.md](../world.md#prefabs) says an instance is stored as its prefab's UUID plus the components it owns, and its children are not stored, because they come back with the prefab. "Nested scene instances beyond prefabs" has been in the roadmap's "Later" list since. It is chosen now because it changes the scene and prefab formats, which the [1.0.0](../roadmap.md#100) rule wants settled before the freeze, and because the gap loses work today.

The model to follow is Godot's: every scene can be placed in another, an instance is saved as a link to its source plus what the instance changed, the changes may reach the instance's children ("editable children"), children may be added under them, and nesting goes to any depth. Godot addresses an instance's nodes by name path, so renaming or moving a node in the source silently detaches the overrides that point at it; Sonnet has a UUID on every entity and can do better.

Constraints found in the code:

- Scenes and prefabs share one JSON format (`world/include/sonnet/world/Scene.h`), version 3 (`SceneVersion`). The bundle is version 6 (`BundleVersion`).
- `World::instantiate` gives the instance a fresh `Uuid::generate()`, and `adoptInstantiatedChildren` gives every instantiated child one too (`world/src/World.cpp`), so a child's identity differs on every load and nothing saved can refer to it. `saveScene` skips an instance's descendants (`world/src/Scene.cpp`), so a child added under an instance in the editor, or a component changed on one, is dropped on save without a warning.
- A model's prefab already derives its children's identities from the model's: `Uuid::derive(model, "node/<index>")` (`loadModelPrefab`). `Uuid::derive` is the tool the rest of this decision uses.
- Overrides are whole components: an instance shares the prefab's value until it writes the component, which flecs then copies onto the instance (`OnInstantiate, Inherit`). The inspector shows inherited and owned per component.
- The editor's undo commands, its selection and the recovery file refer to entities by `Identity`, never by flecs id, and a deleted subtree is restored with the identities it had ([editor.md](../editor.md#undo-and-redo)).
- Lua instantiates prefabs at run time (`instantiate` in `scripting/src/LuaScriptRuntime.cpp`).

## Decision

Proposed, to be settled by a spike on `spike/nested-instances` before acceptance (the questions are listed at the end).

**Any prefab or scene file can be instantiated into a scene or into another prefab, to any depth. An instance's descendants take identities derived from the instance's identity and their source entity's, so a scene can address them across loads, renames and moves, and saves only what differs from the source.**

- *Identity.* An entity instantiated from source entity `s` under instance `i` gets `Uuid::derive(i, s)`. Nested instances compose: a child two levels down derives from the inner instance's derived identity. The same file loaded twice gives the same identities; two instances of one prefab give different ones.
- *What a scene stores for an instance.* The instance's own entry as today (source UUID, name, owned components), plus, for each descendant that differs from its source, an entry with its source path (the chain of source UUIDs from the instance down) and its owned components. A descendant that is unchanged is not written.
- *Added children.* An entity created under an instance's descendant is an ordinary scene entity whose `parent` is that descendant's derived identity, so it is saved and loaded as any other.
- *Removed children.* A source child cannot be deleted from an instance; it is hidden with an owned `Disabled`, as Godot hides instead of deleting.
- *Overrides stay whole components* in the first version: changing one field of an instance child's `Transform` makes the instance own the whole `Transform`, and later changes to the source's `Transform` no longer reach it. Per-field overrides are a later change if they are missed.
- *Missing targets.* An override whose source entity no longer exists in the prefab is kept in the file and logged, as a missing prefab is today, so editing the prefab back restores it.
- *Cycles.* A prefab that contains itself, directly or through another, is refused at load with the chain named.
- *Prefab variants* (Godot's inherited scenes) are a prefab whose root is an instance of another prefab, which the rules above already describe; the editor's way to create one is not in the first version.
- *Formats.* The scene and prefab version becomes 4, with a migration from 3 that changes nothing (a version 3 file stores no descendants of instances). The bundle's version becomes 7, since bundles carry binary scenes.
- *Editor.* Making a prefab from a selection (written to a `.prefab.json` and the selection replaced by an instance), the inspector marking overridden components on an instance's descendants and offering to revert them, and the hierarchy showing an instance's descendants as editable.

Not in the first version: per-field overrides; reordering an instance's source children; deleting rather than hiding them; creating a prefab variant from the editor; applying an instance's overrides back to its prefab.

Questions for the spike, each answered by a test in `world_tests`:

1. Does flecs 4 instantiate a prefab child that is itself an instance of another prefab, inherit components through both levels, and keep an override on a deep instance child local to that instance?
2. Does flecs keep a link from an instance child to the prefab child it came from (an `IsA` to the prefab child, or slots), or only the name and order? This decides whether the derived identity can be computed from flecs alone or the prefab must record the source UUID on each child.
3. Do derived identities come out the same on every load, at every depth?
4. Does a scene round-trip one overridden component on a nested child, one added child and one hidden child, and does a later change to the prefab reach what the instance does not own?
5. Do undo commands still find a nested child after it is deleted and restored, and what happens to a live instance's owned components when its prefab file is reloaded?

## Consequences

- A scene stops losing the edits made inside an instance, and the playground and the samples can be built from prefabs of prefabs.
- Identity derivation moves into `World::instantiate`, so every instance, including those Lua creates at run time, gets stable child identities; an instance created twice under the same identity gets the same children, which undo relies on.
- `saveScene` and `loadScene` grow the descendant entries, the source path and the cycle check; the editor gains a command for making a prefab and the inspector a revert.
- The scene format and the bundle change once more before the freeze.
- Whole-component overrides are coarser than Godot's and will surprise someone who expects a moved child to still follow its prefab's rotation; the inspector's marking is what explains it.

## Alternatives considered

- **Godot's name paths.** Addressing descendants by name path needs no identity scheme, but renaming or moving an entity in the prefab detaches the overrides silently, and Sonnet's UUIDs already avoid it.
- **Storing the whole instance expanded.** Writing every descendant in full is simple, but changes to the prefab stop reaching its instances, which is what a prefab is for.
- **A separate nested-scene feature beside prefabs.** A second mechanism for the same thing; ADR-0003 already decided that a placed scene is a prefab instance.
