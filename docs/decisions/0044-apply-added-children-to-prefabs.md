# ADR-0044: Apply an instance's added children to its prefab

- **Status:** Accepted
- **Amends:** [ADR-0028](0028-nested-scene-instances.md)
- **Date:** 2026-11-19

## Context

An entity created under an instance or under one of its parts has no `PrefabSource`; it is an ordinary scene entity whose `parent` is the part's derived identity ([ADR-0028](0028-nested-scene-instances.md), "Added children"). The editor can apply components, names and the order of source children back into the prefab ([#381](https://github.com/Pacheco95/sonnet/issues/381), [ADR-0037](0037-per-field-instance-overrides.md), [ADR-0032](0032-instance-child-order.md)), but an added child stays on the scene: "Apply all overrides" lists it as kept and nothing else offers it ([#561](https://github.com/Pacheco95/sonnet/issues/561)).

The difficulty is identity, not the file edit. The added subtree has local identities in the scene. Once it is part of the prefab, the applying instance's copy has identities derived from the instance and the prefab's own ([ADR-0028](0028-nested-scene-instances.md), `World::instancedIdentity`). The scene's old entries must go before the instances are rebuilt, or the child exists twice. Every reference to the old identities must follow it, and undo and redo must reproduce the identities exactly.

What the code does today:

- `World::walkTargets` lists the levels of an entity's chain of sources whose prefab root has a `PrefabFile`, deepest first; `applyTargets`, `nameTargets` and `orderTargets` differ only in the `reaches` predicate. `Editor::appliedPrefabText` changes the prefab entities in the world, saves them with `savePrefab` and puts them back; `ApplyToPrefabCommand` saves the scene, writes the new files and rebuilds from the scene, and its undo rebuilds from the saved scene.
- `withFreshIdentities` (`EntityCommands.cpp`), used by Save as prefab and Duplicate, gives a subtree fresh identities and re-derives its nested `instanced` entries. It rewrites only `uuid` and `parent`: the `order` arrays of a nested instance inside the subtree keep the old identities and are skipped on load with a warning ([ADR-0032](0032-instance-child-order.md), rule 1). This is a gap that already affects those two commands.
- Nothing remaps entity references when a prefab is instantiated. flecs copies a prefab entity's component values verbatim, so a script's `entity` property that names another entity of the same prefab holds the prefab-level identity in every instance and resolves to the prefab entity, not to the instance's own copy. Hand-written prefabs have the same limit.
- Model prefabs (glTF) have no `PrefabFile`, so no level of a chain that ends in one is writable.

## Decision

An added child's whole subtree can be promoted into a prefab file, with the instance's copy taking the identities derived from the file. No scene or bundle format changes.

1. **Request.** `ApplyKind` gains `AddedChild`, next to `Name` and `ChildOrder`; the request's entity is the root of the added subtree. The existing guards apply: no project, play mode and bundles refuse. `World::addedChildTargets(anchor)` returns every level of the anchor's chain whose root has a `PrefabFile`, deepest first. Every level qualifies, since each has a counterpart of the anchor and anything added there is copied into every instance of it.
2. **Target.** The default is the deepest prefab with a file on the chain, as for every other apply kind and for Apply all; the chooser lists the others. A subtree whose chain ends in a model prefab without a file is refused with a reason; creating a prefab variant stays out of scope.
3. **Granularity.** Apply is offered on the root of the added subtree only (an entity without `PrefabSource` whose parent is an instance root or a part). A deeper node of the subtree is refused with a reason that names the root.
4. **References are refused.** The subtree's component values are scanned for strings that are the identity of an entity of the world. One that points outside the subtree (outbound) or inside it (internal) refuses the apply with a message naming the entity, component and member, because the first cannot stay valid for the other instances and the second would be written as a prefab-level identity the copies do not resolve. Asset identities never resolve in the world and null is ignored. A reference from outside the subtree to an entity of it (inbound) is allowed and rewritten in the scene (decision 7).
5. **The document.** The subtree is saved, given fresh identities (`withFreshIdentities`, which now also returns the old-to-new map and remaps `order` arrays), loaded as temporary prefab entities under the target's counterpart of the anchor, marked with `makePrefab`, saved with `savePrefab`, and destroyed: the mechanism `appliedPrefabText` already uses. Nested instances travel with their overrides, `instanced` patches, added grandchildren and `order`; their own prefabs are not rewritten. The child keeps its local transform.
6. **Position mirrors the applying instance.** The new child goes after the target's counterpart of the nearest preceding sibling of the root that has one, or first when none; several promoted in one Apply all go in their live order. The applying instance then has the default order and its stored `order` drops where possible.
7. **Identities.** The identity of the instance's copy is a fold of `instancedIdentity` along the chain from the anchor to the target, from the inside out, exposed from `world` so it is unit-tested without the editor. For each entity of the old subtree it gives old local identity to new derived identity (a nested instance's copies derive from the nested instance's new identity). The map is computed once, when the apply is built, and stored in `ApplyToPrefabCommand` with the set of old identities; redo reuses it and reproduces the same identities. `apply` saves the scene, drops every entry of the old set from that JSON, rewrites every string equal to an old identity to its new one (`parent`, `order`, `instanced` patches and any reflected `Uuid` member, with no need to know which members are entity references, since identities are random), writes the files, and rebuilds from the edited JSON. `revert` writes the old files and rebuilds from the saved scene, which holds the original local identities, so undo needs no inverse edit. The applying instance's stored `order` is remapped, never changed. Matching is never by name.
8. **Selection.** The rebuild callback carries the map, `Selection::replace(map)` remaps the selection forward and back before `prune`.
9. **Failure.** The risk is a prefab the loader then refuses: `rebuildPrefabs` keeps the previous document in memory, the scene no longer has the local subtree, and the child would vanish. Three guards: before writing, a cycle check (a nested instance in the subtree whose prefab is the target or transitively places it, via `world::documentsPlacing`) refuses, and the new document is loaded in a scratch `World`; after the rebuild, every mapped identity must exist, and if one does not, the command reverts itself and reports why. A test injects a refused document. With Apply all, each file is built once and a failure rolls back every write.
10. **Same-name sibling.** A child of the same name already under the target gives a warning in the confirmation, not a refusal: names are never keys, other instances' local children keep their identities, and only a script path `entity:child("Name")` (first match in sibling order) can change what it finds.
11. **UI.** A hierarchy item and an inspector split button "Apply to prefab" on the root of an added subtree, disabled with the reason as tooltip when refused. `Editor::applyRefusal` returns the reason; Apply all's kept entries use the same text. The confirmation counts entities and nested instances and lists the scenes, prefabs and tabs that place the file, as now.
12. **Apply all.** An added child under a visited node becomes an `AddedChild` change with the same target rule, and is kept only when refused. The order of that node's children stays kept for this round ("it has a child the prefab lacks"), so Apply order's restriction is unchanged: a second Apply all or Apply order offers it.
13. **Offered check.** It scans the subtree for references and cycles only when the subtree exists, and memoises the result per command-history position and root. The offered check and the Apply all plan must still agree, and `docs/reports/apply-all-offered.md` is re-measured.

