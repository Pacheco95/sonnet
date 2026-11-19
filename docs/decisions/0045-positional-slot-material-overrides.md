# ADR-0045: An instance overrides material slots one by one

- **Status:** Accepted
- **Amends:** [ADR-0037](0037-per-field-instance-overrides.md)
- **Date:** 2026-11-19

## Context

[ADR-0037](0037-per-field-instance-overrides.md) stores an instance's component as a patch of the members that differ, and treats every list except `Scripts.slots` as one value: "written whole when any element differs, and replaces the prefab's". `MeshRenderer::slotMaterials` ([ADR-0041](0041-skin-shading.md#representation), [#542](https://github.com/Pacheco95/sonnet/issues/542)) is such a list, indexed by `Submesh::materialSlot`. An instance that gives slot 0 a material of its own therefore stores the whole list, stops following the prefab's slot 1, and Revert and Apply in the inspector act on the whole list. ADR-0041 already says "a prefab instance overrides one slot as a per-field override"; that does not hold today. [#600](https://github.com/Pacheco95/sonnet/issues/600) asks whether to override slots one by one, and it contradicts ADR-0037's text, so it needs a decision before the [1.0.0](../roadmap.md#100) freeze or not at all.

What the code does today:

- `componentPatch` (`world/src/World.cpp`) writes a member when it differs, recursing into objects. An array that is not a matched member is a leaf and is written whole. An instance that clears the prefab's slots stores `"slotMaterials": []`.
- `World::matchBy` pairs the elements of one list for a patch (`Scripts.slots`, registered by `scripting`), and the loader tells the two forms apart by their JSON type: an array replaces, an object patches. Matching needs object elements with a key member, which a `Uuid` string does not have, so `slotMaterials` cannot be registered with it.
- `MeshRenderer` is known to `world`, so `world` can register the pairing itself, unlike `Scripts`.
- A submesh draws with its slot's entry, then `material`, then the mesh's own; a nil entry falls through, and an entry past the mesh's slots is ignored and warned about once ([world.md](../world.md#components)).
- `setSlotMaterial` (editor) pads with nil before a slot and drops trailing nils, so values made in the editor never end in a nil.
- Cooking passes scene and prefab JSON through unchanged, and the player loads it with the same `loadScene`.
- `World::applyTargets` decides where Apply writes by comparing `value.at(pointer)` of the source and the instance, and treats a missing index as absent.
- The repository has one user of a slot override on an instance: `apps/samples/character/prefabs/character.prefab.json`, written by `tools/generate_character_level.py`.

## Decision

**An instance's `slotMaterials` is compared and merged by position. The patch is an object keyed by slot index, holding only the slots the instance changed, and every slot it leaves out follows the prefab. The pairing is a second kind of list matching next to `World::matchBy`, and it is registered for `slotMaterials` only.**

- *Mechanism.* `World::matchByIndex(component, member, padding)` registers a positional list, where `padding` is the JSON value of an absent element. `world` registers `("MeshRenderer", "slotMaterials", <nil UUID string>)` beside the component. No other member is registered: `MorphWeights.weights`, `Animator.layers` and the struct lists stay whole, as ADR-0037 says.
- *Patch form.* When both sides are arrays, the patch compares the two lists with their trailing nil entries dropped, pads the shorter with nil, and writes an object keyed by the decimal index for each index whose element differs:

  ```json
  "MeshRenderer": { "slotMaterials": { "1": "6f0c..." } }
  ```

  The value is the instance's element, which may be the nil UUID string. Equal lists write no key, so a component equal to its prefab's is left out as before.
- *Loading.* The loader tells the forms apart by their JSON type, as ADR-0037 already does for `Scripts.slots`: an object patches the target's list by position, and an array still replaces it. Every file written before this decision, in either scene version, therefore loads as it did. The merge grows the target's list with nil to the highest index named, sets each element the object names, leaves the others, and drops trailing nils.
- *Nil and length.*
  - A nil value is an explicit override: the slot falls through to `material` or the mesh's own, and a later prefab material for that slot does not reach it. This is ADR-0037's `null` rule applied to a UUID.
  - A key the object leaves out follows the prefab. An instance cannot pin a slot equal to the prefab's, as for every other member.
  - Trailing nils are insignificant on both sides. An instance whose list is `[A]` over a prefab's `[A, B]` stores `{"1": "<nil>"}` and loads as `[A]`; an instance longer than the prefab stores the keys past the prefab's end.
