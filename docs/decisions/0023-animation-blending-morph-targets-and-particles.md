# ADR-0023: Animation blending, morph targets and GPU particles

- **Status:** Accepted
- **Date:** 2026-10-08

## Context

M5 ([ADR-0010](0010-audio-and-animation.md)) shipped one clip per `Animator`, skinning in a compute pass and deferred the rest: blending and crossfades, morph targets, animation events, root motion and several clips on one entity. M12 adds them and adds particles. All of it touches the clip, mesh and bundle formats, which have to settle before 1.0.0.

Constraints found in the code:

- `AnimationChannel` stores a `vec4` per key, which holds a translation, a scale or a quaternion but not the N weights of a morph channel.
- `skin.slang` is one thread per vertex over `Vertex` and `SkinWeights`, run once per skinned instance into a per-instance device-local buffer that every later pass pulls from ([ADR-0015](0015-bindless-vertex-buffers.md)). A morphed mesh without a skin has no such buffer today.
- Indirect draws take no count buffer ([ADR-0014](0014-indirect-draws-without-count.md)), since MoltenVK has no `drawIndirectCount`; the culling pass fills commands, and the batches record `drawIndexedIndirect` ([ADR-0012](0012-gpu-driven-rendering.md)).
- Events cross module boundaries as data recorded by one module and delivered by another ([ADR-0022](0022-gameplay-events-and-script-properties.md)); `scripting` links `world`, not the other way round.
- Reflection supports repeated members of reflected structs (`World::registerVector`), so a component can hold a list.

## Decision

**Morph targets run in the skinning pass.**

- A mesh created with morph targets keeps, per target, a dense delta per vertex (position, normal and tangent xyz, 36 bytes) in one device-local storage buffer, addressed like the skin weights. The importer reads glTF `targets` and the mesh's default `weights`, and keeps at most 8 targets per mesh, dropping the rest with a warning.
- `MorphWeights` is a component beside `MeshRenderer` holding the current weights; `loadModelPrefab` adds it to a node whose mesh has targets. A new `AnimationPath::Weights` channel animates it; `AnimationChannel` values become a flat `std::vector<float>` with a per-channel stride (3, 4 or the target count), and `sample` returns the interpolated span.
- `skin.slang` first adds the weighted deltas to the bind-pose vertex, then skins the result. The renderer runs the pass for a draw that is skinned, morphed or both, and the morphed-only case gets its per-instance buffer the same way a skinned one does. A draw item carries its weights as a range of `SceneView::morphWeights`. Normals are renormalised after the sum.

**Blending is by layer, and crossfade is a fade of layers.**

- `Animator` stays the primary layer and gains `fade` (seconds, default 0) and `layers`, a repeated member of `AnimationLayer{clip, time, speed, weight, loop, playing}`. Playback samples every layer and writes, per target, the weight-normalised blend: translations and scales by linear mix, rotations by normalised lerp along the shorter arc.
- Changing `Animator::clip` while `fade > 0` is a crossfade: the system moves the previous clip, at its current time, into a layer whose weight falls to zero over `fade` seconds and removes it; the primary layer's weight rises over the same time. A script or the inspector just assigns `clip`; no new Lua API. Layers a script or the author adds keep their weights until changed, so an upper-body clip over a walk is two layers. There are no masks or additive layers yet.
- Existing scenes load unchanged: new fields default, so the scene format stays at version 3.

**Animation events are data, recorded by `world` and delivered by `scripting`.**

- A clip carries `events`, a list of `{time, name, argument}`. They come from the glTF animation's `extras.events` and from the model's sidecar import settings (`animationEvents`, per clip name), so an author can add a footstep to a clip without editing the glTF file.
- `AnimationSystem::events()` returns the events crossed by each layer in the last `Update`, handling loops and wrap-around, ordered by entity then time. A crossfading-out layer still fires until its weight is zero; a layer under 0.05 weight does not.
- `scripting` delivers `onAnimationEvent(self, name, argument)` to the scripts of the animator's entity, before `update`. An entity destroyed since gets nothing.

**Root motion is removed from the pose and applied to the entity.**

- `Animator::rootMotion` (default false) and `rootBone` (a path; empty means the translation channel with the shortest target path). When set, the blended translation of that bone is replaced by its first-frame value, and the frame's difference, rotated by the entity's rotation, is added to the animator's entity `Transform`. Rotation root motion is not extracted. Physics sees an animated kinematic body move as it already does.

