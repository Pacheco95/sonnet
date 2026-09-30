# ADR-0021: Several views in one graph frame

- **Status:** Accepted
- **Date:** 2026-09-30

## Context

[#79](https://github.com/Pacheco95/sonnet/issues/79) adds a Game view beside the editor's Scene view: the same objects through two cameras into two targets, in one frame. The renderer kept one view's state as members (`m_view`, the sort orders, the batches, `m_cullJobs`, the cascades, the frame buffers and the statistics), reset by `prepareFrame` once per graph serial, and the pass lambdas captured `this`. A second `addScenePasses` in the same graph therefore took the reset path at declaration time and cleared what the first view's passes, which only record later in `execute`, were about to read. The baseline was a test that declares two cameras into two targets and reads both back: with the renderer as it stood, both targets showed the second camera's box.

The spike settled where the line between frame-wide and per-view state falls, which shape of graph makes it correct, and what a second view costs.

## Decision

- **One graph, a `ViewState` per view.** Everything a view owns is a `ViewState` the pass lambdas capture by reference, so declaring another view touches nothing the first view's passes will read. `prepareFrame` keeps one state per view pointer and target size for a graph serial and returns the existing one to the id and mask passes, so those still share the Scene view's orders and jobs. The states are pooled and keep their vectors' capacity across frames.
- **Per view:** the resolved draws (their view depth), the opaque, blended and all orders and their batches, the cull jobs and their candidates, the shadow cascades and their images, the frame constants (camera, sun, environment, cascades), the light and cluster inputs, the direct-range slots, the skinning pass's job list and the statistics. `Renderer::statistics(view)` reports a view's own; `statistics()` is the first view's, which is what a single-view frame has.
- **Frame-wide:** meshes, textures, materials, the pipelines and the cluster buffer (rewritten per view, ordered by the barriers `recordClustering` already emits). The skinned vertices are deduplicated by instance across views: the first view that draws an instance schedules its skinning, and the barrier ahead of the skinning pass covers the earlier view's reads. The lookup table's and the environments' graph imports are made once per graph frame and shared, so the passes that compute them and every view's reads name one graph image each. `m_selected` and the outline stay with the Scene view, since only the passes the editor adds for it use them.
- **One pair of cull buffers, sliced per view.** The command and visible buffers are shared; each view takes a slice after the ones before it (its cull jobs' runs, then its direct range), and the buffers grow to the sum of the slices as views are declared. Slices never overlap, so view B's clear and cull cannot race view A's draws, and `recordCulling`'s barrier against the previous frame's reads keeps its meaning for the whole buffer.
- **What #79 changes:** it declares the Game view's `addScenePasses` in the editor's one graph, into a target of its own and from a `SceneView` whose camera is the scene's `Camera` entity, sharing the Scene view's draw list; it adds no id, mask, outline or debug-line pass for it; the ImGui pass samples both targets; and the Statistics panel reads `statistics(1)`. It declares the second view only while the Game panel is open, because the cost below is paid whenever it is declared.

## Consequences

Measured with `renderer_tests "[benchmark][views]"` on the RTX 4090 in Release, two 1920×1080 targets against one, three runs each, the runs agreeing to within about 10%:

| | Showcase scale: 441 draws, 67 lights | 10 000 draws, 100 lights |
|---|---|---|
| `addScenePasses` (declaration, `prepareFrame`), 1 view → 2 | 0.016 → 0.032 ms | 0.40 → 0.79 ms |
| recording the passes | 0.035 → 0.071 ms | 0.19 → 0.37 ms |
| GPU time | 0.16 → 0.33–0.35 ms | 0.45–0.49 → 0.93–1.02 ms |
| submission | 0.003 → 0.004 ms | 0.003 → 0.004 ms |
| graph images | 98.3 → 196.5 MiB | 98.3 → 196.5 MiB |

The showcase sample has about 440 mesh renderers and 67 lights (its scene file); the benchmark builds that many draws over two meshes, so it stands in for its scale rather than being its frame. A second view costs exactly what the first does, on the CPU and on the GPU: nothing is shared beyond the buffers' allocation, which is what this prototype chose to keep simple.

- **Memory:** 98.2 MiB of graph images per view at 1080p and the default 2048 cascades, of which 64 MiB are the four cascade images (four times 2048² times 4 bytes) and the rest the HDR, bloom and colour targets. The indirect buffers are small: at 10 000 draws over two batches a view's slice is 8 jobs times 2 commands times 20 bytes, 320 bytes, and 10 000 × 8 + 10 000 slots of 4 bytes in the visible list, 360 KB; the buffers are the sum of the slices. Each view also uploads its own frame constants (816 bytes), objects (96 bytes per draw), materials, lights and two candidate arrays (48 bytes per draw each) into the frame's transient memory: 1.9 MB per view at 10 000 draws, 85 KB at the showcase's.
- **The single-view path is unchanged.** The benchmark of ten thousand draws and a hundred lights records in 0.195–0.201 ms on main and 0.201–0.204 ms on this change, and takes 0.46–0.52 ms of GPU time on main against 0.44–0.45 ms; the existing renderer, editor and trace tests pass, and the id and mask passes find the Scene view's state.
- **A Game view with shadows costs its cascades again.** A smaller `shadowMapSize` for it, or reusing the Scene view's cascades when the cameras are close, would cut the 64 MiB and the four depth passes; neither is part of this decision, and the cascade fit stays per camera because it depends on it.
- **The remaining duplication is the frame-wide data uploaded per view:** objects, materials, lights and the candidate arrays. Sharing them means one upload and one set of candidates read by both views' cull jobs, which halves the 0.40 ms of declaration at 10 000 draws and the megabytes above. It is left until a scene shows the CPU cost matters, since the fill is bandwidth-bound ([roadmap.md](../roadmap.md#the-per-frame-fill-is-bandwidth-not-computation)) and at the showcase's scale the whole second view declares in 0.016 ms.
- **The lookup table and environment imports were a hazard nobody had hit.** A second view's `addPrecomputePasses` imported them again as already computed, though the first view's passes write them on their first frame; the graph tracks state per import, so those writes were unknown to the second view's reads. Two views on the first frame now share one import; the GPU test of that frame passes with and without the fix on Lavapipe, whose barriers were enough in practice, so it guards the frame rather than proving the hazard.
- **#79's estimate:** the spike's hours were part of its 14–20 hour range, and the renderer work it found is done here, so what remains of #79 is its own 10–14 hours: the panel and its target, the camera selection and fallback, the script view and audio listener switch, and the tests. No renderer change is left for it beyond the optional sharing above.
- `SceneView` pointers must stay distinct per view within a frame, since `prepareFrame` finds a view's state by its address and target size; two views cannot share one `SceneView` object.

## Alternatives considered

- **Two graphs executed one after the other.** The renderer's per-frame state would reset between them, which works only if each graph is declared and executed before the next, and the editor's ImGui pass has to sample both targets in one graph. Each graph has its own image pool, so the transient memory would not be shared either, and the shared cull buffers still need an explicit barrier between the graphs. It buys nothing the single graph does not.
- **A second command and visible buffer set.** A sizing variant of the chosen shape: it would double the buffers to avoid slicing one, and its only benefit, view B's cull not depending on view A's slice, is already true of slices. The slices need no second allocation and no second bind.
- **Sharing the objects, materials, lights and candidates now.** The right end state, but it needs the candidates to be view-independent (they hold world-space bounds, so they are) and a frame-wide upload separate from `ensureFrameUploaded`; the measured saving is 0.4 ms at ten thousand draws and nothing visible at the showcase's scale, against a refactor of the upload and the batches. Deferred, not rejected.