### Tests

- `world_tests`: the fold on three shapes (anchor is the instance root, a part, a part inside a nested instance with a target two levels down); `addedChildTargets` with and without files; the scene edit (entries dropped, strings remapped, keys untouched, idempotent); `order` remapped by `withFreshIdentities`, also covered through Save as prefab and Duplicate.
- `editor_tests` (Lavapipe): single apply with new identities, other instances gaining the child, one undo step; undo, redo and selection remap, with the same derived identities after redo; a nested instance in the subtree; nested targets; order; references (inbound remapped, outbound and internal refused, null and assets ignored); refusals; Apply all in one command; a refused document rolling back. ImGui tests for the menu item and the inspector.

### Versioning

New editor behaviour and `World` API, which is a `feat` and a MINOR bump before 1.0.0. The scene and bundle formats do not change.

## Consequences

- An instance's added children are promoted in one undoable step, nested instances, overrides and order included, and Apply all no longer keeps them. ADR-0028's list of what is not in the first version loses the apply of added children.
- The command carries an identity map, so undo and redo are exact without inverse edits; the cost is one more thing a rebuild must keep in step with, which the post-rebuild check guards.
- `withFreshIdentities` now remaps `order` arrays, which also fixes Save as prefab and Duplicate of a subtree holding a reordered nested instance.
- Instantiate-time reference remapping does not exist; refusing references hides the gap for this feature only. A prefab with internal references cannot be made by applying, and the gap deserves its own issue.
- Model prefabs without a file stay unwritable; a variant-creation card is the way to lift that.
- `docs/editor.md` ("Applying to a prefab", Apply all, the hierarchy menu), `docs/world.md` (Prefabs), `docs/scripting.md` (the `child(path)` note about same-name siblings) and `docs/reports/apply-all-offered.md` are updated by the implementation.

## Alternatives considered

- **Allow internal references**, written as prefab-level identities plus an override in the applying instance: other instances would hold references to prefab entities until instantiation remaps them.
- **Allow all references with instantiate-time remapping**: needs a way to tell entity references from asset references in reflected `Uuid` members, an ADR-level change of its own.
- **The nearest prefab as the default target**: a smaller blast radius, but inconsistent with every other apply kind and with Apply all.
- **Always last among the target's children**: simpler, but the applying instance keeps a stored `order` whenever it had moved the child.
- **Splice JSON entries into the saved prefab document**: avoids temporary entities, but reimplements `order` and sibling positioning that `savePrefab` already writes, and `loadPrefabFrom` takes the same transient path.
- **An inverse identity edit for undo**: the saved scene already holds the original identities, so rebuilding from it is simpler and cannot drift.
- **Docs only, no ADR**: the identity contract crosses `world`, the editor and undo, and changes ADR-0028's not-in-the-first-version list.
- **A prefab variant for model prefabs without a file**: out of scope until the editor can create variants.
- **Offering on any node of an added subtree**: promoting the root's whole subtree from a deeper node surprises.
- **Refusing a same-name sibling**: names are never keys, so a warning is enough, as for Name and Order applies.
- **Composing Apply order with the promotion in one plan**: about six hours more for a restriction that one more round removes.
- **Restricting the offered check to a cheap precondition**: the plan would then list a refused child as kept and the offered and plan agreement test would change meaning.
