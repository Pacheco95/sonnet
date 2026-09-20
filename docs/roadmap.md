# Roadmap

Milestones are sequential and each ends with something runnable. Every feature listed in the README maps to one milestone here.

## M0: Foundation

Build scaffolding and the two lowest modules.

- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/` helpers, `.clang-format`, `.editorconfig`, `.gitignore`, CI workflow.
- `core`: handles with generations, logging, assertions, UUID, GLM configuration, Tracy zones.
- `platform`: `IWindow`, input events, SDL3 implementation with the callback-based main loop.
- `rhi`: instance and device creation with vk-bootstrap adopted into RAII, VMA, swapchain, per-frame resources, a triangle drawn through the render hardware interface, Slang compiled at build time.
- Tests: `core`, `platform` (headless), `rhi` on Lavapipe.

Landed as four commits, so CI is green from the first one:

1. Scaffolding: presets, manifest, format config, the CI workflow, and `core` with an empty test.
2. `platform`: the SDL3 window and the callback-based loop.
3. `rhi`: instance, device, swapchain and per-frame resources.
4. The triangle, with Slang compiled at build time.

Done when the editor executable opens a window, clears it and draws a triangle, all three desktop platforms build and test in CI, and the triangle test passes on Lavapipe. Windows and macOS runners have no Vulkan implementation, so the rhi tests skip there and the triangle is verified on Linux.

## M1: Editor shell

- `ui`: ImGui with SDL3 and Vulkan backends in dynamic-rendering mode, docking, multi-viewport on desktop.
- `renderer`: render graph with transient resources and barriers, an offscreen viewport target.
- `editor`: docking layout, viewport panel with fly camera (right mouse plus WASD, Q/E), log panel, frame statistics overlay (CPU and GPU times per pass, draw count, VMA budget).
- Primitives: box, sphere, plane, cylinder, capsule for testing without assets.

Landed as six commits, plus a fix:

1. Dear ImGui through an overlay port, so its SDL3 binding does not re-enable `sdl3`'s default features.
2. `platform`: the SDL window handle, raw events and relative mouse mode for the ImGui backends and the camera.
3. `rhi`: depth images, explicit barriers, indexed draws with buffer device addresses, push-descriptor buffers, the transient allocator, timestamps, the memory budget and the null device; then a fix for resources released between frames.
4. `renderer`: the render graph, primitives, camera, viewport target and the forward pass over `sonnet.slang`.
5. `ui`: the ImGui layer.
6. `editor`: the shell with viewport, log and statistics panels, and the application around it.
7. A fix: the ImGui overlay port stops linking vcpkg's Vulkan loader, which had no X11 surface support and was found ahead of the system's when the SDK environment was absent.

Done when the editor shows a lit primitive scene in a dockable viewport with live statistics. Two items named in the docs wait for a later milestone: runtime shader compilation for hot reload comes with asset hot reload in M3, and the log panel's `file:line` links need the preferences of M2's project handling.

## M2: World

- `world`: flecs integration, core components, hierarchy, reflection registration, scene serializer with versioning, prefabs.
- `editor`: hierarchy panel, inspector generated from reflection, translate/rotate/scale gizmos, picking, selection outline, undo/redo command stack, play/stop with snapshot restore, project open and create.
- `apps/samples/basic` project.

Landed as six commits, plus a fix:

1. flecs and nlohmann-json through vcpkg.
2. `rhi`: the `R32Uint` format for entity ids and a sampled image in the pass set, which the outline pass reads through.
3. `renderer`: ids on draw items, the id pass, the outline pass and the picker with its deferred readback.
4. `world`: the flecs world with the components registered by reflection, identities, hierarchy and world transforms, the phases and the play-mode switch, the scene and prefab format, and the draw list.
5. A fix: `platform` hands applications their arguments without the program name.
6. `editor`: hierarchy, inspector, selection, gizmos, picking, the outline, undo and redo, play mode, projects, preferences and the log panel's links.
7. `samples`: the basic project, copied next to the binaries by `SONNET_BUILD_SAMPLES`.

Done when a scene can be authored from primitives, saved, reopened, and played with a script-free rotating object driven by a system. The sample's spinning box is that object. Two refinements wait for later: native file dialogs instead of the path modal, and the fixed timestep, which arrives with physics in M4.

## M3: Assets and PBR

- `assets`: UUID sidecars, database, glTF import, image import, KTX2 cooking, hot reload.
- `renderer`: clustered forward pipeline with depth pre-pass, PBR materials, directional and punctual lights, cascaded shadow maps, image-based lighting, skybox, bloom, tone mapping, anti-aliasing.
- `editor`: asset browser, material editing, import settings in the inspector.

Landed as ten commits, after ADR-0008 was accepted:

1. `fastgltf`, `stb` and `ktx` through vcpkg, with an overlay port for `ktx` whose build applied a Clang-only flag to C sources.
2. `rhi`: the bindless set, samplers, mipmapped and cube images, HDR and block-compressed formats, uploads through a staging ring, compute pipelines, blend modes.
3. `renderer`: the clustered forward pipeline: shadow cascades, the depth pre-pass, light clustering in compute, PBR with the sun, the clustered lights and split-sum image-based lighting, the skybox, blended draws, bloom, ACES tone mapping and FXAA; meshes with tangents and submeshes, textures, materials and environments as renderer resources.
4. `core`: derived UUIDs and `writeFile`; `rhi`: the block-compression capability.
5. `assets`: the database with sidecars, image, HDR, KTX2 and glTF import, KTX2 cooking into the project's cache, material files and hot reload by polling; then a fix keying glTF sidecars on the file's content.
6. `world`: mesh renderers by asset identity with the scene format at version 2 and the migration from version 1, punctual lights and environments handed to the renderer, glTF models as prefabs.
7. `editor`: the asset browser, material editing and import settings in the inspector, asset pickers on identity fields.
8. `assets` and `renderer`: runtime shader compilation with the Slang library and pipeline rebuilding, so the editor reloads changed shader sources.
9. `samples`: the basic project gains a generated glTF crate, a checker texture, a gradient sky with a sun and a material file, its scene at version 2 with a model instance, a point light, a spot light and the environment; the version becomes 0.4.0.

Done: the sample renders with shadows and image-based lighting. The README's target was measured for the first time with `renderer_tests "[benchmark]"`, ten thousand draws and a hundred lights at 1080p on an RTX 4090: about 1.1 ms of GPU time per frame, of which the forward pass takes 0.3 ms, but about 95 ms of CPU time recording six passes of ten thousand draws each, which is why GPU-driven culling and indirect draws are next on the rendering side. Deferred to later milestones: BC5 for normal maps and ASTC for mobile, cooked mesh files and the cook tool (M6), the asynchronous form of asset loading (with the job system), transparency in the shadow and depth passes beyond alpha masking, and 2020-era mid-range hardware for the target itself.

## M4: Physics and scripting

- `physics`: `IPhysicsWorld` with Jolt: static, dynamic and kinematic bodies, box, sphere, capsule and mesh shapes, raycasts, debug draw.
- `scripting`: `IScriptRuntime` with Lua and sol2: entity and component access through reflection, input, logging, hot reload.
- `editor`: physics and script components in the inspector, physics running in play mode only.

Landed as thirteen commits, the first a fix found on the way and the second accepting ADR-0009:

1. A fix: the inspector compared scalar members' kinds with flecs' type entities of the same names, so every float, bool and integer member showed as unsupported.
2. ADR-0009: physics and scripting as world subsystems.
3. `joltphysics`, `lua` built as C++ and `sol2` through vcpkg.
4. `core`: log records at a given location, for script lines, and the script error category.
5. `platform`: the input state and the names of keys and mouse buttons.
6. `world`: the fixed timestep in a pipeline of its own with the interpolation fraction, component registration open to subsystems, world matrices from the hierarchy.
7. `rhi`: line-list pipelines, and the null device's count of live pipelines.
8. `renderer`: the debug line pass, depth-tested against the scene.
9. `assets`: script assets with revisions, CPU mesh data kept for collision, new scripts from the editor.
10. `physics`: the components, the Jolt world with static and kinematic bodies following their entities and dynamic ones interpolated back into them, compound, scaled and mesh shapes, raycasts, forces and collider outlines.
11. `scripting`: the Lua runtime with an instance per entity, components through reflection, the `world`, `input`, `physics`, `log` and maths API, errors at the script's line and hot reload that keeps the instances' state.
12. `editor`: physics and scripts in play mode, game input from the focused viewport, collider outlines, script assets in the browser and the inspector.
13. `samples`: the playground scene, with a ball rolled by the player, a sweeper, an elevator, a crate pyramid and a spawner, all scripted or simulated; the version becomes 0.5.0.

Done: the basic sample's playground plays in the editor with its scripts and physics and is back as it was on stop, which `editor_tests` checks headless on Lavapipe. Deferred: contact and trigger events for scripts, compound bodies from a hierarchy's colliders, per-instance script properties in the inspector and several scripts per entity, `require` between scripts, the scene camera in play mode (the editor draws through its own), and Jolt on the engine's job system once there is one.

## M5: Audio and animation

- `audio`: `IAudioDevice`, sources, listener, mixing with miniaudio.
- Skeletal animation: skins and clips from glTF, animation player component, GPU skinning.

Landed as nine commits, the first accepting ADR-0010:

1. ADR-0010: audio on miniaudio behind `IAudioDevice`, animation in `world`, skinning in a compute pass, and both driven by components so scripts reach them.
2. `miniaudio` through vcpkg.
3. `rhi`: the null device's count of live buffers.
4. `renderer`: skin weights on a mesh, joint ranges on a draw item, the skinning pass over `skin.slang` with a device-local buffer per instance, and the statistics for it.
5. `assets`: skins, animation clips and their sampling, sound files, flat normals for a primitive without them, the sidecar at version 2 with the new sub-assets.
6. `world`: `SkinnedMesh`, `Animator` and `SkinPose`, `AnimationSystem` with the playback and skin palette systems, lookup by path, model prefabs that carry the skin and the first clip, and joint matrices in the draw list.
7. `audio`: the components, the miniaudio device with its voices, spatialisation and listener, decoding with hot reload, and mixing without an output device for the tests.
8. `editor`: animation and sound in play mode, the editor camera as the fallback listener, sounds, skins and clips in the browser and the inspector, with a preview button, and the skinned count in the statistics.
9. `samples`: a skinned reed that sways and a beacon that turns and hums in the basic sample's start scene, a chime the playground's spawner rings, and the generator that writes them; the version becomes 0.6.0.

Done: the basic sample's start scene plays its skinned and node animations with a spatial hum in the editor, which `editor_tests` checks headless on Lavapipe, and the Khronos animated samples (CesiumMan, Fox, RiggedFigure, RiggedSimple, BoxAnimated, InterpolationTest) import and play. The player half of the criterion waits for M6, where `apps/player` arrives. Deferred: blending and crossfades between clips, morph targets, animation events, root motion, animation in edit mode, streaming long sounds instead of decoding them whole, sound cooking into the bundle, and several clips on one entity.

## M6: Player and desktop export

- `apps/player`: opens a project or a cooked bundle, no editor code.
- Cook tool: cooked asset bundle, binary scenes, manifest.
- Export dialog in the editor for Windows, Linux and macOS.

Done when an exported sample runs on a machine without the SDK or vcpkg installed.

## M7: Mobile export

- Android: NDK build of the player, ASTC texture cooking, Android 16+ device testing, packaging into an APK.
- iOS: Xcode build of the player from a macOS host, MoltenVK, packaging into an app bundle.
- Touch input mapping and the OS-owned loop through the SDL3 callbacks.

Done when the basic sample runs on an Android 16 device and an iOS device.

## Later

Job system and multi-threaded command recording, GPU-driven culling and indirect draws, temporal anti-aliasing, nested scene instances beyond prefabs, C++ game-code module hook, terrain, particles, game UI.
