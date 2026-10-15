# ADR-0030: A model prefab's nodes are identified by their name path

- **Status:** Proposed
- **Date:** 2026-10-15

## Context

A glTF file is a prefab ([world.md](../world.md#prefabs)), and [ADR-0028](0028-nested-scene-instances.md) gives each of an instance's descendants an identity derived from the instance's and from the prefab entity it was copied from. A scene stores what a descendant changed under that identity, so the identity of the prefab entity decides which node an override belongs to.

`loadModelPrefab` (`world/src/Scene.cpp`) derives a node's identity from its index: `Uuid::derive(model, "node/<index>")`. The index is the node's position in the importer's walk, so a re-export that inserts or removes a node shifts every identity after it. The overrides stored in a scene then belong to a different node, and nothing says so. This is a bug today, without any refresh: change the file, reopen the project, and an override on `B` is on `A`. The spike for [#304](https://github.com/Pacheco95/sonnet/issues/304) and [#305](https://github.com/Pacheco95/sonnet/issues/305) reproduced it (`world_tests [spike]` on `spike/model-prefab-refresh`).

#304 refreshes a model's prefab and its instances after a re-import, by saving the scene, rebuilding the prefab and loading the scene again. That works only if the identities of the nodes that survive the re-import do not change, so the identity scheme comes first. It is a decision of its own because the [1.0.0](../roadmap.md#100) rule wants what scenes store settled before the freeze.

Constraints found in the code:

- The importer already names a node by a path from the model's root, for the animation channels and skin joints that bind to it (`walkNodes`, `assets/src/GltfImporter.cpp`, [ADR-0010](0010-audio-and-animation.md)). Where two siblings share a name it logs a warning and the first one wins.
- A node the file leaves unnamed is named `node<index>` by the importer, where the index counts over the whole file.
- A cooked bundle carries the model's nodes with their names, and the player builds the prefab from them at load, so the derivation runs in the editor and the player from the same data.
- A scene loader skips an `instanced` entry whose entity is not in the prefab, with a warning ([ADR-0028](0028-nested-scene-instances.md), "Missing targets").

What the sample assets show (120 of the 150 models in glTF-Sample-Assets, read from their `.glb` JSON): 3 have a name shared by two siblings (`NodeVisibilityTest`, `SheenTestGrid`, `TransmissionTest`, all test scenes) and 22 have at least one unnamed node (`Duck`, `BrainStem`, `BoxAnimated`). The nine models of the repository's samples have neither.

## Decision

**A model prefab's node takes the identity `Uuid::derive(model, "path/<key path>")`, where the key path is the names of the node and its ancestors from the model's root, joined by `/`.**

- *Keys.* A node's key is its name. Among siblings with the same name, the first keeps the name and the later ones take `name#1`, `name#2`, in sibling order, which is the order in which the importer's animation binding already picks "the first". The key is only for the identity; the node's name stays what the file says.
- *Unnamed nodes.* They keep the importer's `node<index>` name, so their key still contains the file-wide index, and an insertion ahead of one shifts it. This is accepted for now: the unnamed models in the samples are hand-written, and the exporters a project is likely to use name every node. If a real project hits it, naming an unnamed node by its position among unnamed siblings fixes it in the importer, and bundles rebuild.
- *Renaming a node* in the source file gives it a new identity, so the overrides on it are lost. A sidecar map from old names to new would be additive and needs no format change; it is not part of this decision.
- *Reordering equal siblings* swaps their overrides.
- *Formats.* None changes. A derived identity is a value in the file, not a new field, so the scene and prefab versions stay, and bundles carry names and not identities. A scene saved under the index scheme has `instanced` entries that no node has any more: the loader skips them with its existing warning, and the spike confirmed it puts none on a wrong node, because a different derivation gives different identities rather than shifted ones. They are gone after the next save, as for any entry whose entity left the prefab. No file in the repository is affected: the only `instanced` entries, in `playground.scene.json` and `pallet.prefab.json`, are under file prefabs whose identities are authored. There are no projects outside the repository, and none are expected before 1.0.0, so no migration is written: a scan of the samples is all the check there is to make.
- *Where.* `loadModelPrefab` is the only place that derives a node's identity. The comment in `GltfImporter.cpp` that says the prefab derives identities from the node index is updated, and [world.md](../world.md#prefabs) describes the new scheme.

## Consequences

- An override stays on its node when a file is re-exported with nodes added or removed around it, in the same world and after a reopen, which #304 needs.
- The importer's animation binding and the override identity follow one rule, so what is ambiguous for one is ambiguous for the other, and the warning the importer already logs covers both.
- Every model-instance override in a scene saved before this change is dropped, with a warning, the next time the scene is loaded and saved. The samples have none; a project that does has to redo them.
- The limits above (unnamed nodes, renames, equal siblings reordered) are the cost of having no identity in the glTF file; the format has none to use.

## Alternatives considered

- **Keep the index and refresh anyway.** #304 would then merge or reload onto identities that shift, so the refresh itself moves overrides. The spike showed the override moving to the wrong node.
- **A stable identity stored in the file**, such as a glTF `extras` field. Blender and most exporters do not write it, and the file's author would have to keep it through every tool.
- **A sidecar that records each node's identity** the first time the file is imported. It survives renames, but a sidecar listing every node has to be kept in step with the file, grows with it, and still needs a rule to match a changed file to its record, which would be the name path anyway.
- **Hashing a node's content** (mesh, transform, name). It survives an insertion, but any edit to the node changes its identity, which is the case an override matters most.
- **Naming unnamed nodes by sibling position now.** It removes the file-wide index from their keys, at the cost of an importer change, new display names and rebuilt bundles; deferred until a project needs it.
