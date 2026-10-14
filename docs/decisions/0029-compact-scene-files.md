# ADR-0029: Scene files write vectors as arrays

- **Status:** Accepted
- **Date:** 2026-10-14

## Context

Scene and prefab files have become too long to read or review. `apps/samples/basic/scenes/playground.scene.json` has 38 entities in 1970 lines, and most of those lines are not content:

- 748 lines each hold a single vector component (`"x": 0.0,`). `World::registerComponents` registers `vec2`, `vec3`, `vec4` and `quat` with flecs as structs with named members. `ecs_ptr_to_json` writes them as objects, and `writeJson` (`world/src/Scene.cpp`) pretty-prints with `dump(2)`, which puts each member on its own line. As a result, a single `Transform` takes 21 lines.
- 525 lines contain only a brace or a bracket.

Switching to YAML would not fix this: its block style drops the braces but keeps one line per vector member (see the alternatives below). Rewriting the playground with its vectors as arrays, and nothing else changed, cuts it from 1970 lines to 977.

The [1.0.0](../roadmap.md#100) rule wants the scene format settled before the freeze. This change alters the format, so it has to happen now or wait for a format version after 1.0.

## Decision

**Scene and prefab files write vector and quaternion members as JSON arrays. The scene version becomes 5.**

- *Form.* A member of type `vec2`, `vec3`, `vec4` or `quat` is written as an array of its components in `x, y, z, w` order, for example `"position": [0, 0.5, -10]` or `"rotation": [0, 0, 0.1305, 0.9914]`. The quaternion keeps the order of its reflected members and of glTF, not glm's storage order.
- *Where.* The conversion happens at `World`'s JSON boundary, in `valueToJson` and `componentFromJson`. It walks the component's reflected members by type, the same way `convertEmbedded` walks embedded JSON. flecs reflection stays as it is, since the inspector and Lua's `LuaReflection` walk the `x`/`y`/`z`/`w` members. Undo copies and the recovery file go through the same two functions, so they use arrays too, and a component has only one JSON form.
- *What stays.* Script property values inside `Scripts` keep their `{x, y, z}` and `{r, g, b, a}` objects, because `scripting` owns them as embedded JSON. The envelope (`uuid`, `name`, `parent`, `prefab`, `instanced`, `removed`), the two-space indentation and the set of members written are all unchanged.
- *Migration.* Only reflection knows which members are vectors, so the migration for version 4 cannot be a step in `migrate`, which sees raw JSON. Instead, `loadEntities` passes the document's original version to the component reader, which accepts the object form for vector members only in documents older than version 5. In a version 5 file, an object where an array belongs is a malformed value and is logged as one. As with every earlier migration, nothing is written back on load.
- *Formats.* The scene and prefab version becomes 5, and the bundle version becomes 9 because bundles carry the scenes. The scenes and prefabs in the repository are resaved through `loadScene` and `saveScene`. The generators in `tools/` (`generate_lighting_scene.py`, `generate_stress_scene.py`, `generate_showcase_assets.py`) write arrays. [world.md](../world.md#scenes) and [assets.md](../assets.md#scene-file) show the new form.

## Consequences

- The sample scenes are about half as long, and moving an entity changes one line (its `position` array) instead of up to three scattered member lines.
- A hand-edited file gives vector components by position instead of by name. Component and member names stay, so `"scale": [20, 1, 20]` remains readable.
- Every conversion between a component and JSON walks the value once more to rewrite its vectors. That costs about as much as `convertEmbedded` on the same path, which is small next to the text round trip through flecs. Loading should cost the same or less, because the files are about half the size and arrays have no keys. The implementation pull request measures the playground's load and save times before and after. None of this runs in the frame loop.
- The scene format and the bundle change once more before the freeze. Older files still load, but files written in the new format need an engine that knows version 5.

## Alternatives considered

- *YAML.* Its block style removes the brace lines but keeps one line per vector member, unless it uses flow style (`[x, y, z]`), which is this decision. YAML would also add a parser dependency on every platform, require migrating every scene, prefab, material and sidecar to keep the files consistent, bring implicit-typing traps (`no`, `1e3`), and lose `jq` and nlohmann across the tools and tests.
- *Leaving out members equal to the component's default.* This would shorten the playground by roughly another 100–300 lines, and an entry would show only what sets it apart. However, it makes every default value part of the format: changing one would silently change each scene that relied on it. It needs no format change, because `componentFromJson` already fills the members a file leaves out with their defaults, so it is not bound by the freeze and is left out of this decision. It is tracked as [issue #259](https://github.com/Pacheco95/sonnet/issues/259), and it shares its member comparison with [ADR-0028](0028-nested-scene-instances.md)'s per-field overrides.
- *Compact formatting only.* A custom printer that keeps short arrays and objects on one line needs no version bump, but it makes the layout depend on a hand-written printer instead of on the data. It is still possible on top of this decision.
- *Vectors as flecs opaque array types.* `ecs_ptr_to_json` would write arrays directly, but the inspector and Lua walk the named members and would both have to change, for a difference that only matters in files.
- *Binary or CBOR scenes.* The bundle already stores cooked scenes compactly. Source scenes are meant to be read, diffed and merged.
