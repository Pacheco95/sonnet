# Deciding whether to offer "Apply all overrides"

Issue [#578](https://github.com/Pacheco95/sonnet/issues/578), a follow-up of [#551](https://github.com/Pacheco95/sonnet/issues/551). Measured in Release for [#612](https://github.com/Pacheco95/sonnet/issues/612): see [Release build](#release-build-612). Re-measured for [#602](https://github.com/Pacheco95/sonnet/issues/602), after the walk took names and children's order ([#550](https://github.com/Pacheco95/sonnet/issues/550), [#562](https://github.com/Pacheco95/sonnet/issues/562)) and `MeshRenderer` took slot materials ([#594](https://github.com/Pacheco95/sonnet/pull/594)): see [Re-measured](#re-measured-602). The inspector's "Apply all overrides" button and the hierarchy's context-menu item decide every frame they are shown whether an instance has a change that can be applied. They did it by building the whole plan (`Editor::applyAllPlan`), which walks the root and every descendant, writes each component of each one and of its prefab entity as JSON to compare them, and describes every change.

## What changed

- `Editor::applyAllOffered` runs the same walk without a plan: it stops at the first change that can be applied, skips the descriptions of what stays on the instance (names, the order of children) and builds no file list. The plan is built only when the action is used. The walk is shared, so the two cannot disagree; a test checks that they agree through edits, undo, redo and an apply.
- `World::differs` compares the two values' bytes before writing them as JSON. Equal bytes are an equal value (a pointer in both points at the same thing), so an untouched component answers without JSON; unequal bytes (padding, owned memory, -0 and 0) fall through to the JSON comparison as before. This also speeds up the inspector's change marks and scene saving, which use the same call.

No cache was added: there is no revision counter that every change to the world goes through (live drags write the world before their command is recorded), so a cached answer could go stale.

## Measurements (#578)

`editor_tests "[benchmark][apply]"`, a hidden test case: a prefab of a root with a point light and 300 parts, each with a `Transform` and a `MeshRenderer`, instantiated once. Each number is the best of 20 runs of the check the panels make, in milliseconds. Linux Debug build (GCC 14), Lavapipe device, on a desktop shared with other builds; a Release build was not measured.

| Instance | Before (plan) | Byte comparison only | After (`applyAllOffered`) |
| --- | ---: | ---: | ---: |
| Untouched | 30.8 | 3.9 | 2.2 |
| The last part changed | 30.8 | 4.0 | 2.3 |
| Every part moved | 46.1 | 33.5 | 0.6 |

Building the full plan with every part moved, which now happens only when the action is used, takes 33.4 ms (46.1 before the byte comparison). The untouched instance is the common case and the worst one for an early stop, since the whole walk runs; what remains there is asking each of the 300 parts for each registered component.

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/linux-debug/modules/editor/editor_tests "[benchmark][apply]"
```

## Re-measured (#602)

Re-run on `main` at 153df93, which checks renames and children's order as well, the untouched instance took 9.8 to 11.5 ms, four times the 2.2 ms above, and not the 2.6 ms the [#562](https://github.com/Pacheco95/sonnet/issues/562) branch measured. The checks were not the cause. [#594](https://github.com/Pacheco95/sonnet/pull/594) gave `MeshRenderer` a `std::vector` of slot materials, so it is no longer trivially copyable: flecs copies it member by member, the copy's bytes need not match its prefab's (the padding after `visible`, which a member-wise copy does not carry, and the vector's pointers when it holds slots), and the byte comparison of `World::differs` stopped matching. Counting the calls that fell through to JSON confirmed it: every `MeshRenderer` of the 300 parts, on every check, and of `Transform` only the edited ones.

The fix is in `world`: `MeshRenderer` and `Animator` (whose `std::string` and vector of layers never matched byte for byte either) got a defaulted `operator==`, `World::registerComponent` registers a component's `operator==` as its flecs equals hook, and `World::differs` asks that hook instead of comparing bytes when there is one. A no still falls through to JSON, as unequal bytes do. Equality of the members matches what the JSON comparison decides (a `Uuid` by value, -0 equal to 0 as nlohmann's comparison of numbers has it), so no answer changes. The inspector's marks and scene saving go through the same call and gain the same.

Same benchmark, Linux Debug build (GCC 14), Lavapipe device, best of 20 in milliseconds, three runs each:

| Instance | 153df93 (main) | With the equals hook |
| --- | ---: | ---: |
| Untouched | 9.8 to 11.5 | 2.5 |
| The last part changed | 9.9 to 11.5 | 2.6 |
| Every part changed | 0.81 | 0.79 to 0.81 |
| Full plan, every part changed | 38.9 to 40.6 | 32.0 to 32.5 |

The 153df93 runs were on an otherwise idle desktop; the runs with the hook were on the same desktop while other builds ran in parallel (load average 7 to 14 on 16 threads); six runs agreed within 0.03 ms on the checks and 0.5 ms on the plan. The untouched check is now 0.3 ms above #578's 2.2 ms, the cost of the two checks [#550](https://github.com/Pacheco95/sonnet/issues/550) and [#562](https://github.com/Pacheco95/sonnet/issues/562) added (compare 2.3 and 2.6 ms on the #562 branch, before #594). A Release build is measured below.

## Release build (#612)

The same benchmark on `main` at cc0d4a3, run three times on each build in a row on an otherwise idle desktop (load average 1.4 on 16 threads; AMD Ryzen 7 9800X3D, Lavapipe device). Best of 20 in milliseconds; the range covers the three runs.

| Instance | GCC 14 Release (`-O3`) | Clang 22 `linux-release` (`-O2 -g`) | GCC 14 Debug |
| --- | ---: | ---: | ---: |
| Untouched | 0.33 | 0.33 to 0.35 | 2.63 |
| The last part changed | 0.34 to 0.35 | 0.34 to 0.36 | 2.71 to 2.74 |
| Every part changed | 0.10 | 0.11 | 0.80 |
| Full plan, every part changed | 2.88 to 3.15 | 2.96 to 2.99 | 31.97 to 32.22 |

The `linux-release` preset builds `RelWithDebInfo` with the default compiler, Clang 22 on this machine; the GCC 14 column is a plain `Release` build with `g++-14`. Both compilers land within 0.02 ms of each other on the checks. A Release build is about eight times faster than Debug on the checks and eleven times on the plan: the untouched instance, the worst case for the check, costs 0.33 ms a frame, under 1 ms, so no follow-up is proposed. The Debug numbers match the #602 re-measurement (2.5 ms there was taken under load).

```sh
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
  ./build/linux-release/modules/editor/editor_tests "[benchmark][apply]"
```

Logs: `~/sonnet-measurements/612/`.
