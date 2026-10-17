# ADR-0032: An instance stores the order of its children when it differs from the default

- **Status:** Accepted
- **Date:** 2026-10-17

## Context

[ADR-0028](0028-nested-scene-instances.md) lists "reordering an instance's source children" under *Not in the first version*. [#260](https://github.com/Pacheco95/sonnet/issues/260) asks for it: a scene cannot say "on this instance, Crate 2 comes before Crate 1", or "my Light goes first".

How order works today:

- *Siblings are ordered by `SiblingOrder`*, a counter `World::assignOrder` hands out. `World::instantiate` numbers an instance's source children when it copies them, and `loadEntities` creates the scene's own entries, including children added under the instance, after that. So on every load the source children come first, in the prefab's order, and the added children follow in file order.
- *A scene file keeps the order of ordinary entities through file order.* `saveSubtree` writes children in sibling order and the loader creates them in that order. Instance descendants have no such route: an unchanged source child has no entry in the file, and a changed one has an `instanced` entry that only says what differs.
- *The hierarchy refuses both moves.* `HierarchyPanel::canMove` returns false for any entity with `PrefabSource`, and for any index that puts something before the instance's source children.
- *Parents can be unchanged.* A source child that has children of its own, none of them changed, has no entry at all (`saveInstanced` returns null when nothing differs), so a stored order has to be able to create one.
- *Parents are resolved last.* `loadEntities` attaches every entity to its parent after all entries exist, so an order can only be applied after that.

The [1.0.0](../roadmap.md#100) rule wants the scene format settled first, and this adds a field.

## Decision

**An instance's entry, or the `instanced` entry of a descendant, may carry an `order` array: the identities of that entity's children, source and added together, in sibling order. It is written only when that order differs from the default, and the scene version becomes 8.**

- *Form.* `"order": ["<uuid>", ...]` next to `components`. It lists every child of that entity at save time. Source children are listed by their derived identities, which are stable across loads, and added children by their own.
- *Default order.* The source children in the prefab's order, then the added children in their sibling order. `saveInstanced` and the instance root's writer compare the live order with this and write `order` only when they differ, so a scene with no reordered instance saves as it does today, byte for byte.
- *Where.* On the instance root's entry; on the `instanced` entry of a descendant, which also covers a nested instance, since its children are copied with it. When the descendant itself is unchanged, an `instanced` entry is written for it with an empty `components` object and the `order`. Added children keep the order among themselves that file order gives them, as today, and an added child's own children need no `order`.
- *Load.* After the parents are resolved, each stored `order` is applied once to its entity's children. The rules, which make the result the same for any prefab:
  1. An identity in the list that is not a child of that entity is skipped, with a warning naming it. This is what happens to a source child the prefab no longer has. A repeated identity is read once, at its first position.
  2. A child that the list leaves out, such as one the prefab gained, goes right after the child that precedes it in the default order, so it stays behind the sibling it follows in the prefab, wherever that sibling was moved. A child with nothing before it goes first.
  3. Everything else follows the list.
- *Editor.* `canMove` stops refusing a source child among its siblings and stops refusing an index before the source children. A source child still cannot change parent, and that refusal stays. "Make local" keeps the current order, because the entities become ordinary ones and are saved in sibling order. Undo, redo and the play-mode snapshot go through the same save and load, so they keep the order.
- *Formats.* The scene and prefab version becomes 8 and the bundle version goes up by one, since bundles carry the scenes ([ADR-0029](0029-compact-scene-files.md) did the same). Older files have no `order` and load as they did, so `migrate` has nothing to change. Both prefab files and scene files use the field, because they share the loader.

## Consequences

- A scene can order the children of an instance at any depth, source and added alike, and the order survives save, reopen, undo and play.
- The order refers to children by identity, so a rename or a move in the prefab does not detach it. A child the prefab removes is dropped with a warning, and one it adds appears behind its predecessor.
- The first save after reordering rewrites the instance's entry with a list that names every child. That is a list per reordered parent, not per child, so a reorder in a large prefab costs one line per child once, in the one entry.
- A file with a hand-written `order` that omits children is read by rule 2, not rejected.
- `Entity:child(path)` matches the first child with a name in sibling order. Reordering two children with the same name changes which one a path finds, which `docs/scripting.md` should say.
- The scene format and the bundle change once more before the freeze.

## Alternatives considered

- *An `index` on each `instanced` and added-child entry.* Simpler to write, but moving one child shifts the index of every child it passes, so either all of them get entries or the stored indices drift. It also reads badly after a prefab change: an index of 2 means a different place once the prefab gains a child.
- *Storing the order for every instance.* It removes the default-order comparison, but every scene would grow an `order` per instance and every prefab edit would then need the rule above for all of them.
- *Keeping the source children first and letting only added children move.* It needs no format change, and the hierarchy already allows it among the added children. It leaves "Crate 2 before Crate 1" and "my Light goes first" out, which is what the issue is about.
- *Placing a new child last.* The issue suggested "or last". After a reorder, last separates a new child from the sibling it follows in the prefab, so a prefab that gains a Crate 3 shows it away from Crate 2.