- *Clearing the list.* An instance whose list is empty over a prefab's `[A, B]` stores a nil for each slot the prefab has at that save, so it falls through at those slots. Slots the prefab gains later are followed. The empty array `[]` is no longer written by a save, though it still loads and replaces.
- *Malformed keys.* A key that is not a non-negative decimal integer, an index above 1024, a value that is not a UUID string, and a member that is neither an array nor an object are each skipped with a warning naming the entity and the key, as `world` already reports a matched element that has gone. The rest of the component loads, and nothing throws. The cap of 1024 keeps a hostile key from allocating a large list.
- *Apply.* `World::applyTargets` compares an indexed member through the padding, so an index past the end and a nil entry are the same slot there as in the draw list, and Apply is offered or refused for the right slots.
- *Formats.* The scene and prefab version becomes 10 and the bundle version 22, with a migration that changes nothing: a version 9 or older file loads as it does today. The bump is there so that an engine without this decision refuses a version 10 file instead of misreading an object where it expects a list. The repository's `character.prefab.json` is resaved through `loadScene` and `saveScene`, and `tools/generate_character_level.py` writes `{"0": ...}`.
- *Cook.* Unchanged. A cooked scene carries the object form as CBOR, and the player merges it with the same loader.
- *Inspector.* Each overridden slot in `drawSlotMaterials` is marked and has its own Revert and Apply, which act on the pointer `slotMaterials/<slot>` as one undoable command, through the same `revertMember` and apply paths as every other member. The row's whole-list Revert and Apply stay. A slot past the mesh's slots has no block, keeps the existing "ignored" warning, and can be reverted only from the row.

## Consequences

- Overriding one slot no longer detaches the others: a prefab's change to slot 1 reaches an instance that overrides slot 0, and Revert and Apply work per slot. The docs ([world.md](../world.md#components), [editor.md](../editor.md#material-slots), [assets.md](../assets.md#the-bundle)) and the ADR-0041 sentence quoted in the context become true; `docs/world.md` no longer says the list is "written whole as any list is".
- A slot's identity is its index, as it was for the whole list and for `Submesh::materialSlot`. A re-import that reorders or inserts a model's materials moves an instance's stored slots onto other slots; only the stored ones are affected, where before all were. This is not solved here.
- Nil is a stored override. An instance that fell through at slot 1 keeps doing so when the prefab later sets a material there; the docs have to say so.
- The inspector's "(changed)" mark compares whole values, so `[X]` against `[X, nil]` can be marked with an empty override object. Editor-made lists never end in a nil, so this only shows for hand-written files.
- The scene and prefab version and the bundle version change once more before the freeze; bundles need cooking again, and a version 10 file needs an engine that knows version 10. ADR-0046's hair work ([#591](https://github.com/Pacheco95/sonnet/issues/591)) also plans a bundle format change: the later of the two takes the next number, and whoever lands second rechecks the numbers in [assets.md](../assets.md#the-bundle) and ADR-0041.
- `world_tests` expectations that assert `{"slotMaterials": [nil, green]}` and `{"slotMaterials": []}` change to the object form. New cases cover the patch form, nil and length rules, old files, malformed keys, `applyTargets`, a descendant instance, and a cooked round trip; `editor_tests` cover per-slot Revert, Apply and apply all.
- The mechanism is generic and registers one member, so another list can opt in later with one call. It would need its own decision on padding and on what an element's identity is.
- Versioning policy: [conventions.md](../conventions.md#versioning) says a schema bump ships with a migration and that the engine never writes an old version, and [world.md](../world.md#scenes) describes migrations as applied on load and never written back. This ADR follows that text: the migration exists and is a no-op. No document states a rule that migrations are skipped before 1.0.0, and this ADR does not rely on one.

## Alternatives considered

- **Close the issue** and keep a list as one value, as ADR-0037 reads. Cheapest, but an instance of a model with a skin slot stops following every other slot as soon as it changes one, and a later adoption would need a version bump anyway.
- **The file and load part only, with no per-slot inspector buttons.** About half the work, and instances would follow slots, but Revert and Apply would stay whole-list while the file holds per-slot patches, which reads as a bug.
- **Also `MorphWeights.weights`.** A float list could use the same mechanism with padding 0, but nothing asks for it, and each registration is a format decision.
- **Also `Animator.layers` and the struct lists.** Positional pairing of struct elements breaks when the prefab adds or removes one, as ADR-0037 found for slots.
- **An explicit `"$length"` key.** An exact round trip, but an instance that shortened the list then pins its length and ignores slots the prefab gains later.
- **The whole array when the lengths differ.** Simpler, but a shorter instance list stops following the prefab, which is the behaviour this decision removes.
- **No version bump**, as for additive members ([ADR-0029](0029-compact-scene-files.md), ADR-0039). This is not additive: an existing member gets a new form, and an older engine would log an invalid value and drop the override instead of refusing the file.
- **Registering `slotMaterials` with `World::matchBy`.** It needs object elements with a key member, and a `Uuid` string has none; a key by value would pair slots by their material, not their slot.
