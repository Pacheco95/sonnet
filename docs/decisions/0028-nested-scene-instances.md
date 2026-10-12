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
- Overrides are whole components: an instance root shares the prefab's value until it writes the component, which flecs then copies onto the instance (`OnInstantiate, Inherit`). The inspector shows inherited and owned per component.
- flecs 4.1.6 treats an instance's children differently from its root (`flecs_instantiate_children` in `src/instantiate.c`, confirmed by the spike): each prefab child is copied into the instance as a plain entity, with no `IsA` to the prefab child and owning every component it has. Components marked `DontInherit`, `Identity` among them, are left off the copy. So `owns` says nothing about which of a child's components the scene changed, nothing links a copy back to its source, and a change to a prefab child after instantiation does not reach the copies. A child that is itself an instance in the prefab keeps its `IsA` to the inner prefab, and its own children are copied with it, so nesting itself works.
- Prefabs are loaded once, at project open or game start, in file-listing order with the models' prefabs last (`Editor::loadPrefabs`, `Game::loadPrefabs`). `loadEntities` already accepts a `prefab` entry inside a prefab file, but fails when that prefab is not loaded yet, so a prefab that contains another prefab or a model loads only if the files happen to come in the right order. Nothing reloads a prefab when its file changes.
- The editor's undo commands, its selection and the recovery file refer to entities by `Identity`, never by flecs id, and a deleted subtree is restored with the identities it had ([editor.md](../editor.md#undo-and-redo)).
- Lua instantiates prefabs at run time (`instantiate` in `scripting/src/LuaScriptRuntime.cpp`). It finds entities only across the whole world, by identity (`world.find`) or by the first matching name (`world.findByName`), so a script cannot name a child inside a particular instance.
- A prefab file has exactly one root (`loadPrefabFrom`), and a scene file has any number. Godot's scenes always have one root, which is why any Godot scene can be placed; a Sonnet scene file cannot be placed as it is.

## Decision

Proposed. The spike on `spike/nested-instances` (commit 928cce9) prototyped what follows in `world` and answered the five questions the first draft asked; its findings are at the end of this section.

**Any prefab can be instantiated into a scene or into another prefab, to any depth. An instance's descendants take identities derived from the instance's identity and their source entity's, so a scene can address them across loads, renames and moves, and saves only what differs from the source.**

