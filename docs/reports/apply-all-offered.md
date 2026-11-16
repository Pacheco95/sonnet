# Deciding whether to offer "Apply all overrides"

Issue [#578](https://github.com/Pacheco95/sonnet/issues/578), a follow-up of [#551](https://github.com/Pacheco95/sonnet/issues/551). The inspector's "Apply all overrides" button and the hierarchy's context-menu item decide every frame they are shown whether an instance has a change that can be applied. They did it by building the whole plan (`Editor::applyAllPlan`), which walks the root and every descendant, writes each component of each one and of its prefab entity as JSON to compare them, and describes every change.

## What changed

- `Editor::applyAllOffered` runs the same walk without a plan: it stops at the first change that can be applied, skips the descriptions of what stays on the instance (names, the order of children) and builds no file list. The plan is built only when the action is used. The walk is shared, so the two cannot disagree; a test checks that they agree through edits, undo, redo and an apply.
- `World::differs` compares the two values' bytes before writing them as JSON. Equal bytes are an equal value (a pointer in both points at the same thing), so an untouched component answers without JSON; unequal bytes (padding, owned memory, -0 and 0) fall through to the JSON comparison as before. This also speeds up the inspector's change marks and scene saving, which use the same call.

No cache was added: there is no revision counter that every change to the world goes through (live drags write the world before their command is recorded), so a cached answer could go stale.

## Measurements

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
