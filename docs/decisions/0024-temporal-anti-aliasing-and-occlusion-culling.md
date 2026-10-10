# ADR-0024: Temporal anti-aliasing, occlusion culling and compressed HDR environments

- **Status:** Accepted
- **Date:** 2026-10-08

## Context

M13 raises the renderer's quality without changing what a scene is. Four pieces touch the same frame, and two of them touch formats that [M14](../roadmap.md#m14-performance-targets) freezes, so they are decided together.

Constraints found in the code:

- Anti-aliasing is FXAA on an LDR copy after tone mapping. Nothing is jittered, no pass writes motion, and a view keeps no image between frames. The editor declares two views in one frame ([ADR-0021](0021-two-views-in-one-frame.md)), so history is per view.
- The depth pre-pass and the forward pass share `frame.viewProjection` and the forward pass tests depth equal, so the matrix they rasterise with must be one value. The culling pass rebuilds a frustum from each pass's matrix ([ADR-0012](0012-gpu-driven-rendering.md)).
- `ObjectData` is 96 bytes since the normal matrix was dropped, and the per-frame fill is bandwidth-bound ([roadmap](../roadmap.md#the-per-frame-fill-is-bandwidth-not-computation)): a previous matrix inside it would take back 64 of the bytes that were saved.
- Skinned and morphed draws read a per-instance deformed vertex buffer written by `skin.slang` ([ADR-0015](0015-bindless-vertex-buffers.md), [ADR-0023](0023-animation-blending-morph-targets-and-particles.md)). Their cull bounds are the mesh's bind-pose box through the transform, which is wrong in both directions once the pose moves.
- Culling writes one command per batch and the CPU never learns how many draws survived. No draw count is used ([ADR-0014](0014-indirect-draws-without-count.md)), so MoltenVK takes the same path.
- Blended draws are not in the shadow passes; masked ones are, through an alpha test.
- An environment is cooked as an uncompressed equirectangular RGBA16F `TextureData` and turned into cubes by `ibl.slang` on the frame after its creation. LDR textures cook to UASTC and transcode at load to BC7, ASTC or RGBA8, whichever the device has ([assets.md](../assets.md#textures)).

## Decision

**Temporal anti-aliasing is the default, FXAA stays selectable.** `RendererSettings::antialiasing` becomes `AntiAliasing { None, Fxaa, Taa }`, defaulting to `Taa`.

- *Jitter.* The projection of the depth pre-pass, forward pass and id pass is offset by a sub-pixel Halton(2, 3) sequence of 8 samples. The frame constants carry three matrices: `viewProjection` (jittered), `unjitteredViewProjection` and `previousViewProjection` (unjittered, of the previous frame). Shadow passes, the selection mask, the outline and the debug lines use unjittered matrices so they do not shimmer against the resolved image; a culling job uses the matrix of the pass it feeds.
- *Motion vectors.* The depth pre-pass gains a second colour attachment, `R16G16Sfloat`, holding the screen-space motion in UV units of each opaque and masked surface, so the forward pass still shades each visible surface once. Where `DrawItem::previousTransform` and the previous deformed vertices are known, motion is exact for moving and skinned objects; the sky has no depth and the resolve derives its motion from the camera's rotation alone. Blended draws and particles write no motion and take the motion of what is behind them.
- *Where the previous pose lives.* A second array, `PreviousObject { float4x3 model; uint vertexBuffer; }` (64 bytes), is filled beside `ObjectData` only when TAA is on, so the other modes pay nothing and the 96-byte entry stays. `DrawItem::previousTransform` is supplied by `world`, which keeps the previous frame's world matrix per entity (the same as the current one for an entity that appeared this frame). A skin instance keeps two deformed buffers and swaps them each frame; `PreviousObject::vertexBuffer` names the one the previous frame wrote.
- *Resolve.* A pass `taa` between the forward pass and bloom, in HDR: it takes the closest motion in a 3×3 neighbourhood, samples the history with a Catmull-Rom filter, clips it to the neighbourhood's variance box in YCoCg, weights the blend by luminance so a bright pixel does not dominate, and blends at 0.1, raised towards the current frame as the clip distance and the motion grow. History outside the screen, a resized target and `SceneView::resetHistory` (a camera cut) take the current frame alone. Bloom and tone mapping then read the resolved image, so a stable scene stops flickering through the bloom as well.
- *History.* Two `R16G16B16A16Sfloat` images per view, owned by the renderer, keyed on the view's target like the other per-view state, recreated on resize with deferred destruction and imported into the graph each frame. At 1080p they are 32 MiB per view.
- *Debug views* show the unresolved frame. A mip bias of −0.5 on material samples counters the softness of the resolve; there is no sharpening pass in this ADR.

**Occlusion culling is two-phase against a depth pyramid.**

- *The pyramid.* A compute pass builds a min-reduced mip chain from the pre-pass depth, one dispatch per level, in an `R32Sfloat` image of the view's depth size rounded down to a power of two. Depth is reversed-Z, so a texel's minimum is its farthest surface, the conservative occluder: a candidate whose nearest point (its bounds' maximum depth) is farther than that value at the mip covering its screen rectangle is hidden.
- *Phase one* culls the opaque candidates against the frustum and against the *previous* frame's pyramid with the previous matrix, and the survivors go into the depth pre-pass. *The pyramid is then built* from that depth. *Phase two* re-tests, against the new pyramid, exactly the candidates phase one rejected for occlusion, and appends the survivors to the same batch commands. A late arrival in the pre-pass is drawn by a second, late command per batch; the forward pass and every later pass draw the batch's one command, which then covers both phases. Because phase two tests against this frame's depth, the result is the same set of visible surfaces as without occlusion culling; a wrong phase-one guess only costs a draw.
- *Slots.* Phase two takes its slot with the same atomic on the batch command's `instanceCount`, so early and late survivors are contiguous in the visible run. A small dispatch between the phases snapshots each batch's early count into its late command (`instanceCount` 0, `firstInstance` after the early ones), and phase two raises both counts. The CPU still records a count it knows: one call per batch in the forward pass, two in the pre-pass.
- *Scope.* Only the pre-pass and forward pass's candidates are occlusion-culled. The shadow cascades, local shadow maps and the editor's id and mask passes stay frustum-only. A batch with no pre-pass (a view with no opaque draws) has no pyramid; the first frame of a view, a resize and a cut have none either and cull against the frustum alone.
- *Counts.* The culling dispatches add each job's survivors to a counter, copied into a host-visible buffer the way `Picker` reads an id, and `RenderStatistics` reports `drawCount` and `triangleCount` from it `FramesInFlight` frames late, beside the submitted figures it has now. No pass waits for it.
- *Bounds of deformed meshes.* A skinned mesh keeps, at creation, a box per joint over the vertices that joint influences (weight over 0.1), in the mesh's space; a draw's bounds are the union of those boxes through its joint matrices. A morphed mesh grows its box by the largest delta of each target times its weight. Both are computed by the renderer when it fills the cull candidates, in the same loop and with the same job split, so the bounds follow the pose they are drawn in with no frame of delay.

**Blended and masked materials cast shadows through a hashed alpha test.** The shadow passes' candidates include blended draws as well as opaque and masked ones; a shadow fragment of a blended or masked material discards when the surface's alpha is under a 4×4 ordered threshold chosen from its pixel, so the 3×3 hardware comparison of the sampling filter averages them into a partial shadow whose darkness follows the alpha. Blended draws are not in the depth pre-pass, whose job is to make the forward pass's equal test exact for what writes depth; they still draw after it without writing depth.

**Environments cook to UASTC HDR and transcode at load.** An environment's equirectangular map is cooked, like a colour texture, as one KTX2 payload in basisu's UASTC HDR 4×4 format, 8 bits per texel. `readKtx2` transcodes it to BC6H where the device has BC, to ASTC HDR 4×4 where it has `textureCompressionASTC_HDR`, and decodes to RGBA16F otherwise, so one cooked form runs on every platform and `CookPlatform` does not matter. `rhi::DeviceInfo` gains `bc6hSupported` (from `textureCompressionBC`) and `astcHdrSupported`. `ibl.slang` reads the compressed map as it reads the uncompressed one, so the cubes it makes do not change in kind. The bundle's version is raised and the older reading is dropped, since the formats freeze only at M14.

## Consequences

- Edges, alpha-tested foliage and shader aliasing resolve over a few frames; the cost is two images per view, a resolve pass of a few tenths of a millisecond at 1080p, a motion attachment on the pre-pass and a 64-byte previous entry per draw.
- A scene with many occluders submits fewer draws for the same pixels; a scene with none pays the pyramid build and a second cull dispatch, which the benchmark must measure. `renderer_tests "[benchmark]"` gets an occluder-heavy scene beside the current one, and both go in the roadmap.
- `world` must keep a previous world matrix per entity, and `DrawItem`, `SceneView` and the cull candidates grow fields; `skin.slang` and the instance cache double their buffers per skinned instance.
- Masks and ghosting need a test that is not a pixel equality: the goldens compare with a tolerance (a mean and a maximum error per image), are rendered on Lavapipe, and a ghosting test moves a bright object over a dark background and requires the trail to fall under a threshold within four frames.
- A cooked HDR environment is a basisu encode that costs seconds, and BC6H and ASTC HDR are lossy: the lighting it produces is checked against the uncompressed cubes within a tolerance, as the LDR textures are with PSNR floors.
- Blended surfaces cast partial shadows with visible ordered-threshold structure when the filter is small; the 3×3 kernel hides it at the default map sizes.

## Alternatives considered

- **Single-phase occlusion culling against the previous frame's pyramid**: one dispatch and no late command, but an object that moves or an occluder that moves pops for a frame, and the criterion is identical output.
- **Hardware occlusion queries**: a query per draw or per batch, results a frame late, and nothing the indirect path could use without the CPU.
- **Motion from depth reprojection alone**: no per-object data, but a moving object smears and ghosts, which is the artefact this milestone removes.
- **A previous matrix inside `ObjectData`**: the simplest shader, but 160 bytes an entry again for every mode, undoing the fill saving.
- **A separate motion-vector pass**: every opaque draw twice; writing motion in the pre-pass costs one more attachment.
- **Resolving after tone mapping**: a smaller history, but bright pixels clamp before they are averaged, so highlights shimmer and bloom flickers.
- **MSAA**: costly in bandwidth on the mobile GPUs the engine targets, with a clustered forward pass that shades per sample at edges, and it does not touch shader or alpha-test aliasing.
- **Temporal upscaling (FSR 2, DLSS)**: a different product with vendor code; the history and motion vectors here are what it would need later.
- **Per-vertex bounds read back from the skinning pass**: exact, but a frame late, so a mesh popping into view would be culled for a frame.
- **Alpha-to-coverage in the shadow maps**: needs multisampled shadow maps.
- **Cooking BC6H and ASTC HDR separately per platform**: two encoders and a bundle per family, where UASTC HDR transcodes to both from one payload.