- *Identity.* Every entity of a loaded prefab carries a `PrefabOrigin` holding its own identity. flecs copies it into each instantiated child, where `World::instantiate` turns it into a `PrefabSource` (the identity of the prefab entity it was copied from) and sets the copy's `Identity` to `Uuid::derive(instance, source)`. The instance root drops the `PrefabOrigin` it inherits. Nesting composes: the inner prefab's children inside an outer prefab are prefab entities with derived identities of their own, and an instance of the outer prefab derives from those. The same file loaded twice gives the same identities; two instances of one prefab give different ones. `instantiate` takes the instance's identity as a parameter, so the scene loader, undo and Lua all get derived children for the identity they ask for. Neither component is serialized.
- *What a scene stores for an instance.* The instance root's entry as today (source prefab, name, owned components). Then, for each descendant that differs from its source, an `"instanced": true` entry with the descendant's derived identity, a `name` if it was renamed, the `components` whose value differs from the source's or that the source lacks, and a `removed` list of the source's components the descendant no longer has. Since flecs copies children whole, the difference is found by comparing reflected values with the source entity's, not by `owns`. An unchanged descendant is not written.
- *Added children.* An entity created under an instance's descendant has no `PrefabSource`. It is an ordinary scene entity whose `parent` is that descendant's derived identity, so it is saved and loaded as any other.
- *Removed children.* A source child cannot be deleted from an instance; it is hidden with `Disabled`, which the diff writes like any other added component, as Godot hides rather than deletes.
- *Overrides stay whole components* in the first version: changing one field of an instance child's `MeshRenderer` makes the scene store the whole component, and later changes to the prefab's `MeshRenderer` no longer reach it. Per-field overrides are the first change after this ADR, starting with `Scripts`: there the whole component holds every slot and every property, so one changed property stops all of the instance's scripts from following the prefab.
- *When prefab changes arrive.* A component that a scene does not store comes from the prefab when the scene loads, so a prefab edit reaches every instance the next time its scene is opened. Running instances do not change, since flecs copied their children, and nothing reloads prefabs today anyway. Live propagation in the editor waits for prefab hot reload.
- *Missing targets.* An `instanced` entry whose entity no longer exists in the prefab is skipped with a warning naming its identity. The entry is lost when the scene is next saved, which is the one place this differs from keeping it.
- *Load order and cycles.* One function in `world` loads a set of prefab documents in dependency order, after the models' prefabs, since a model's prefab references nothing. `Editor::loadPrefabs` and `Game::loadPrefabs` both call it, replacing their file-order loops. The ordering finds a prefab that contains itself, directly or through others, and refuses it with the chain named.
- *What can be placed.* Only prefabs. A scene file is not placed in another scene: its many roots have no single entity to be the instance. The editor's "Save as prefab" (below) turns any part of a scene into something that can be placed, which covers Godot's use of placing a whole scene.
- *Prefab variants* (Godot's inherited scenes) are a prefab whose root is an instance of another prefab, which the rules above already describe. The editor's way to create one is not in the first version.
- *Formats.* The scene and prefab version becomes 4, for the `instanced` entries; a version 3 file has none and migrates unchanged. The bundle's version becomes 7, since bundles carry the scenes.
- *Editor.* Making a prefab from a selection ("Save as prefab": written to a `.prefab.json`, with the selection replaced by an instance); turning an instance into ordinary entities ("Make local", as Godot's command of that name: the root takes its own copy of every component it inherits and drops its `IsA`, its descendants drop their `PrefabSource`, and every entity keeps its identity, so references to them still resolve; one undoable command); the inspector marking which components of an instance's descendant differ from the source and offering to revert them; and the hierarchy treating an instance's descendants as editable.
- *Scripts.* `entity:child(path)` returns the descendant reached by a `/`-separated path of names from that entity (`cart:child("Axle/Hub")`), or `nil`, so a script can name a part of the instance it runs on, as Godot's `get_node` does. A name matches the first child with it, in sibling order. Instances keep their source children's names unless renamed, so a path written against the prefab works on every instance.

Not in the first version: per-field overrides (the first follow-up, above); placing a scene file; reordering an instance's source children; deleting rather than hiding them; creating a prefab variant from the editor; applying an instance's changes back to its prefab; live propagation of prefab edits.

What the spike found (`modules/world/tests/NestedInstanceSpikeTests.cpp` on the spike branch; all six cases pass, and so do the existing suites, `editor_tests` and `runtime_tests` included, on Lavapipe with GCC 14):

1. flecs creates a nested prefab's children inside an outer instance, and the inner instance root keeps its `IsA`. Copied children have no `IsA` and own their components, and a later change to the prefab child does not reach them.
2. flecs keeps no link from a copied child to its prefab child. Slots (`SlotOf`) exist, but cover only children marked as slots, and the spike did not try them inside nested prefabs. A copied component (`PrefabOrigin`) is the simpler link and is what the prototype uses.
3. Derived identities are equal across two worlds loading the same files, at every depth, and distinct between two instances.
4. A scene round-trips an override two levels down, a hidden source child, a renamed nested instance and a child added under an instance child. After the prefabs change, the overridden component keeps the saved values and the components the scene did not change follow the prefab. An override inside an outer prefab file reaches that prefab's instances.
5. The editor's delete and undo path (`saveSubtree`, destroy, `loadScene`) brings an instance back with its descendants' identities, their overrides and the children added under them. Prefabs are never reloaded today, so there is no live instance to update.

## Consequences

- A scene stops losing the edits made inside an instance, and the playground and the samples can be built from prefabs of prefabs.
- Identity derivation moves into `World::instantiate`, so every instance, including those Lua creates at run time, gets stable child identities; an instance created twice under the same identity gets the same children, which undo relies on.
- `saveScene` and `loadScene` grow the `instanced` entries, prefabs load through one ordered function, and the editor gains a command for making a prefab and the inspector a revert.
- Saving an instance compares each descendant's reflected components with its source's, a cost per descendant on every save, undo snapshot and recovery write. It is the same reflection the serializer already runs, and is measured if a scene with many large instances shows it.
- The scene format and the bundle change once more before the freeze.
- Placing only prefabs differs from Godot, where any scene can be placed; a scene meant to be placed is saved as a prefab first.
- Whole-component overrides are coarser than Godot's and will surprise someone who expects a moved child to still follow its prefab's rotation; the inspector's marking is what explains it.

## Alternatives considered

- **Godot's name paths.** Addressing descendants by name path needs no identity scheme, but renaming or moving an entity in the prefab detaches the overrides silently, and Sonnet's UUIDs already avoid it.
- **Storing the whole instance expanded.** Writing every descendant in full is simple, but changes to the prefab stop reaching its instances, which is what a prefab is for.
- **Making copied children inherit**, by adding an `IsA` from each copy to its prefab child so that `owns` finds overrides as it does on roots. flecs would then instantiate the prefab child's children again under the copy, so every level would be duplicated.
- **flecs slots** as the link from a copy to its source. They cover only the children marked as slots, and how they compose across nested prefabs is untested, where a copied component is plain data.
- **A separate nested-scene feature beside prefabs.** A second mechanism for the same thing; ADR-0003 already decided that a placed scene is a prefab instance.