**Particles are simulated and drawn by the renderer from a component.**

- `ParticleEmitter` in `world` is the authored component: `maxParticles`, `rate`, a one-shot `burst`, `lifetime` and `speed` as min/max, a cone `angle`, `gravity`, `drag`, size and colour at birth and death, a sprite `texture` (identity), `blend` (`Alpha` or `Additive`), `space` (`Local` or `World`), `seed` and `playing`. `buildDrawList` hands the renderer one `ParticleEmitterItem` per enabled emitter: the parameters, the world matrix, a key naming the emitter across frames, the frame's `dt` and whether to simulate.
- The renderer keeps per emitter a device-local state buffer of `maxParticles` particles in a ring, an alive-index buffer and an indirect command, released a few frames after last use like skin instances. Each frame a compute pass `particles.slang` (a) writes the newly emitted particles into the ring from a hash of `(seed, serial)`, so the result does not depend on thread timing, (b) integrates every live particle, and (c) appends survivors' indices to the alive list with an atomic counter that becomes the command's `instanceCount`. The count of particles to emit is decided on the CPU from the rate and the fractional carry kept in the emitter's state. The draw is one `drawIndexedIndirect` of a quad whose instances read the alive list: no count buffer, so the same path runs on MoltenVK.
- Particles draw in the forward pass after the blended meshes, depth-tested and not depth-writing, as billboards facing the camera; emitters are ordered by distance, particles within one are not sorted. They are not in the shadow, depth or id passes and are not picked; the editor outlines the emitter's entity by a debug cone instead.
- The editor previews an emitter in edit mode while it is selected, with a restart button, and in play mode always. The preview simulates only on frames that ask for it, so a paused editor is still.
- The `Animator`'s sibling for the tests is a fixed `dt`: `renderer_tests` steps an emitter with a given `dt` and reads the state buffer back, so the count and positions at a step are exact.

**Formats.** The cooked mesh payload gains the morph buffer, the clip payload the stride-based channels and events, and the sidecar gains `animationEvents`; the bundle's version is raised and the older reading is dropped, since the formats are frozen only at M14.

## Consequences

- A character crossfades with one assignment from a script, and footsteps are a name and a time in the import settings.
- A morphed mesh costs one dispatch and one buffer per instance per frame, however many passes draw it; a mesh with 8 targets reads 8 more deltas per vertex in that dispatch only.
- Particle cost is one dispatch and one indirect draw per emitter, independent of the CPU, and bounded by `maxParticles`; 20 000 live particles do not appear in `statistics`' CPU time.
- `AnimationChannel::values` changes type, so `assets`, `world`, the bundle and the importer's tests change together, in one commit.
- Alpha-blended particles within one emitter can show ordering artefacts; additive emitters, the common case, cannot.
- Collisions, trails, mesh particles, sprite sheets, sorting inside an emitter and GPU culling of particles are later work.

## Alternatives considered

- **Morph targets in the vertex shaders**: no extra buffer, but a morphed variant of every pipeline, and the deltas read up to eight times a frame; ADR-0010 rejected the same for skinning.
- **Sparse morph deltas**: smaller for facial rigs, but a gather or a second index buffer in the skinning pass; dense deltas at 8 targets are simple and bounded, and sparse can follow behind the same weights.
- **A blend tree or state machine asset**: the right tool for a large character, but a new asset type, editor and format; layers with fades cover the stated crossfade and leave a state machine a script can build.
- **Animation events as Lua callbacks installed by the animation system**: `world` would call into `scripting`, reversing ADR-0009's dependency; data keeps the order deterministic.
- **Root motion by driving a physics character controller**: there is none yet; moving the `Transform` works for kinematic and plain entities now.
- **Particles on the CPU in `world`**: simple and easy to test, but 20 000 particles a frame is the work the engine moved to the GPU in M7, and the vertex data would have to be uploaded every frame.
- **A separate `particles` module**: the emitter is a component and the simulation lives with the renderer's other compute passes and buffers; a module would need the renderer's internals or duplicate them.
- **`drawIndexedIndirectCount` over the alive list**: avoids the instance-count trick but is missing on MoltenVK ([ADR-0014](0014-indirect-draws-without-count.md)).
