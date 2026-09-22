# ADR-0016: Instanced batches, one indirect command per batch

- **Status:** Accepted
- **Date:** 2026-09-22

## Context

[ADR-0012](0012-gpu-driven-rendering.md) gives each surviving draw its own `IndirectCommand`, and a batch is submitted as the run of commands the culling pass filled. [ADR-0014](0014-indirect-draws-without-count.md) added a second form for MoltenVK, which has no `drawIndirectCount`: every slot of the batch is drawn and a culled draw's slot carries no instances. It left the cost of that form to be measured on macOS. It has now been measured, with `renderer_tests "[benchmark]"` on an Apple M4 Max, ten thousand draws of two meshes and a hundred lights at 1080p:

| | RTX 4090 | Apple M4 Max |
|---|---|---|
| CPU, recording the passes | 0.85 ms | 0.17 ms |
| CPU, submitting the frame | 0.07 ms | **4.96 ms** |
| GPU, shadow cascade 0 | 0.002 ms counted, 0.036 ms uncounted | 0.366 ms |
| GPU, whole frame | 0.63 ms | 5.37 ms |

MoltenVK encodes one Metal draw per indirect command, on the CPU, at submission. Sixty thousand commands, ten thousand slots across six passes, cost about 83 ns each, which is 4.96 ms of every frame and scales with the draw count: the cost M7 set out to remove, back in a different place. Shadow cascade 0 shows the GPU side of the same thing, 0.366 ms where a desktop GPU spends 0.002 ms on the survivors, because the slots are fetched whether or not they draw.

A batch is already a run of draws sharing a pipeline, a front face and a mesh, sorted that way so index-buffer binds are rare. What those draws do not share is the submesh, and each needs its own object index, which is why each has a command of its own today.

## Decision

- **One indirect command per batch, drawn as instances.** A batch's command holds the submesh's `indexCount` and `firstIndex`, and its `instanceCount` is the number of draws in the batch the culling pass kept. Twelve commands replace sixty thousand, on every platform.
- **A batch is keyed by pipeline, front face, mesh and submesh.** The draw list gains the submesh to its sort key after the mesh, so a batch's draws share an index range. A mesh whose submeshes take different materials makes one batch per submesh, which is what it already costs in pipeline and index-range changes.
- **The object index moves into a visible list.** A device-local `uint` buffer, one entry per draw per job, holds the object indices of the survivors. The culling pass appends: on survival it takes a slot with an atomic add on its batch's `instanceCount` and writes its object index at that slot of the batch's range. A command's `firstInstance` is the batch's base in the list, so the vertex shader reads `visible[SV_StartInstanceLocation + SV_InstanceID]`, and each job's base is folded into `firstInstance` rather than pushed per pass.
- **The blended draws keep the direct path and take slots of their own.** The renderer writes their object indices into a reserved range of the same buffer once per frame, through the staging ring, and each direct `drawIndexed` names its slot in `firstInstance`. Every scene shader therefore finds its object exactly one way, which is what ADR-0012 asked for.
- **No draw count, and no second form.** The number of commands a pass submits is the batch count, which the CPU knows, so `drawIndexedIndirect` is enough and `drawIndirectCount` stops being used at all. The clearing dispatch writes each batch's command with no instances instead of clearing counters, and the count buffer disappears. This supersedes [ADR-0014](0014-indirect-draws-without-count.md): the two paths become one, and `DeviceDesc::disableDrawIndirectCount` goes with them.
- **`drawIndirectFirstInstance` and `multiDrawIndirect` stay required**, and are what MoltenVK already supports.

## Consequences

- macOS pays for twelve Metal draws a frame rather than sixty thousand. The 4.96 ms of submission should fall to the noise, and cascade 0's 0.366 ms with it; the benchmark is what says so, and the same run on the RTX 4090 is what shows the counted path lost nothing.
- The command buffer shrinks from one command per draw per job to one per batch per job. The visible list replaces it at four bytes per draw per job instead of twenty.
- Every scene shader gains one dependent load in the vertex stage, a value uniform across the instance, and `depth.slang`, `forward.slang`, `id.slang` and `sonnet.slang` change together as they did in ADR-0012.
- Culling keeps its atomic per survivor; the atomic moves from a counter to the command's `instanceCount`, which is a buffer the draw reads, so the barrier that already orders it still does.
- A scene whose meshes carry many submeshes gets more batches than before, since batches no longer span a mesh's submeshes. The benchmark's two meshes have one each; the samples' glTF models are what will show the real number, and `statistics().indirectCallCount` reports it.
- The tests above `rhi` assert on the calls a pass makes, and those counts change from two per pass to one per batch. The GPU tests on Lavapipe keep their pixels, and the selection-mask test keeps its job: an unselected draw must contribute no instance.
- `world` and the editor are untouched, as in ADR-0012: the renderer derives the batches from the draw items it already receives.

## Alternatives considered

- **Keep ADR-0014's two forms**: no work, and macOS keeps paying 4.96 ms of CPU and about 2 ms of GPU per frame at ten thousand draws, growing with the scene. The measurement is what rejects it.
- **Per-draw commands with the count, and a Metal indirect command buffer on macOS**: Metal's own answer to this, but not reachable through Vulkan; it belongs to a native Metal backend under [ADR-0001](0001-vulkan-1.4-only.md).
- **CPU culling on macOS only**: submits only survivors without touching the shaders, but it is the recording cost M7 removed, and a second submission path for one platform.
- **Instancing without a visible list, by making the object index the instance index**: needs the batch's objects to be contiguous in the object array, which would force the object array to be rebuilt in batch order every frame and break the editor's stable object indices.
- **A shared index arena, one command per pass**: ADR-0012 deferred it and it stays deferred; it removes the index-buffer bind, not the per-draw command, so it does not address this.

## Measuring it

The benchmark reports recording and submission separately and runs on both the RTX 4090 and the M4 Max. Landing this means: submission on the M4 Max falls from 4.96 ms to under 0.1 ms, cascade 0's GPU time falls to the survivors' cost, the RTX 4090's frame is unchanged within noise, and `indirectCallCount` reports the batch count for the samples as well as the benchmark.
