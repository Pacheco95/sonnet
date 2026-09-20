# ADR-0010: Audio and skeletal animation

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

M5 adds sound and skeletal animation, the last runtime features before the player ships. The architecture names an `audio` module after `scripting` with miniaudio or SDL3 audio behind `IAudioDevice`, and says nothing about where animation lives. A glTF file already becomes a prefab whose node hierarchy is entities with `Transform`s ([world.md](../world.md#scenes)); its skins and clips have to drive those entities, and the renderer has to deform the skinned meshes in every pass that draws them: four shadow cascades, the depth pre-pass, the forward pass and the editor's id and mask passes. Prefab instances get fresh identities and flecs ids for their children, so nothing imported can refer to an instance's nodes by either.

## Decision

- **Animation is part of `world`, skinning part of `renderer`, clips and skins part of `assets`.** The glTF importer yields skins (joints and inverse bind matrices) and clips (translation, rotation and scale channels with linear, step or cubic-spline keys) as sub-assets of the model. `world` gains two components, `SkinnedMesh` next to a `MeshRenderer` and `Animator` on the entity a clip plays on, which `loadModelPrefab` adds to the model prefab, and `world::AnimationSystem`, constructed on a `World` and the asset database like the subsystems of ADR-0009. It registers two systems: playback, a simulation system in `Update` that advances each `Animator` and writes the sampled poses into its targets' local `Transform`s, and the skin palette, which runs in edit and play mode in `PreRender` after the transform system and turns the joints' world matrices into the skinned entity's joint matrices. `buildDrawList` hands those to the renderer with the draw items. There is no `animation` module: the components belong to the model prefab that `world` builds, and the pose is the hierarchy `world` already owns.
- **Bindings by name path.** Channels and joints name their node by the path of names from the model's root (`Armature/Hips/Spine`). A clip's paths resolve under the entity that carries the `Animator`; a skin's under the nearest ancestor of the skinned entity where all of them resolve. Resolved bindings are cached per entity and rebuilt when the clip or skin, or its revision, changes. Paths survive instantiation, scene files and re-import, and a clip plays on any hierarchy with the same names.
- **Skinning runs once per frame in a compute pass.** A skinned draw item carries its range of the view's joint matrices and a key naming its instance across frames. The first pass the renderer declares in a frame is preceded by a compute pass that skins every instance's vertices, with up to four joints each, into a device-local buffer the renderer keeps per instance and releases a few frames after its last use. Every pass then pulls the skinned vertices through the vertex address it already uses, so no vertex shader has a skinned variant.
- **Audio is miniaudio, in `audio` after `scripting`.** `audio::IAudioDevice`, created on a `World` and the asset database, registers `AudioSource` and `AudioListener` and a system that plays each enabled source's sound while its `playing` field is set, in play mode only, spatialised from its entity's world position relative to the first listener's, or the application's camera without one. miniaudio's engine mixes, resamples and spatialises on its own audio thread; the main thread only starts, stops and moves sounds. The asset database hands over the encoded file and `audio` decodes it: WAV, FLAC and MP3 with miniaudio's decoders, Ogg Vorbis with stb_vorbis. Without an output device, which is the case in CI and when none can be opened, the system mixes each frame's worth of audio on the main thread into a buffer the tests read.
- **Scripts reach both through components.** A script plays a sound by setting `AudioSource.playing` and switches or restarts an animation through `Animator`'s fields, by reflection like every component (ADR-0009); `scripting` gains no table for either.

## Consequences

- A skinned instance costs one dispatch and one buffer of its vertex count per frame, however many passes draw it, and the picking, outline, shadow and debug paths need no change.
- Animation writes ordinary `Transform`s, so play-mode snapshots undo it, physics sees animated kinematic bodies move, scripts read the pose, and the editor shows joints as entities in the hierarchy.
- A clip whose node names are not unique among siblings cannot be bound unambiguously; the importer warns about it and the first match wins.
- Morph targets, blending between clips, animation events and root motion are not part of M5. Sounds are decoded whole into memory on first use, which suits effects and short loops; streaming long music is later work.
- `audio` depends on `world` only; the editor, and the player from M6, create the device and feed the fallback listener. A machine without an audio device runs silent rather than failing.

## Alternatives considered

- **An `animation` module between `world` and `physics`**: keeps `world` smaller, but the model prefab that `world` builds needs the skin and animator components, which would then live in `world` anyway, leaving the module only a system.
- **Skinning in the vertex shaders**: no extra buffer, but a skinned variant of the depth, shadow, forward, id and mask pipelines, and the same vertices skinned up to eight times a frame.
- **Joints and targets by entity identity or flecs id**: instance children get fresh identities and ids, so every instantiation would have to rewrite the references, and re-importing a model would break them.
- **SDL3 audio**: gives device output and stream conversion, but mixing, attenuation, spatialisation and decoding would be engine code; miniaudio has all of them in one header, runs without a device for tests, and is already the manifest's M5 port.
- **`audio` before `scripting` with an `audio` table in Lua**: convenient for one-shot sounds, but ADR-0009 placed `audio` after `scripting`, and a component the script sets keeps sound state in the scene, where snapshots and the inspector see it.
