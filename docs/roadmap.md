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
- Primitives: box, sphere, plane, cylinder, capsule for testing without assets (cone, torus, ramp, stairs, hemisphere, arch and icosphere followed).

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

Landed as nine commits, the first accepting ADR-0011:

1. ADR-0011: cooked bundles, the `runtime` module and what export assembles.
2. `assets`: the project file moves down from `editor`, since the player opens projects too; creating one with its starter scene stays in the editor.
3. `assets`: `cookMesh`, which welds vertices and reorders each submesh for the vertex cache with tipsify, and `averageCacheMissRatio` to measure it.
4. `assets`: the bundle, one file with a CBOR index, and the payload encodings, binary for meshes, skins, clips, models and raw texture data, CBOR for the material and scene documents.
5. `assets`: `cook`, which walks the open database and writes a bundle, `AssetDatabase::openBundle`, which reads one back under the same API, and `sonnet_cook`, the command line over both.
6. `renderer`: the present pass, a full-screen copy into an image of the swapchain's format, since the player has no Dear ImGui to write one with.
7. A fix: `rhi`'s `waitIdle` submits uploads staged since the last frame, so tearing down after loading without drawing does not destroy resources a recording command buffer still names.
8. `runtime` and `apps/player`: `Game`, the world and its subsystems running a project folder or a bundle, and the executable around it.
9. `editor`: the export dialog, which cooks through the editor's own database and copies the player and the shaders next to the bundle.

Done: `sonnet_cook` cooks the basic sample into a 680 KB bundle with no warnings, and the player runs it from a directory holding only the binary, `shaders/` and `game.sbundle` — no project folder, no importers, no SDK. `runtime_tests` checks the same two paths headless on Lavapipe. Deferred: compressing the bundle's environment maps, which are stored as uncompressed RGBA16F because the KTX2 path cooks RGBA8; Lua bytecode instead of source; incremental cooking, which today redoes the whole project; native file dialogs for the export folder, with the other path modals; and cross-compiling a player from the editor, which stays CI's job.

## M7: GPU-driven rendering

M3 measured the engine against the README's target and found the gap is CPU-side: ten thousand draws cost about 1.1 ms of GPU time and about 95 ms of CPU time, because six passes each record ten thousand draw calls. Submitting those draws from the GPU is what closes it, and it comes before the job system because it removes the work rather than spreading it over threads.

Landed as three commits, the first accepting ADR-0012:

1. ADR-0012: culling into indirect draws, batched per pipeline and mesh. The open question was how an indirect draw reaches a mesh's index buffer, which it cannot rebind: batches won over a shared index arena, because the draw loop already sorts into exactly those runs.
2. `rhi`: the `Indirect` buffer usage, an `IndirectCommand` matching Vulkan's layout, the `DrawIndirect` stage and `IndirectCommandRead` access, and `drawIndexedIndirectCount`; three device features that the 1.4 baseline already guaranteed; and a Lavapipe test over the whole shape, a compute pass writing the commands and a draw reading them.
3. `renderer`: `cull.slang` and the culling pass, the batches each drawing pass submits, the object index moved into the command's `firstInstance` and the vertex address into the object array, with the scene shaders following. `world` and `editor` needed no change: the renderer derives world-space bounds from the mesh bounds it already holds, so every producer of a draw item gets culling for free.

Done: `renderer_tests "[benchmark]"` on an RTX 4090 in Release, ten thousand draws of two meshes and a hundred lights at 1080p, records 12 indirect calls where it recorded 60000 draw calls; the six scene passes fall from 2.69 ms of CPU to 0.28 ms, of which 0.26 ms is the per-frame object upload that stays, and GPU time falls from 1.24 ms to 0.64 ms because nothing culled the draws before. In Debug the recording falls from the 93 ms M3 measured to about 13 ms. The basic and playground samples render as they did, and picking, the outline and skinning still work, which `editor_tests` checks headless on Lavapipe.

Deferred: occlusion culling and a depth pyramid, meshlets, a shared index arena to reach one call per pass, sorting the blended draws on the GPU so they take the same path, reading the surviving counts back so the statistics report what drew rather than what was submitted, and tighter bounds for a skinned mesh, which is culled by its bind pose today. The per-frame object, material and light upload is now the larger part of a frame's CPU cost and is M8's to spread over threads.

## M8: Job system and asynchronous loading

More of the docs wait on the job system than on anything else: asset loading is synchronous, Jolt runs on a single-threaded job system of its own, and `world` never uses flecs' multi-threaded pipeline. It follows M7, which answered whether multi-threaded command recording is worth building: it is not, since recording a scene pass is now a handful of calls, but the per-frame object, material and light upload that M7 left behind is. [ADR-0013](decisions/0013-job-system.md) settles its shape first, and above all how flecs, Jolt and the engine's own parallel loops share one machine without starving each other.

- `core`: the job system — jobs with dependencies, a parallel for, main-thread affinity for what needs it, Tracy zones on the workers.
- `physics`: `JPH::JobSystem` implemented on it, replacing `JobSystemSingleThreaded` ([ADR-0009](decisions/0009-physics-and-scripting.md)).
- `assets`: the asynchronous request form, which returns at once with a placeholder until the job finishes ([assets.md](assets.md#database)).
- `world`: not threaded after all. `cascade` excludes `TransformSystem` and `immediate` excludes the animation, skin and script systems, which leaves flecs' pipeline nothing worth splitting; a parallel `TransformSystem` over `parallelFor` waits for the benchmark that asks for it ([ADR-0013](decisions/0013-job-system.md)).
- `renderer`: the per-frame object, material and light upload spread over workers, which M7 left as the larger part of a frame's CPU cost.
- A `linux-tsan` preset, since the sanitizer preset is address and undefined-behaviour only and nothing has run under a thread sanitizer.

Landed as five commits, the first accepting ADR-0013:

1. ADR-0013: one job system in `core`, and who runs on it. The question the milestone turned on was not what a job system looks like but who gets the cores. flecs offers "task threads" as the hook for an external job system, and taking it would have starved physics: `ecs_run_pipeline` creates and joins those tasks on every call, a task blocks on a condition variable for the whole run, and `World::progress` makes up to six of those calls a frame, so every worker would sit blocked through the `FixedUpdate` pipeline that `PhysicsStep` lives in.
2. `core`: the job system — dependencies, a parallel for, main-thread affinity, Tracy zones on the workers — and the `linux-tsan` preset that says it is correct, with Tracy off in it because its lock-free queue reports races of its own.
3. `physics`: `JoltJobSystem` over `JobSystemWithBarrier`, replacing `JobSystemSingleThreaded`; the module compiled without RTTI, as vcpkg builds Jolt and its target does not say.
4. `assets`: the request form, with the glTF and file-texture loaders split into a worker half that parses, decodes and cooks and a main-thread half that creates the renderer objects; `buildDrawList` requests rather than loads.
5. `renderer`: the per-frame object and cull-candidate fill spread over the pool.

Done, with two criteria short of what they said. Physics scales: `physics_tests "[benchmark]"` settles a pile of about a thousand boxes in 1.23 ms per step on fifteen workers against 2.87 ms on none, 2.33 times.

The suites pass under the thread sanitizer, but less of them than the criterion implies. `linux-tsan` runs with `VK_DRIVER_FILES` pointed at `/dev/null`, so every suite's GPU cases skip: Lavapipe rasterizes on a pool of its own, is no more instrumented than Jolt, and reports races and lock-order inversions from inside its own threads by the hundred, through stacks that enter at `VulkanDevice` and so cannot be suppressed without blinding the sanitizer to the engine's own Vulkan calls. What still runs under it is everything the job system touches — `core`, `physics`, `assets`, `world`, `scripting`, `audio` in full, and 29 of `renderer_tests`' 38 on the null device — and what does not is `rhi`, `ui` and `runtime`, which are almost entirely GPU cases and now run a handful of tests each. `ctest --preset linux-debug` on Lavapipe is what covers those, where the driver's threads are not the sanitizer's problem. Jolt was suppressed in `tools/tsan.supp` for the same uninstrumented reason; it is now built under the sanitizer instead ([Known gaps](#the-thread-sanitizer-cannot-see-two-libraries-the-engine-depends-on)).

The frame hitch is only half gone. Mesh and texture resolution no longer blocks a frame, but loading a scene still does: `loadModelPrefab` needs a model's node hierarchy before it can create the entities, so a prefab instantiation imports its glTF file in place. Letting a prefab instance exist before its model does is the change that would finish it.

Two things the measurements said that the plan did not. The renderer's per-frame fill was the larger part of a frame's CPU cost and was meant to be spread away; it goes from 0.26 ms to 0.19 ms with one extra worker and then stops, because writing ten thousand 160-byte entries into `MemoryUsage::CpuToGpu` memory is bandwidth to host-visible device memory, not computation. Writing less is what would move it. And `world` gained nothing: `cascade` excludes `TransformSystem` from flecs' multi-threaded pipeline and `immediate` excludes the animation, skin and script systems, so the pipeline stays single-threaded and a parallel `TransformSystem` over `parallelFor`, with a barrier between depth levels, waits for the benchmark that asks for it.

What M8 left behind, with what each would take, is in [Known gaps](#known-gaps): the asynchronous scene load, the bandwidth the per-frame fill is bound by, the two libraries the thread sanitizer cannot see, a placeholder to draw while a request is in flight, and a parallel transform hierarchy.

## Before M9

Five issues were queued before M9. [#27](https://github.com/Pacheco95/sonnet/issues/27), [#14](https://github.com/Pacheco95/sonnet/issues/14), [#11](https://github.com/Pacheco95/sonnet/issues/11) and [#13](https://github.com/Pacheco95/sonnet/issues/13) are implemented below. [#29](https://github.com/Pacheco95/sonnet/issues/29) is implemented and checked on Intel, Lavapipe, the RTX 2050, the RTX 4090 and an Apple M4 Max through MoltenVK. Each issue has its own branch and pull request. Scene tabs ([#12](https://github.com/Pacheco95/sonnet/issues/12)) landed after M10. Planar translate handles ([#15](https://github.com/Pacheco95/sonnet/issues/15)) and snapping ([#16](https://github.com/Pacheco95/sonnet/issues/16)) are features and wait until after M9.

### 1. Linux binaries load vcpkg's Vulkan loader ([#27](https://github.com/Pacheco95/sonnet/issues/27))

Closed by [ADR-0020](decisions/0020-linux-runtime-libraries.md). `assets` linked Slang's shared library, giving every binary above it a RUNPATH into `vcpkg_installed/x64-linux/lib`. SDL found vcpkg's loader there, built without window-system support, and could not open a window. The SDK's `LD_LIBRARY_PATH` hid this on the development machine. What closed it:

1. `ShaderCompiler` and its tests moved from `assets` to `editor`, following ADR-0018. Only `editor` imports and links the Slang library package; the build rules find `slangc` independently through vcpkg's tool path. The player and cook no longer need Slang on GCC or Clang.
2. `sonnet_copy_slang_runtime` copies Slang's compiler and loadable modules beside the Linux editor and `editor_tests`, with RUNPATH `$ORIGIN` replacing CMake's automatic path. The explicit-system-path alternative worked in a loading probe but required distro-specific path selection; a probe without a loader in its search directory confirmed the overlay alternative's lookup, but port inspection showed it would need both the manifest and the stub changed and would retain the broad RUNPATH. ADR-0020 records why local Slang copies were chosen.
3. `the editor keeps a Vulkan loader outside vcpkg mapped` in `editor_tests` checks the library after `Platform` shuts down. Before the fix it failed on `vcpkg_installed/x64-linux/lib/libvulkan.so.1.4.357`; after it, it resolves the system loader. CTest clears `LD_LIBRARY_PATH` and `SDL_VULKAN_LIBRARY`, so SDK overrides cannot hide the regression. The moved shader tests verify compilation and diagnostics with the copied libraries.
4. PR [#28](https://github.com/Pacheco95/sonnet/pull/28) is superseded by this fix; its Linux setup section is retained in [build.md](build.md#linux-setup). The manifest has no Vulkan loader window-system features. `/sonnet-setup` no longer suggests the `LD_LIBRARY_PATH` workaround.

Checked with GCC 14 and Clang 22. `readelf -d` shows `$ORIGIN` for the editor and `editor_tests`, no Slang dependency or RUNPATH on the player or cook, and no vcpkg RUNPATH on any GCC application or test binary. With the SDK variables cleared, the editor opened an X11 window under Xvfb on the RTX 4090 and wrote both sample screenshots; `LD_DEBUG=libs` showed Slang loaded beside the executable and `/lib/x86_64-linux-gnu/libvulkan.so.1` loaded from the system.

### 2. Shadow seam and dashed shadow edge in the basic sample ([#29](https://github.com/Pacheco95/sonnet/issues/29))

The reported ground seam and dashed cube-shadow edge both appeared in the `shadow-factor` term. The new `cascade` term colours the nominal view-depth slices red, green, blue and yellow, exposing the split boundaries in both the editor menu and `--shading-term cascade`.

Implemented in the renderer:

1. Statically unroll the four comparison-image sampling branches. The GPU regression initially found 768 incorrectly shadowed pixels on an unobstructed plane on Lavapipe, in rows at the splits. Bias and blending alone left a faint row; keeping the image index uniform within each branch removed it. A non-uniform descriptor-index annotation alone did not fix the dynamic path.
2. Overlap cascades over the last 10% of each slice and blend across that range; fade the last cascade to lit at `shadowDistance`. Sample only when the entire bilinear 3×3 footprint fits, otherwise try a farther cascade.
3. Offset the receiver along its geometric normal in world-space shadow texels, replacing the cascade-index bias multiplier with a constant depth bias. Pad the projection for the filter and offset, and fix texel snapping: an NDC position must snap by the map resolution, not by world-space texels per metre.
4. The GPU test compares every pixel of an unoccluded plane against shadows disabled, at 256 and 1024 shadow-map resolutions, and verifies that its camera spans all four cascade colours. It fails before the fix and passes on Intel ADL GT2, Lavapipe and the RTX 2050, with validation enabled. Restoring the original sampling shader also reproduces 768 bad pixels at each test resolution on Intel.

All 12 test suites pass on Lavapipe. Final-colour and shadow-factor captures of the basic and playground scenes on Intel ADL GT2, Lavapipe and the RTX 2050 show neither reported artifact.

On the RTX 4090 (NVIDIA 595.71.05), the shadow regression passes at both resolutions and all 48 `renderer_tests` cases pass with validation on. Final, shadow-factor and cascade captures of both scenes show neither artifact, and the shadow-factor image is continuous across the splits the cascade term shows. The seam did not appear on this GPU before the fix either, so the RTX 4090 confirms the fix regresses nothing there rather than that it removes the seam. Against `main` on the same GPU, the final images differ only along shadow edges, where shadows now meet their casters instead of floating clear of them. The shadow-factor term now darkens faces turned away from the sun, whose direct light is already zero. The jagged edge of the crate stack's shadow in the playground's farthest cascade is the same before and after.

On the Mac, through MoltenVK with validation on, all 12 suites pass in Debug, including the shadow regression at both resolutions, the runtime Slang compiler tests and the editor's shader reload, which is the path that compiles `forward.slang` at `-O0` ([report](https://github.com/Pacheco95/sonnet/blob/agents/mac-shadow-seams/docs/reports/mac-shadow-seams.md) on the `agents/mac-shadow-seams` branch). The GPU cases run only when the installed Vulkan loader is selected explicitly; without it they skip and the suites still report passing. Final, shadow-factor and cascade captures of both scenes show neither artifact, and neither did `main`, so as on the RTX 4090 this shows the fix regresses nothing there. Shadows meet their casters as they do on Linux. In Release, three alternating runs each of `renderer_tests "[benchmark]"` put the forward pass at 1.132 ms on `main` and 1.160 ms with the fix (+2.5%), and the frame's GPU time at 2.090 ms and 2.130 ms (+1.9%). The shadow passes and submission did not change beyond run-to-run noise. The extra forward time fits the second cascade sampled in each blend band. Both sides needed `-Wno-error=unused-parameter` and `-Wno-error=unused-variable` to build in Release with Apple Clang, because assertion-only variables become unused under `NDEBUG`.

### 3. The mouse leaks into the UI while flying the viewport camera ([#14](https://github.com/Pacheco95/sonnet/issues/14))

Right-drag in the viewport switches on relative mouse mode, but every SDL event still reached Dear ImGui. SDL kept reporting a moving cursor position in relative mode, so ImGui's cursor wandered over the other panels and hovered them. It was reproduced on Linux. Fixed in the editor: while the camera looks, mouse motion is kept from ImGui but still turns the camera, and button events still pass, so releasing the button ends the look. The editor saves the cursor position before relative mode and warps it back before disabling the mode, then gives ImGui that position once. An `editor_tests` case feeds a motion event while the camera is active and checks that ImGui's cursor did not move and the look delta did, then checks that the cursor returns to its starting position after release.

### 4. The export dialog does not close on OK ([#11](https://github.com/Pacheco95/sonnet/issues/11)) — closed

After a successful export, the dialog reports what was written and replaces OK and Cancel with a single Close button, which Enter and Escape also trigger. A failed export keeps both buttons and shows the error. Implemented in the editor.

### 5. Export the current scene only ([#13](https://github.com/Pacheco95/sonnet/issues/13))

Done: `CookOptions` accepts an optional scene. When it is given, the manifest starts there and it is the only scene in the bundle. Every prefab and asset is still cooked, because scripts reach them at run time. The export dialog offers "Current scene only" when a scene file is open and warns about unsaved scene changes, since the export reads it from disk. `sonnet_cook --scene` exposes the same choice. `assets_tests` cooks the playground alone and reads the bundle back; `editor_tests` checks the scene selected through export. The standalone player loads that playground bundle from its own directory and renders it on Lavapipe.

## M9: Mobile export

Mobile export was one milestone and is now two, because each platform is blocked on something different. Android needs the NDK build and a device on [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s 1.3 path. iOS needs a Mac, signing and a phone in hand. Either one would have held the whole milestone back. M9 is the work both platforms share, plus Android; iOS is [M10](#m10-ios-export). [ADR-0018](decisions/0018-mobile-export.md) decides how both are done and what counts as running, and its decisions hold across the split.

- Shared: ASTC texture cooking (`CookPlatform` gains `android` and `ios`), the ASTC formats and `astcSupported` in `rhi`, touch input mapping, and the OS-owned loop through the SDL3 callbacks.
- Android: NDK build of the player, Android 16+ device testing, packaging into an APK.

Done when the basic sample runs on an Android 16 device.

### Vulkan 1.3 devices

The Galaxy S25 Ultra's driver is Vulkan 1.3.284, which the selector rejected. [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) is implemented in `rhi`, before any of the Android work, since it can be checked on the desktop:

1. The selector takes devices at 1.3 or later with the 1.0 to 1.3 features, then checks the four 1.4 features per device: through `VkPhysicalDeviceVulkan14Features` at 1.4, through their extensions and feature structures at 1.3. The first device with all four is taken, in vk-bootstrap's order; if none has them, the error names what each device lacks.
2. A device's version is its own, bounded by the one the instance asked for. VMA takes that version, and `DeviceInfo` reports it with `vulkan14FeaturesAsExtensions`; the device log line says when the features came as extensions. Dear ImGui's backend takes the device's version rather than 1.4.
3. `DeviceDesc::apiVersionCap` lowers the version the instance asks for. CTest runs `rhi_tests` a second time as `rhi_tests_vulkan_1_3`, with every test device capped at 1.3.

Checked on Lavapipe and on the RTX 4090, each with and without the cap: every `rhi_tests` case passes with validation on, and the capped runs report a 1.3 device with the features as extensions. The RTX 4090 skips the three headless swapchain cases, as before. Enabling `VkPhysicalDeviceVulkan14Features` under the cap instead fails validation at `vkCreateDescriptorSetLayout`, so the capped run does catch a 1.4 structure used on the 1.3 path. The phone itself waits for the first APK.

### Checked before the code

ADR-0018 was accepted with open questions. Questions 1 (Android), 3, 6, 7 and 8 were answered in it before acceptance. The rest were answered afterwards by a run on the Mac (macOS 26.7, Xcode 26.3 with the iOS 26.2 SDK) and an iPhone 15 Pro Max (`iPhone16,2`, A17 Pro), reported on `agents/mac-m9-questions`. The ADR itself stays as accepted.

- **Every port builds for `arm64-ios` (question 1).** The manifest with ADR-0018's changes installed all 29 ports, with Xcode's Apple Clang. Homebrew LLVM 22 does not work: with `CC` and `CXX` pointing at it, `ktx` failed, because its libc++ rejects the iOS 12.0 minimum the port compiles for ("The selected platform is no longer supported by libc++"). The iOS presets therefore take Apple Clang, whatever the environment says.
- **The `vulkan` stub port installs for iOS (question 2).** It pulls in `vulkan-loader:arm64-ios`, which builds in about five seconds and is never linked. ADR-0018 makes the `vulkan-memory-allocator-hpp` overlay conditional on the install failing. It did not fail, so there is no overlay. The unused loader in the iOS install tree is the whole cost.
- **The iOS deployment target is 16.3 (question 3), confirmed against Xcode's own headers.** Compiling each library feature against iOS 15.0, 16.3, 17.0, 18.0 and 26.0 gives these minimums: `std::format` of a `double` and floating-point `to_chars` need 16.3. Floating-point `from_chars` needs 26.0, later than the table in `availability.h` suggests (LLVM 20, which it dates to iOS 19). Integer `from_chars`, atomic waits, `std::filesystem` and `std::expected` all work from 15.0. This is why the player parses `--play` without floating-point `from_chars`.
- **Static MoltenVK works on both Apple platforms (question 4).** `vkprobe` links Khronos's MoltenVK 1.4.2 static library (both archives' SHA-256 matched) with `-Wl,-u,_vkGetInstanceProcAddr`. The only frameworks it needs beyond SDL3's are Metal, Foundation, QuartzCore, IOSurface and CoreGraphics, plus IOKit and AppKit on macOS and UIKit on iOS. On macOS, `otool -L` lists no Vulkan or MoltenVK library. With every Vulkan SDK variable cleared, the probe finds `vkGetInstanceProcAddr` in the process, SDL hands back the same pointer, and `dladdr` and `dlopen(RTLD_NOLOAD)` resolve it to the executable, so `Platform`'s loader pinning is harmless. The instance is created without portability enumeration, which MoltenVK does not offer. The same holds on the iPhone.
- **The iPhone meets the baseline (question 5).** The A17 Pro GPU reports Vulkan 1.4.357 through MoltenVK 1.4.2, and every required feature and limit is present. ASTC LDR and BC are both supported, as is `hostImageCopy`, and ASTC 4×4 and 6×6 and BC7 sample with linear filtering. `drawIndirectCount` is absent, as on the Mac.
- **The iOS commands work as ADR-0018 wrote them (question 9, iOS half).** These all worked: building with `xcodebuild -allowProvisioningUpdates -allowProvisioningDeviceRegistration`, `devicectl device install app`, `devicectl device process launch --console <bundle id> <arguments>` (the arguments arrived), and `devicectl device copy from --domain-type appDataContainer`. SDL's pref path on iOS is `Library/Application Support/<organisation>/<application>/` inside the app's data container. The first launch of an app signed by a personal team fails until the developer profile is trusted on the phone, under Settings > General > VPN & Device Management. That is a one-time step for whoever holds the phone.

A follow-up run (`agents/mac-m9-followup`) closed what the first run left open:

- **The iPhone can use `D32_SFLOAT` for the shadow cascades as they are.** Read bit by bit from `VkFormatProperties3`, the A17 Pro's `D32_SFLOAT` is a depth attachment and can be sampled, compared and copied into, but not linearly filtered. `D16_UNORM` has every bit. The M4 Max has every bit for both. The cascades are compared through a linear comparison sampler, and the Vulkan spec requires linear filtering only of a sampler that does not compare (`VUID-vkCmdDraw-magFilter-04553`). A comparison needs `SAMPLED_IMAGE_DEPTH_COMPARISON` (`VUID-vkCmdDraw-None-06479`), which the iPhone has. The only read of a depth image in the shaders is `forward.slang`'s `SampleCmpLevelZero`, so nothing changes for iOS. The rule this leaves: a depth image is never sampled without comparison through a linear filter, or iOS breaks.
- **The iPhone runs iOS 27.0** (`osVersionNumber` from `devicectl device info details`). The earlier "19" was another field.
- **Signing:** `DEVELOPMENT_TEAM` has to be the team of the provisioning profile Xcode made for the bundle identifier. That can differ from the team of the signing certificate in the keychain, and passing the certificate's team failed with "No Account for Team".

The Android half of question 9 waits for the first APK. The Galaxy S25 Ultra's Vulkan report is in ADR-0018's open question 6 and led to [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md).

### The Android build

The first Android step is the build alone, as ADR-0018's "Builds" section decides it ([build.md](build.md#presets)):

- **Manifest:** `shader-slang` is declared as a host tool and again as a desktop-only library, and `vulkan-loader` and `imgui` are desktop only. There is no `vulkan-memory-allocator-hpp` overlay, because the iOS install did not need one (above).
- **`slangc` comes from the host triplet.** vcpkg's toolchain searches the host triplet's tools only when `VCPKG_HOST_TRIPLET` is set, and it never sets it, so the first configure found no `slangc` under `arm64-android` and took the Vulkan SDK's from the environment. On a machine without the SDK it would have failed. `cmake/SonnetShaders.cmake` now adds `vcpkg_installed/<host triplet>/tools/shader-slang` to `CMAKE_PROGRAM_PATH` in a cross build.
- **Presets:** `android-debug` and `android-release`, with build presets and no test presets. They configure only `core` to `runtime` and the player.
- **The player is `libsonnet_player.so`.** `sonnet_add_executable` makes a shared library on Android. It exports `SDL_main`, the four `SDL_App*` callbacks and SDL's JNI entry points (`JNI_OnLoad`, `Java_org_libsdl_app_*`), which is what SDL's Java side loads.
- **CI:** an Android job configures `android-release` and builds the player library, with NDK r30 rather than the runner's default r27.3 that ADR-0018 names. r27.3 builds every port but stops at API 35 (`android-36 is above the maximum supported version 35`), and so do r28 and r29: each release branch's `meta/platforms.json` ends at 35, and its build refuses a sysroot that goes further. The ADR's `android-36` stays, and the supported NDK floor in [build.md](build.md#toolchains) rises from r27 to r30.

Verified with NDK r30 (30.0.16248370, Clang 21) on Linux: the `arm64-android` install of the committed manifest has all 28 ports, the host `x64-linux` `shader-slang` among them. 26 were restored from the binary cache left by the ADR's own install run, and `joltphysics` and `ktx` were rebuilt. Both presets configure and build every target with no warnings and no change to engine code. Configuring with the Vulkan SDK's `PATH` and `CMAKE_PREFIX_PATH` in the environment, and again without them, picks the host triplet's `slangc` both times. The desktop is unchanged: the same ports for `x64-linux`, `slangc` from the same place, and all 13 suites pass on Lavapipe with GCC 14 and with Clang 22.

Still to do for the player on a phone, in ADR-0018's order: the APK (`cmake/SonnetAndroid.cmake`, SDL's Java sources, the manifest, debug signing), ASTC cooking, touch input, `Platform::openContent`, the swapchain's suspend and resume, and the capture in the player. None of them blocks the build.

### The Android APK

The second Android step packages the player, as ADR-0018's "Packaging" section decides it ([build.md](build.md#android), [player.md](player.md#running-on-android)):

- **`cmake/SonnetAndroid.cmake`** adapts SDL's `SdlAndroidFunctions.cmake` (zlib licence, attributed in the file) into `sonnet_add_apk`, with no Gradle: `aapt2` compiles and links the resources and manifest against `platforms/android-36`, `javac --release 17` compiles SDL's Java sources and the activity, `d8` dexes them, `zip` stores the stripped `libsonnet_player.so` and `assets/shaders/` uncompressed, `zipalign -P 16` puts the library on a 16 KB page, and `apksigner` signs with a debug keystore `keytool` generates once in the build directory. `sonnet_player_apk` is its own target; `sonnet_player_app` still builds alone. A cooked bundle is packaged as `assets/game.sbundle` only when `SONNET_ANDROID_BUNDLE` names one. The build does not cook.
- **The `sdl3` overlay port** installs SDL's Java sources into `share/sdl3/android-java/` for Android triplets, and turns off `SDL3.jar`, which SDL otherwise builds only when the environment happens to have a JDK and the Android SDK. Desktop triplets install the same files as before.
- **`apps/player/android/`** holds the manifest (package `io.github.pacheco95.sonnet`, API 36 minimum and target, `extractNativeLibs="false"`), `SonnetActivity` and a vector icon. `getLibraries()` returns `sonnet_player` alone. `getArguments()` splits the intent's `args` extra and logs the result under the `Sonnet` tag.
- **The manifest requires Vulkan 1.3, not 1.4.** ADR-0018 has `android.hardware.vulkan.version` `0x404000`, written before [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) accepted 1.3 devices with the four 1.4 features as extensions. The Galaxy S25 Ultra reports 1.3.284, so it could not have installed a 1.4 APK. The manifest asks for `0x403000` with `required="true"` and leaves the features to the device selector. The ADR's text stays as accepted.
- **CI:** the Android job builds the APK after the player library with the runner's JDK 17 (`JAVA_HOME` pinned to `JAVA_HOME_17_X64` and checked) and build-tools 36.1.0, verifies its signature, alignment and badging, and uploads it as `sonnet_player-android`.

Verified on Linux with NDK r30, build-tools 36.1.0 and JDK 25 (`--release 17`):

- Both `android-debug` and `android-release` build the APK: 34 MB and 14 MB, nearly all of it the library.
- `apksigner verify` passes with the v3 scheme, the only one a `minSdkVersion` of 36 needs.
- `aapt2 dump badging` shows the package, `minSdkVersion` and `targetSdkVersion` 36, `uses-feature: name='android.hardware.vulkan.version' version='4206592'` (`0x403000`), `native-code: 'arm64-v8a'` and `application-debuggable`. Both presets are debuggable, since RelWithDebInfo counts as debuggable, so `run-as` works on both.
- `zipalign -c -P 16 -v 4` passes, with the library at offset 49152, three 16 KB pages.
- `unzip -v` lists `lib/arm64-v8a/libsonnet_player.so`, every `assets/shaders/*.spv` and, when given, `assets/game.sbundle` as `Stored`.
- A configure without `ANDROID_HOME`, or with a build-tools version that is not installed, fails with a message naming it.
- The desktop is unaffected: `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) rebuilt `sdl3` at the new port-version, installed the same files, and build with no warnings, and all 13 suites pass on Lavapipe.

**An emulator can stand in for the phone for packaging, install, launch and arguments, not for rendering.** Probed with emulator 37.1.11 and the `android-36;google_apis_ps16k;x86_64` image (Android 16 with 16 KB pages) under KVM:

- `-gpu host` crashed the emulator (SIGSEGV) a minute into boot, twice, once with the Vulkan SDK's environment cleared. Before crashing it took the RTX 4090 and logged `guestVulkanMaxApiVersion: 1.3.0`, so host mode would offer the guest 1.3 at most.
- `-gpu swiftshader_indirect` boots in 24 s. `pm list features` has `android.hardware.vulkan.version=4206592` (1.3) and `vulkan.level=1`. `cmd gpu vkjson` reports an instance at 1.4.0 and one device, "SwiftShader Device (Subzero)", at **1.3.0**, with ASTC LDR but **none** of `VK_KHR_push_descriptor`, `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_maintenance5` and `VK_KHR_maintenance6`. ADR-0019's selector rejects it.
- The arm64-only APK installs, from both presets, through ARM translation. The image's `abilist` is `x86_64,arm64-v8a`, and Berberis does the translating. The library loads in place from `base.apk!/lib/arm64-v8a`, which confirms it is stored and page-aligned. `am start` with `--es args` launches it, and the activity logs the arguments split as intended (`[my game.sbundle, --flag]`). SDL runs `SDL_main`, which returns after about 0.6 s. Berberis logs the Vulkan entry points it has no wrapper for (`vkGetPhysicalDeviceDescriptorSizeEXT` and others). The reason for the exit is not visible, since the engine's log does not reach logcat yet, and routing stdout through `wrap.<package>` disables the native bridge. The selector rejecting SwiftShader is the likely cause. `run-as` works.
- One APK signed in one build directory cannot update an install from the other (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`), since each has its own debug key. `adb uninstall` first.

**On the Galaxy S25 Ultra** (SM-S938B, Android 16, API 36, 4 KB pages), which answers the Android half of ADR-0018's question 9:

- `pm list features` reports `android.hardware.vulkan.version=4206592`, exactly `0x403000`. The manifest's 1.3 requirement matches it, and ADR-0018's `0x404000` would have refused the install.
- The `android-release` APK installs with `adb install --user 0` (the phone has a second user profile, which plain `pm` commands trip over). The library loads in place from `base.apk!/lib/arm64-v8a`.
- `am start` with `--es args` passes the arguments (`[my game.sbundle, --flag]`). `run-as io.github.pacheco95.sonnet` reaches the app's data directory.
- With spdlog's Android sink added to a local build for this one run (not committed), the engine's log shows how far the player gets. The Adreno 830 is taken as a "Vulkan 1.3.284 device ... with the 1.4 features as extensions", so ADR-0019's path works on the device it was written for. The swapchain is created (1080×2340, 5 images, `R8G8B8A8Unorm`, Mailbox), and the job system starts with 7 workers. Startup then fails with `cannot open ./shaders/cluster.spv`, since the shaders are in the APK and nothing reads them from there yet. `Platform::openContent` is therefore the next blocker, and the logcat sink, which that diagnosis needed, should come with it.

Still to do before the basic sample runs on the phone: ASTC cooking and `CookPlatform::android`, `Platform::openContent` (the player cannot read the APK's assets without it), spdlog's logcat sink, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player. After those, the CI job cooks the sample into the APK.

### Reading content from the APK

The third Android step reads the content the APK stores, as ADR-0018's "Packaging" section decides, and puts the engine's log in logcat ([platform.md](platform.md#paths)):

- **The logcat sink.** On Android the entry point adds spdlog's `android_sink_mt` through `core::Log::addSink` before the first line is logged, under the tag `Sonnet`, the one `SonnetActivity` logs its arguments under. The console sink stays, `core` stays platform-agnostic, and `platform` links `liblog` itself on Android. It already reached the link through spdlog's and SDL's interface libraries.
- **`Platform::openContent`** returns a `ContentStream`, a seekable, read-only stream over `SDL_IOStream` with `read`, `readExactly`, `seek`, `tell`, `size` and `readAll`. `Content.h` forward-declares `SDL_IOStream`, so SDL stays out of the header. A relative path resolves against the content root, which is the APK's `assets/` on Android and `basePath()` elsewhere. An absolute path is an ordinary file everywhere. On Android, SDL looks a relative path up in the app's internal storage before the assets. `openContent` is static, since it needs nothing SDL initialises.
- **The readers.** `Bundle` holds a `ContentStream` instead of an `std::ifstream`, and checks a payload's span against the stream's size before allocating. `Bundle::open` and `AssetDatabase::openBundle` still take a path, because `openContent` is static and every desktop caller passes an absolute path. The renderer reads its shaders through `openContent`. `Game` passes the relative `shaders` and so no longer takes the `Platform`, while the editor, the cook and the tests pass the absolute `basePath() / "shaders"` as before. With no argument the player opens `game.sbundle` from the content root. It makes an argument absolute first, so on desktop an argument is still a path from the working directory. Import, cook, `Project` and the editor's files stay on `core::readFile`.
- **No new dependency.** `assets` and `renderer` reach `platform` through `rhi`, which links it publicly.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings. All 13 suites pass on Lavapipe, including the five new `openContent` cases in `platform_tests`, and `runtime_tests` loads the basic sample as a folder and as a bundle.
- The desktop player runs the basic sample from a project folder, from a relative bundle argument given in another working directory, and from `game.sbundle` beside a copied binary started from `/`. An export the editor wrote (`Editor::exportProject` with the real player: 27 assets, 22 support files) runs the same way.
- `android-debug` and `android-release` build with no warnings. `libsonnet_player.so` lists `liblog.so` as `NEEDED`.

**On the emulator** (`sonnet36`, `-gpu swiftshader_indirect`), since the phone was not connected:

- `adb logcat -s Sonnet` shows the engine's log from its first line (`Sonnet 0.10.0`), with spdlog's levels as logcat's (`I`, `D`, `W`, `F`). The log reaches the device selector, which rejects SwiftShader: `Missing feature VkPhysicalDeviceVulkan11Features::shaderDrawParameters`. That is a 1.1 feature, checked before the four 1.4 extensions the APK step expected to be the reason. The player then exits with a failure.
- The device is rejected before anything reads the shaders or the bundle. A probe added to a local build for this one run (not committed) showed that the reads work from the APK. It read `shaders/cluster.spv` through `openContent`, 41812 bytes, the size `unzip -v` lists. It also opened the packaged `assets/game.sbundle` (the basic sample, cooked by the Linux `sonnet_cook`) with its 27 assets, and read its start scene.
- A bundle pushed to `/data/local/tmp` and copied with `run-as ... cp` into `files/` opens when `--es args` gives its absolute path. A missing absolute path is an `Io` error naming it.

**On the Galaxy S25 Ultra** (Android 16, the `android-debug` APK with the basic sample cooked by the Linux `sonnet_cook` and packaged as `assets/game.sbundle`):

- `adb logcat -s Sonnet` shows the engine's log from its first line. The Adreno 830 is taken on ADR-0019's path ("Vulkan 1.3.284 device ... with the 1.4 features as extensions"), the swapchain is created (1080×2340, 5 images, `R8G8B8A8Unorm`, Mailbox), and the job system starts with 7 workers.
- The shaders now load from the APK. The earlier `cannot open ./shaders/cluster.spv` is gone, and startup gets one step further, into `Renderer::createPipelines`.
- **There the process dies with a SIGSEGV**, a null-pointer read inside the driver's shader compiler (`/vendor/lib64/libllvm-qgl.so`), called from `vkCreateComputePipelines` through `VulkanDevice::createComputePipeline`. Nothing is logged first, since the crash is in the driver. `cluster.spv` is the first module the renderer builds, and its one pipeline is the compute pipeline `light clustering`, so that pipeline is the likely one. The crash happens before the bundle is opened, so the basic sample does not reach the screen. This step does not investigate it, as scoped. It is the next blocker on the phone.
- A bundle pushed to `/data/local/tmp` and copied with `run-as ... cp` into `files/` gets its absolute path through `--es args` (the log shows `arguments: [/data/user/0/io.github.pacheco95.sonnet/files/pushed.sbundle]`). The run then stops at the same crash, since `Game` builds the renderer before it opens the bundle. The emulator run above showed that the bundle opens by absolute path.

Still to do before the basic sample runs on the phone: the Adreno compiler crash in `createComputePipeline` ([the next step](#the-adreno-shader-compiler)), then ASTC cooking and `CookPlatform::android`, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

### The Adreno shader compiler

The fourth Android step finds and works around the crash above.

**Finding the pipeline.** `Renderer::createPipelines` now logs each module and each pipeline at debug level before creating it, so the last line before a driver crash names the pipeline. On the phone it is `pipeline "light clustering" from cluster`. On Android, SDL reads a relative path from the app's internal storage before the APK, so a module copied into `files/shaders/` with `run-as` replaces the packaged one without a rebuild. That made every experiment below a copy and a restart:

- A compute module that does not read `frame` builds, and the next pipeline, `cull`, crashes at the same address. So the crash is not about clustering. It is about how `FrameConstants` is declared.
- **Debug information is ruled out.** The module built with `-O2` and no debug information (what Release ships) crashes the same way, as do `-g0` and `-g1`.
- **Validity is ruled out.** `spirv-val --target-env vulkan1.3 --scalar-block-layout` accepts every module. They declare SPIR-V 1.6, which the device takes as a 1.3 device, and they use only the `Shader` and `PhysicalStorageBufferAddresses` capabilities, both of which the device has. The engine requires `bufferDeviceAddress` and `scalarBlockLayout`.
- **A minimal repro.** A uniform block with a pointer to a `uint`, a scalar array or an array of matrices builds, but a block with a pointer to a struct crashes. Slang emits `OpTypeForwardPointer` for every pointer to a struct it has not emitted yet, and defines the pointer after the block that uses it. The same module, with the pointer and its struct defined before the block and nothing else changed, builds. So does one that keeps the `OpTypeForwardPointer` but defines everything in order. What the driver cannot take is a struct member whose pointer type is only declared forward, not the instruction itself.

With the types reordered, `light clustering` fails differently: `vkCreateComputePipelines` returns `VK_ERROR_UNKNOWN`. Reducing again:

| Read through `Light *lights` | Adreno 830 |
|---|---|
| `lights[i].position` (the first member) | builds |
| `lights[0].range`, a whole `lights[1]` | builds |
| `lights[i].range`, `lights[i].color`, a whole `lights[i]`, `clusters[i].lights[0]` | `VK_ERROR_UNKNOWN` |
| `lights[i].range` as one `OpPtrAccessChain lights i 1` | builds |

A pointer to a struct computed with a dynamic index cannot be read past its first member or loaded whole. Slang writes `lights[i].range` as an `OpPtrAccessChain` to the element followed by an `OpAccessChain` to the member. Decorating the struct `Block`, as glslang does for a `buffer_reference`, doesn't help. Neither does glslang's shape, a block with a runtime array and one chain through it, and Slang lowers an unsized array behind a pointer to an `OpPtrAccessChain` anyway. No Slang option or source form avoids either shape, and the `shader-slang` port installs prebuilt binaries, so Slang cannot be patched through an overlay port either.

**The fix** is `tools/spirv_for_adreno.py`, which the Android build runs over every module after `slangc` ([rendering.md](rendering.md#shaders)). It folds chained access chains on a buffer pointer into one chain from the root pointer, splits a whole struct or array loaded through one into a load per member with an `OpCompositeConstruct`, removes the chains left unused, and sorts the types so no pointer is used before it is defined. It rewrites 7 of the 11 engine modules, and the output passes `spirv-val`. The desktop is unchanged: its modules are not rewritten, and the editor's screenshots of the basic sample's main scene and of the playground after `--play 3` are byte-identical with the rewritten modules in place of the originals. The editor was checked to read those files by giving it a truncated one. The rewrite needs no C++ and no device check. Its test, `renderer_spirv_for_adreno`, runs it over the engine's modules on every desktop build and checks that neither shape is left, that a second run changes nothing, and that `spirv-val` accepts the result when the SDK is installed.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked by the Linux `sonnet_cook` packaged:

- All 29 pipelines from the 11 modules build. The bundle opens (27 assets), the game plays `Basic` with 15 entities, the render graph allocates its targets at 1080×2340, and **the basic sample renders**: lit and shadowed, the sky from the environment, bloom and tonemapping, with the crate turning. Nothing is logged at warning level or above, apart from the missing validation layer, which is expected.
- A bundle pushed to `/data/local/tmp`, copied into `files/` and given through `--es args` as `/data/user/0/io.github.pacheco95.sonnet/files/game.sbundle` opens and plays the same way.
- `android-release`, with the same bundle, also plays.

Still to do before the phone runs a game as the desktop does: ASTC cooking and `CookPlatform::android`, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

### ASTC texture cooking

The fifth Android step cooks a phone's textures as ASTC, as ADR-0018's "Textures" section decides ([assets.md](assets.md#textures)):

- **`rhi`** has `ASTC4x4Unorm`, `ASTC4x4Srgb`, `ASTC6x6Unorm` and `ASTC6x6Srgb`, and `DeviceInfo::astcSupported`, enabled where `textureCompressionASTC_LDR` is present, apart from BC, since vk-bootstrap enables a feature structure only when all of it is there. `formatSupported` says whether a device samples a format, and `createImage` asserts it. The device's log line lists `BC` and `ASTC`, and the renderer's debug line for each texture names its format ([rendering.md](rendering.md#the-rhi-module-today)).
- **Loading.** `readKtx2` takes the device's `DeviceInfo`. It transcodes UASTC to BC7 with block compression, to ASTC 4×4 with ASTC and no BC, and to RGBA8 with neither, uploads an ASTC file as it is, and refuses a format the device cannot sample, which falls back as a failed import does.
- **The cook.** `CookPlatform` has `android` and `ios`, taken by `sonnet_cook --platform` and the export dialog. For them the cook asks `AssetDatabase::mobileTexture`, which encodes a compressed texture's RGBA8 levels, decoded from the source, with `ktxTexture2_CompressAstcEx`: 6×6 perceptual for sRGB, 4×4 for linear, medium quality, no normal-map mode. It caches the result as `<uuid>.astc.ktx2` under the UASTC entry's freshness rule. Uncompressed textures stay RGBA8 and the environment RGBA16F. Android and iOS cook the same bytes.
- **`Game::open`** refuses a bundle whose platform's textures the device cannot sample, before anything in it loads, and names the platform. A desktop bundle runs everywhere and a mobile one needs `astcSupported` (`assets::canRun`, [player.md](player.md#opening-a-game)).
- **Export.** Exporting for Android or iOS writes the bundle alone, and the dialog says the editor builds no APK or app bundle ([editor.md](editor.md#export)).
- **CI.** The Android job builds `sonnet_cook` for the host, cooks the basic sample for `android` and packages it into the APK ([build.md](build.md#continuous-integration)).

**Tests.** `assets_tests` encodes FlightHelmet's glass-and-plastic base colour (sRGB, 6×6) and normal map (linear, 4×4), downscaled to 512×512, decodes them back with `ktxTexture2_DecodeAstc` and holds each above a PSNR floor. At 2048×2048 the base colour measures 48.7 dB, ADR-0018's figure. At 512×512 the two measure 45.4 dB and 48.8 dB, the same with GCC 14 and Clang 22, and the floors are 44.4 dB and 47.7 dB. The test uses FlightHelmet (CC0) rather than DamagedHelmet, whose textures come from an original under CC BY-NC 4.0 as well as the CC BY 4.0 that ADR-0018 names. The ADR measured this base colour too. A mobile cook of the basic sample has ASTC texture payloads (read from the KTX2 header, not transcoded) and names `android`. A second mobile cook rewrites no cache entry and gives iOS the same bytes, and a newer source is encoded again. The test that proved the second cook read the cache found a bug on the way: the freshness check's default sidecar time was the filesystem clock's epoch, which libstdc++ puts in 2174, so a glTF image was never fresh. `runtime_tests` has the null device, which reports BC and no ASTC as a desktop GPU does, refuse an Android and an iOS bundle and open the Linux one. `rhi_tests` uploads a solid-colour image of 13×7 texels with every mip level and samples its first and last levels, in BC7 and in ASTC 4×4 and 6×6. The ASTC cases skip without `astcSupported`. The RTX 4090, RADV and Lavapipe all report `textureCompressionASTC_LDR = false`, as ADR-0018 says, so neither this machine nor CI runs them. They run on Apple silicon and phones.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The desktop is unchanged. A Linux cook of the basic sample is byte-identical to the one packaged in the previous step. The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.
- The basic sample's Android bundle is 702 367 bytes against the Linux one's 682 714. Its three textures are small, so the first cook, which encodes them, takes 0.38 s and a second one 0.32 s.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` by the Linux `sonnet_cook`:

- The device's line ends `BC, ASTC`: **the phone has BC as well as ASTC**, as ADR-0018's question 6 recorded. So a Linux bundle's UASTC was transcoded to BC7 on it, not to RGBA8, both before this step and after it. The ASTC 4×4 target applies to a device with ASTC and no BC.
- The bundle opens ("cooked for android"), and the textures upload as ASTC: the checker and the crate's base colour as `ASTC6x6Srgb`, the crate's normal map as `ASTC4x4Unorm`. The sample plays with 15 entities, and nothing is logged at warning level or above but the missing validation layer.
- `adb exec-out screencap -p` shows the sample drawn as with the Linux bundle, compared by eye: the same lighting, shadows, sky and checker. Only the crate's turn and the reed's sway differ, with the moment of the capture.
- The Linux bundle, copied into `files/` to override the packaged one, uploads its textures as `BC7Srgb` and `BC7Unorm`.
- A build of this step with `blockCompressionSupported` forced off, for one run and not committed, transcoded the same Linux bundle to `ASTC4x4Srgb` and `ASTC4x4Unorm` and drew the same frame. That is the path a phone without BC takes.

A mobile bundle's ASTC is stored without supercompression, while the desktop's UASTC has zstd, so a texture can take more of the APK than it does of a desktop bundle. The crate's flat 128×128 normal map is 22 KB of ASTC 4×4 against 0.8 KB of UASTC. On the GPU a colour texture takes less than half: 3.56 bits per texel against BC7's 8. zstd over ASTC is a possible later change, which `readKtx2` would read as it is.

Still to do before the phone runs a game as the desktop does: touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

### Touch input

The sixth Android step gives the game the fingers, as ADR-0018's "Where it lives" decides for `platform` and `scripting`:

- **`platform`** translates SDL's finger events into `TouchDown`, `TouchMotion` and `TouchUp`, with SDL's finger id, a cancelled finger ending as a lifted one. Positions are in window coordinates, SDL's fraction of the window times its size, which is where SDL puts the mouse it synthesises from the same finger ([platform.md](platform.md#events)). `InputState` keeps up to ten fingers in the order they went down, each with this frame's motion, which `beginFrame` zeroes like the mouse's.
- **Touches and the mouse.** The synthesised mouse stays on, so the first finger is still the left button. A finger reaches `InputState` twice, as a touch and as the mouse, and each report stays whole: SDL moves the mouse for the first finger only, the move to where a finger lands keeps its position with a zero delta rather than counting as motion from where the last finger lifted, and the touches SDL makes from a mouse (on by default on Android and iOS) or a pen are dropped, since those arrive as the mouse already.
- **`scripting`.** `input.touches()` returns `{id, position, delta}` for each finger, with the mouse's coordinates and table shape. Lua had no way to turn a screen point into a world ray, so `camera.ray(point)` returns `{origin, direction}` through a point of the view, for `physics.raycast` ([scripting.md](scripting.md#camera)). The application hands the runtime the camera it draws through and the view's size every frame: the player the scene camera over its window, the editor its viewport camera over the image. The unprojection is the editor gizmo's, moved into `renderer::rayDirection` so both use it.
- **The editor** hands a touchscreen's fingers to the game as it does the mouse, relative to the viewport image, and only a finger that lands on the image ([editor.md](editor.md#play-mode)). Dear ImGui and the viewport see only the synthesised mouse.
- **`player.lua`** rolls the ball towards the point on the ground under the first finger held, with the same force as a key, alongside W, A, S, D and Space.

**Tests.** `platform_tests` translates synthetic finger events (two fingers down at once, motion, up and cancel) in a headless window of known size, drops a mouse's and a pen's touches, and zeroes a finger's landing motion. `InputState` touches appear, move with a motion that resets each frame and end, and end with the focus. `scripting_tests` reads `input.touches()` with its fields and types and `camera.ray` through a known view, and `renderer_tests` checks the ray through the centre and a corner of a known camera and where a pitched camera's centre meets the ground.

Verified on Linux: `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe. The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` with `--scene scenes/playground.scene.json`, since the player takes no `--scene` yet:

- The player draws the playground in portrait at 1080×2340. The window's creation logs the 1280×720 it asked for, and Android resizes it to the surface: from then on its size and pixel size are both 1080×2340, so positions are pixels on the phone.
- A finger held with `adb shell input motionevent DOWN 300 1100`, moved a pixel at a time for about three seconds and lifted, arrived as one touch at (300.3, 1100.5) with the synthesised mouse at the same point and the left button down. A debug line in `player.lua`, for the run only, showed the ray from the scene camera meeting the ground at (−2.42, 0, −2.42) and the ball rolling from (0, 0.5, 5) to (−2.43, 0.5, −0.02) while the finger was held. `adb exec-out screencap -p` frames before, during and after show the ball leaving the bottom of the screen and reaching the finger.
- An `adb shell input swipe` over three seconds moved the ground point from (−2.4, −2.4) to (1.4, 4.6), and the ball followed it.
- Nothing was logged at warning level or above but the missing validation layer, and there was no native crash.

ADR-0018's check 5 needs a person's hand on the device. The `adb` run above was the agent's. Michael then confirmed the check by hand on the Galaxy S25 Ultra: a finger held on the playground rolls the ball towards it. ADR-0018 says "window pixels" for a touch's position. The engine uses window coordinates, the mouse's, which are pixels on Android and logical coordinates on a high-density desktop display.

Still to do before the phone runs a game as the desktop does: the swapchain's suspend and resume with the lifecycle, and the capture in the player.

### The mobile lifecycle

The seventh Android step lets the player go to the background and come back, as ADR-0018's "Where it lives" decides for `platform`, `rhi` and the player:

- **`platform`** translates SDL's lifecycle events into `WillEnterBackground`, `DidEnterForeground`, `LowMemory` and `Terminating`. `DidEnterBackground` and `WillEnterForeground` get no type, since on Android each comes straight after its partner and on iOS nothing is left to do at either ([platform.md](platform.md#background-and-foreground)).
- **`rhi`.** `ISwapchain::suspend` waits for the device to go idle and releases the images, the swapchain and the surface. `resume` creates the surface and the swapchain again from the window and returns an `Error` rather than throwing into the event. While suspended `acquire` returns nothing, as for a minimised window. A surface lost before the suspend, which Android can do (below), is caught in `acquire` and present: one warning, no image, and the `suspend` and `resume` that follow recover it. Both transitions log at `info`, the resume with its extent. The null device's swapchain has the same calls ([rendering.md](rendering.md#suspend-and-resume)).
- **`audio`.** `IAudioDevice::pause` and `resume` stop and start miniaudio's device, so nothing is heard in the background and every sound carries on where it was. Without an output device nothing is mixed while paused ([audio.md](audio.md#pausing)).
- **The player** waits for idle, suspends the swapchain and pauses the audio on `WillEnterBackground`, and resumes both on `DidEnterForeground`, where it also restarts its frame clock. `LowMemory` and `Terminating` are logged. The editor, which runs only on the desktop, is unchanged ([player.md](player.md#the-lifecycle)).

**What SDL's source and the phone said**, where ADR-0018 assumed:

- **The surface goes before the event.** ADR-0018 says the events arrive "before the OS takes the surface away". On Android they do not: SDL's `surfaceDestroyed` queues the pause for the native thread and releases the `ANativeWindow` in the same call, on the UI thread, without waiting for a Vulkan window. In the two trips logged with SDL's own lines, `surfaceDestroyed()` came 3 and 6 ms before the engine heard `WillEnterBackground`. Once in ten trips, on the screen-off, a frame fell in between, and `vkAcquireNextImageKHR` returned `VK_ERROR_SURFACE_LOST_KHR`. The swapchain logged the warning, drew nothing that frame and recovered through the suspend and resume. Before this step the recreate path in `acquire` would have thrown on a lost surface.
- **Nothing iterates in the background.** With `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` at its default, SDL's event pump blocks until the resume, so `SDL_AppIterate` is not called. The player counted no frame in any of the ten background periods.
- **The window keeps its size.** 1080×2340 before and after, with no `WindowResized`, in portrait.
- **`LowMemory` comes every time.** SDL maps every `onTrimMemory` to it, and Android trims a hidden application's UI.
- **`dt`.** The frame clock clamps to 0.1 s and the world to four fixed steps a frame, so without the restart the first frame back would have simulated 1/15 s of the time away. With it, that frame simulates its own time.

**Tests.** `platform_tests` translates the four events and drops the other two. `rhi_tests` suspends a Lavapipe headless swapchain after three frames, acquires nothing while it is suspended, even after a resize request, resumes it at 320×200 and draws three more frames, and checks that a second `suspend` and a `resume` without a suspend do nothing more, all with validation silent. Both cases skip where the other swapchain cases skip, which includes the RTX 4090. The null device's swapchain gets the same checks. `audio_tests` pauses a quarter-second sound a tenth of a second in, mixes nothing for a second, and hears its remaining 0.15 s after the resume. The player's handling is in `apps/player/main.cpp`, which no test reaches, so `runtime_tests` plays the basic sample through the same steps on Lavapipe instead: frames that simulate while acquiring nothing and mixing nothing, then draw into the present pass again, with nothing warned about.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.
- The desktop player minimised and restored twice through the window manager (GNOME on X11) behaves as `main`'s does: the same swapchain lines, nothing warned about, the scene drawn after each restore and a clean exit. No lifecycle event arrives there.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` with `--scene scenes/playground.scene.json`:

- **Ten trips to the background**, sent by `adb`: seven with the home button and `am start` to come back (two of them 65 s long, the second on a probe bundle, below), two pairs of `KEYCODE_APP_SWITCH` (the second press switches to the previous app, so each pair is one trip), and a screen-off with `KEYCODE_SLEEP`. After `KEYCODE_WAKEUP` the phone stayed on its lock screen with the player in the background until it was unlocked by hand, and then the player came back.
- **Every trip logged** `entering the background`, `swapchain suspended` and `audio paused`, then `back from the background after N s, 0 frames in it`, `swapchain resumed at 1080x2340` and `audio resumed`, with no error. The one warning was the lost surface above, on the screen-off. `adb logcat -b crash` stayed empty throughout. Suspending took about 5 ms and resuming about 3 ms, the audio's stop and start 10 to 20 ms more. The audio output was miniaudio's own device (`audio ready: 48000 Hz, 2 channels, output device`), stopped and started with no warning.
- **`adb exec-out screencap -p` frames** before and after each trip show the playground drawing, with the sweeper turned further and the cube pile it knocks over moved on. In the 65 s trip the ball, rolling towards a held finger when the player left, carried its momentum on after the return.
- **Time away is not simulated.** With a debug line in `player.lua` for one run only (not committed), in a bundle copied into `files/` to override the packaged one and removed afterwards, the ball jumped from the ground at game time 11.92 s and was at 1.48 m, the top of the jump, when the player went to the background at 12.34 s. After 64.8 s away, its first frame back was at 12.36 s, with a `dt` of 15 ms and the ball still at 1.48 m. It then fell and landed at 13.01 s: a jump of about 1.1 s of game time across 65 s of real time.

ADR-0018's check 4 needs a person's hand on the device. The runs above were the agent's, through `adb`, apart from unlocking the phone after the screen-off. Michael then confirmed the check by hand on the Galaxy S25 Ultra: the player goes to the background and comes back, still drawing.

Still to do before the phone runs a game as the desktop does: the capture in the player.

### The capture in the player

The eighth Android step gives the player the editor's capture, as ADR-0018's "Device checks and reports" decides, and runs the device checks with it ([player.md](player.md#capture-runs)):

- **`runtime` has the capture.** The flag table, its parser, the run that steps a capture through its frames (`CaptureRun`, driven through `ICaptureTarget`) and the screenshots' readback and PNG writing (`Screenshots`, with stb's writer) moved from `editor` into `runtime`. Each flag in the table says whether the player takes it: `--screenshot`, `--scene`, `--play`, `--shading-term` and `--settle-frames` yes, `--screenshot-window` and `--select` no. The editor keeps its API as a thin layer, `editor::CaptureRun` stepping the shared run through the editor, and its tests are unchanged. ADR-0018 says `editor` already linked `runtime`. It did not: it linked `audio`, `scripting` and `ui`. It links `runtime` now, which comes earlier in the module order.
- **`--play` without floating-point `from_chars`.** `runtime::parseSeconds` reads the integer and fraction digits and the exponent itself, and scales them by a power of ten in a double. It takes exactly what `from_chars` took before: an optional minus, digits with at most one point, an exponent only when it is whole, and a value in float's range that is finite and not negative. So `2.5`, `.5`, `5.`, `1e3` and `-0` are accepted, and `+1`, ` 1`, `1e`, `0x1`, `inf`, `-0.5`, `1e39` and `1e-50` are refused. A fuzz run for this step, not committed, put seven million inputs through it and through floating-point `from_chars`, with GCC 14 and again with Clang 22: random strings over digits, `.`, `e`, `E`, `+`, `-`, `x` and space, and plausible decimals and exponents. It gave no difference in what was accepted and none in a value's bits.
- **The player.** `sonnet_player [game] [capture flags]` in any order, with the game optional and `game.sbundle` from the content root without one, on a phone too. `--scene` alone makes a plain run of another scene, which replaces cooking with `sonnet_cook --scene`, since a default cook keeps every scene. A capture run opens the game paused (`GameDesc::paused`), so the world waits for the assets and draws the settle frames before it simulates, as the editor does before it plays. A relative output resolves against `Platform::prefPath("sonnet", "player")`, which on Android is the app's `files/` (SDL ignores the names there). The run logs the device and its Vulkan version, then the frame times over its last hundred frames. Those are the mean CPU time of the simulation and recording, the mean time between frames, and each pass's GPU time, leaving out the frame that copies the screenshot. A mistake in the arguments is logged before any window opens, since a phone shows only the log. The log's last line, `exit ok` or `exit with failure`, was already the platform's and is how a phone reports the exit code.

**Tests.** `runtime_tests` parses the player's five flags with the game anywhere among them, refuses the editor's two with a pointer to `--help`, and makes `--scene` alone a plain run for the player and an error for the editor. It checks that the two usages come from one table, and puts `--play` through 18 accepted values, three minus zeros and 29 refused ones. A relative path resolves under the preferences directory and an absolute one stays. The frame times average the last hundred frames of a ring. On Lavapipe, a capture parsed as the player parses it (`--play 0.25 --shading-term albedo --screenshot shots/albedo.png`) runs the basic sample from a paused game to `Done`. It lands under `prefPath("sonnet", "runtime_tests")` at the window's 640×480 with something lit in the middle, and nothing is warned about. A cooked bundle takes `--scene scenes/playground.scene.json`, and a scene it lacks fails the run with nothing written. That is as far as the fixtures go: `apps/player/main.cpp`, which parses the real `argv`, resolves against the real `prefPath` and logs the last line, is reached by no test, as for the lifecycle. `editor_tests` pass unchanged. `tools/check_docs.py` reads the table from `runtime` and holds [editor.md](editor.md#screenshots) to all seven flags and [player.md](player.md#capture-runs) to the player's five.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The editor's `--help`, and its error messages and exit code for six bad command lines, are the same as `main`'s. Its `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to those of a `main` build in a worktree, on the RTX 4090.
- The desktop player's captures exit 0 with nothing warned about, and two playground runs with `--play 3` are byte-identical. They are not the editor's images of the same flags, and cannot be. The editor draws through its fly camera into its 968×662 viewport. The player draws through the scene's camera into its 1280×720 window.

**On the Galaxy S25 Ultra** (Android 16, portrait), with the `android-debug` APK and the basic sample cooked for `android` by the Linux `sonnet_cook` and packaged, all four checks the agent can do:

1. **The Vulkan description** (`adb shell cmd gpu vkjson`): the Adreno 830 at 1.3.284, with the same driver as ADR-0018's question 6 recorded (0x80320040). Every feature `VulkanDevice` requires is there: `multiDrawIndirect`, `drawIndirectFirstInstance`, `samplerAnisotropy`, `shaderDrawParameters`, the thirteen 1.2 features, `dynamicRendering`, `synchronization2` and `shaderDemoteToHelperInvocation`. So are the four 1.4 extensions and the swapchain. The limits cover what the engine needs: 256 bytes of push constants against 128, seven descriptor sets against two, and 16 777 216 of each update-after-bind descriptor kind against the bindless set's largest, 4 224 sampled images. `vkjson` lists the four extensions but not their feature bits, which the device selector checks when the player starts ("with the 1.4 features as extensions"). **Pass.**
2. **The launcher:** started by the agent with `adb shell monkey -p io.github.pacheco95.sonnet -c android.intent.category.LAUNCHER 1`, which sends the launcher icon's intent, not by a hand on the icon. The player drew the basic sample's start scene (`adb exec-out screencap -p`), and `adb logcat -b crash` stayed empty. **Pass.**
3. **Capture runs**, each started with `am start -S` and its arguments in `args`: the start scene with `--play 3` for `final`, `albedo` and `normal`, and the playground with `--scene scenes/playground.scene.json --play 3`. Each wrote a 1080×2340 PNG to `/data/data/io.github.pacheco95.sonnet/files/`, and each log ends in `exit ok`, with no error and no native crash. The one warning in each is the debug APK's missing validation layer, the exception every phone run above has had. `adb shell run-as ... cat` and `adb exec-out run-as ... cat` both pulled the PNGs byte for byte, matching `md5sum` on the phone. Compared by eye with the desktop player's captures of the same arguments, the portrait window shows the middle of the same view. Lighting, shadows, sky, bloom and checker match, the albedo has full colour rather than MoltenVK's old red-only reads, the normal term's colours match, and the playground's ball, spawned cube and sweeper stand where Linux has them after 3 s. A second `final` run, from an APK rebuilt at the end of the step, wrote the same bytes as the first. A run with a scene the bundle lacks, and one with the editor's `--select`, end in `exit with failure` with the reason on the line before. `--es args '--scene scenes/playground.scene.json'` alone plays the playground from the packaged bundle, without a special cook. **Pass**, the validation-layer warning aside.
4. **The background and back:** confirmed by Michael by hand in [the previous step](#the-mobile-lifecycle). This step changed nothing there.
5. **A finger on the playground:** confirmed by Michael by hand in [the touch input step](#touch-input).
6. **The frame times** of check 3, below. **Pass.**

The phone's screen stayed on through the runs, with "stay awake while charging" already set, so no run waited in the background.

| Capture run (Debug builds) | Galaxy S25 Ultra, 1080×2340: CPU / apart / GPU (forward) | RTX 4090, 1280×720: CPU / apart / GPU (forward) |
|---|---|---|
| start scene, `final` | 2.04 / 5.27 / 4.96 ms (3.38 ms) | 0.98 / 2.78 / 0.18 ms (0.030 ms) |
| start scene, `albedo` | 2.36 / 6.64 / 5.46 ms (3.96 ms) | 0.96 / 2.78 / 0.19 ms (0.032 ms) |
| start scene, `normal` | 2.36 / 6.69 / 5.56 ms (4.07 ms) | 0.96 / 2.78 / 0.19 ms (0.032 ms) |
| playground | 4.50 / 7.61 / 5.54 ms (3.99 ms) | 1.47 / 2.78 / 0.17 ms (0.023 ms) |

On the phone the forward pass is 68 to 73 % of the GPU frame, then FXAA (0.31–0.39 ms), the first bloom downsample (about 0.3 ms) and tone mapping (0.15 ms): the full-screen work at 2.5 million pixels. Every shadow cascade and the culling pass together stay under 0.3 ms. Frames came 5.3 to 7.6 ms apart, just above the GPU time and well above the CPU's 2 to 4.5 ms, so the phone is GPU-bound in these runs. The desktop and Mac numbers recorded above measure something else: [M7](#m7-gpu-driven-rendering)'s `renderer_tests "[benchmark]"`, ten thousand draws and a hundred lights at 1080p in Release. That is 0.50 ms of GPU time a frame on the RTX 4090 and 2.1 ms on the M4 Max ([macOS could not create a device](#macos-could-not-create-a-device)). The Mac has not made a capture run yet, which is M10's Mac check.

**M9's "done".** "Done when the basic sample runs on an Android 16 device." ADR-0018 defines running as its six checks, and on the Galaxy S25 Ultra all six now pass: 1, 2, 3 and 6 by the agent in this step, and 4 and 5 by Michael by hand. That holds in portrait only. Turned to landscape, the swapchain takes the surface's 90° pre-transform and the scene is drawn rotated and stretched ([#47](https://github.com/Pacheco95/sonnet/issues/47)). Every run here kept auto-rotate off and the phone in portrait. The checks do not ask for landscape, so the criterion holds as the ADR words it. But a player on a phone got a broken image by turning it, which is not a game that runs, so M9 stayed open until #47 was fixed ([Rotation](#rotation)).

What ADR-0018 got wrong, besides the NDK floor, the manifest's version and touch coordinates recorded above:

- `editor` did not link `runtime`.
- The debug APK always logs the missing validation layer at warning level, so check 3's "no warning" has that one exception on any phone.
- `am start` hands new arguments to a player that is running only with `-S`, which stops it first, and `adb shell` needs the whole command in one pair of quotes so that the `args` extra stays one string.
- SDL's pref path on Android is `/data/data/<package>/files/`, the same directory as `/data/user/0/<package>/files/` for the first user.
- The desktop and Mac numbers check 6 sets the phone's beside are a benchmark of another workload, so this step measured the desktop player's capture runs as well.

### Rotation

The last Android step fixes [#47](https://github.com/Pacheco95/sonnet/issues/47): turned to landscape, the player drew the scene rotated and stretched. vk-bootstrap takes the surface's current transform as the swapchain's pre-transform when none is set, and in landscape the S25 reports `Rotate90` with a 2340×1080 extent. The swapchain told Android its frames were already rotated, and the compositor turned them again. The swapchain now asks for the identity pre-transform where the surface supports it and lets the compositor rotate. With identity, every present in landscape reports suboptimal, which recreated the swapchain every frame when the fix was first tried: 321 times in 4.4 s. So a suboptimal whose surface extent is the swapchain's and whose transform alone differs keeps the swapchain ([rendering.md](rendering.md#rotation)). The change is in `rhi` alone. The window already had the landscape size, so the renderer, the capture and the touches needed nothing.

Verified on Linux: `build/linux-debug` (GCC 14) builds with no warnings and all 14 suites pass on Lavapipe. The editor's viewport `--screenshot` of the playground is byte-identical to one from `main`. Its `--screenshot-window` is not, and neither are two window captures from the same build, which differ in the same few digits of text.

**On the Galaxy S25 Ultra**, with the playground cooked for `android`, packaged into the `android-debug` APK and turned with `adb shell settings put system user_rotation` (auto-rotate off):

- **Every orientation is upright and undistorted**: portrait, both landscapes and upside down, from `adb exec-out screencap -p`, with the whole playground in view in landscape.
- **The swapchain is created once per turn**: portrait to landscape made one 2340×1080 swapchain, and landscape back to portrait one 1080×2340 swapchain. Landscape to the other landscape and portrait to upside down made none. In each orientation, no further swapchain appeared in the log.
- **The compositor rotates in hardware**: `dumpsys SurfaceFlinger` lists the player's layer as `DEVICE` composition with `ROT_90` in landscape and `ROT_180` upside down, so the rotation costs no GPU pass.
- **Check 5 in landscape**: a finger held with `adb shell input motionevent DOWN` on the left of the board pulled the ball to the point under it.
- **Check 4 in landscape**: a trip to the recents screen and back, which stays in landscape, suspended the swapchain and resumed it at 2340×1080, upright. The home screen is portrait only, so a trip there comes back in portrait, which also resumed correctly.

With the scene upright in every orientation, and ADR-0018's six checks passing, M9 is done.

## M10: iOS export

- Xcode build of the player from a macOS host, MoltenVK linked statically, packaging into an app bundle.

It builds on M9's shared work: ASTC cooking, touch input and the SDL3 callback loop. Most of what could stop it was already checked on the device ([Checked before the code](#checked-before-the-code)).

Done when the basic sample runs on an iOS device.

### The build

`ios-debug` and `ios-release` (the Xcode generator, `arm64-ios`, iOS 16.3) and the `moltenvk` overlay port landed in [PR #58](https://github.com/Pacheco95/sonnet/pull/58), the same shape as M9's Android presets: `cmake/SonnetIOS.cmake`'s `sonnet_add_ios_bundle()` for the app bundle, `apps/player/ios/` for its `Info.plist` and launch storyboard, and an iOS CI job on `macos-latest`. MoltenVK is linked into the player on every Apple platform, not just iOS, which is also what closed [the macOS export gap](#the-macos-export-needs-the-vulkan-sdk) below.

Two bugs surfaced only once a real Mac ran the build, both the same root cause and neither caught by the presets' first green CI run: under the Xcode generator, a custom command's `COMMAND` arguments never get Xcode's own per-platform build setting (`${EFFECTIVE_PLATFORM_NAME}`) substituted by the script-phase shell that is supposed to do it, no matter which generator expression asks for the target's bundle or output directory. `cmake/SonnetIOS.cmake`'s `SONNET_IOS_BUNDLE` copy and `cmake/SonnetShaders.cmake`'s own copy of the compiled shaders both built their destination that way and both landed their files next to a directory literally named `RelWithDebInfo${EFFECTIVE_PLATFORM_NAME}` instead of `RelWithDebInfo-iphoneos`. Neither was caught by CI's first pass, which never gave `sonnet_player_app` a cooked bundle to place. Both are fixed the same way: build the destination from pieces CMake resolves on its own (`CMAKE_CURRENT_BINARY_DIR` and `$<CONFIG>`) plus a hardcoded `-iphoneos`, which is exact since this project never targets the simulator (`ports/moltenvk` ships no simulator slice). The iOS CI job now cooks a bundle and asserts the built `.app`'s contents directly, so a regression here fails CI instead of needing a Mac to catch it.

### The Mac-only checks

Before a phone was available, `agents/m10-mac-checks` ran what only needed the Mac: the exported macOS player, with every Vulkan SDK variable cleared, got past window and device creation and wrote a screenshot, closing [the macOS export gap](#the-macos-export-needs-the-vulkan-sdk); `SONNET_IOS_BUNDLE` with a real cooked bundle landed `shaders/` and `game.sbundle` inside the built `.app`, confirming the fix above; and a build signed with a real Apple Development team, `codesign -dv` confirmed, closing over CI's `CODE_SIGNING_ALLOWED=NO` shortcut.

### The device checks

`agents/m10-device-checks` ran ADR-0018's six checks on an iPhone 15 Pro Max (iOS 27.0, the same phone [Checked before the code](#checked-before-the-code) probed), with `ios-release`, a real cooked bundle and a real signing team:

1. **The Vulkan description**: `Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC` from the device-selection log line, matching what `vkprobe` already found for this phone. **Pass.**
2. **The launcher**: installed and launched through `devicectl`, the start scene drawn with checkerboard, shadows, sky and every primitive in place; Michael then tapped the icon by hand for a fresh launch and confirmed the same. **Pass.**
3. **Capture runs**, `--play 3` for `final`, `albedo` and `normal` on the start scene and one of the playground: all four exited 0 and wrote correct PNGs — `albedo` and `normal` in full colour, not MoltenVK's old red-only reads, and the playground's ball, stacked boxes and sweeper mid-motion. Each log carries an exception the same shape as M9's check 3 had: a warning neither new nor iOS-specific, `[mvk-warn] ... Blending is enabled for attachment with format VK_FORMAT_R32_UINT`, from the id pass's picking buffer, also seen on the Mac-only run. **Pass, that warning aside** ([#59](https://github.com/Pacheco95/sonnet/issues/59) tracks it).
4. **The background and back**: confirmed by Michael by hand. The console shows `entering the background`, `swapchain suspended`, `audio paused`, 92 frames away, then `swapchain resumed`, `audio resumed`, and the scene still drawing.
5. **A finger on the playground**: confirmed by Michael by hand — the ball rolls toward the touch point.
6. **The frame times** of check 3, below. **Pass.**

| Capture run (Release build) | iPhone 15 Pro Max, 1290×2796: CPU / apart / GPU (forward) |
|---|---|
| start scene, `final` | 0.50 / 16.67 / 11.29 ms (6.95 ms) |
| start scene, `albedo` | 0.51 / 16.67 / 11.92 ms (7.34 ms) |
| start scene, `normal` | 0.49 / 16.67 / 10.53 ms (6.40 ms) |
| playground | 0.90 / 16.67 / 11.75 ms (5.42 ms) |

With ADR-0018's six checks passing, the R32_UINT warning tracked rather than blocking, M10 is done.

## M11: Gameplay core

Every planned milestone is done, so these four are chosen from what M4 to M8 deferred and from the README's unmeasured targets. They are ordered by one rule: 1.0.0 waits for the project and bundle formats to stop changing, so whatever touches the scene, prefab or bundle formats lands first and [M14](#m14-performance-targets-and-10) freezes them. An ADR is accepted before the code of each of the first three, as in M4 to M8.

M4 left scripts able to move things but not to hear about them. This milestone closes that and the other gameplay deferrals.

- [ADR-0022](decisions/0022-gameplay-events-and-script-properties.md), accepted first: events are data that physics records and scripting delivers; a `Scripts` component of slots with declared properties replaces `Script`, and the scene format goes to version 3.
- `physics` and `scripting`: contact and trigger events delivered to scripts (`onContact`, `onTriggerEnter`, `onTriggerExit`); compound bodies from a hierarchy's colliders.
- `scripting`: per-instance script properties, shown and edited in the inspector and stored in the scene; several scripts on one entity; `require` between scripts.
- The scene camera component the milestone first listed already exists (`world::sceneCamera`, the player and the editor's Game panel), so it is not part of this milestone.

Done when the playground gains a trigger-driven pickup and a script with inspector-editable properties, and plays the same in the editor, the player and the cooked bundle, with `editor_tests` and `runtime_tests` covering it on Lavapipe.

1. `world`: reflection for a repeated member of a reflected struct (`World::registerVector`, through flecs' opaque collections), `std::string` as reflected text, and `World::embedJson`, which shows a string that holds JSON as the JSON itself in files and in undo's copies. `Scripts` and `ScriptSlot{script, properties}` in `scripting` replace `Script`, and the scene format goes to version 3 with a migration of each `Script` into a one-slot `Scripts`. Vector reflection survived the JSON serializer and the meta cursor the inspector walks, so [ADR-0022](decisions/0022-gameplay-events-and-script-properties.md)'s fallback of four fixed slots was not needed.
2. `physics`: the `Trigger` tag, which makes a body a Jolt sensor; a contact listener that only records, from any thread, into a mutex-guarded buffer; `IPhysicsWorld::events()`, which the stepping thread fills after `Update` from how many shape pairs touched before and after the step, sorted by entity ids and capped at 1024 with the excess counted and warned about once; and compound bodies from a hierarchy, where a collider with no `RigidBody` joins the body of its nearest ancestor that has one.
3. `scripting`: instances keyed by entity and slot, `onContactBegin`, `onContactEnd`, `onTriggerEnter` and `onTriggerExit` delivered to both entities before `fixedUpdate`, `properties` declarations overlaid with the slot's JSON (and set again live when the slot changes), `IScriptRuntime::properties` for the inspector, and `require` with per-revision caching, reload of the scripts that required a changed module, and cycle errors that name the chain.
4. `editor`: the slot list in the inspector (add, remove, reorder, a script asset dropped on the header, the Add component button or a hierarchy row), one widget per declared property with revert, entity pickers that take hierarchy rows, edits as component commands, the pure operations in `ScriptSlots`.
5. `samples`: `pickup.lua` and `score.lua` and two trigger pickups in the playground, the spawner, elevator, sweeper and player with their numbers as properties, every sample scene and prefab at version 3 (the showcase's and the lighting scene's generators write it, and reproduce the migrated files byte for byte), and the version becomes 0.12.0.

Done: the playground's `Coin` is a trigger that takes the ball and its `Crate coin`, the same script with another `value` and `collector` set in the scene, is taken by the first crate to land under the spawner, adding to the score a shared module keeps. `editor_tests` plays the playground on Lavapipe and finds that pickup gone and the other still there, restored by stop, and runs a script slot through the inspector, a property edit reaching the running instance and stop discarding it. `runtime_tests` plays the playground from the project folder and from the cooked bundle and reads the same log line from both, so version 3 reached the bundle without a bundle change. `physics_tests` plays a pile of boxes through a trigger with no workers and with six and gets the same events, step for step. `scripting_tests` covers the hooks, properties and `require`. The thousand-box `physics_tests "[benchmark]"` before and after, which the listener and the event pass could have slowed: in Release (Clang 22) 2.91 ms per step on no workers became 3.00 and 1.15 ms on fifteen became 1.16 to 1.25, about 3 percent and within the run-to-run noise on the pooled figure; in Debug (GCC 14) 86.4 became 88.0 and 14.6 became 15.1. `physics_tests` and `scripting_tests` pass under the address and undefined-behaviour sanitizers and the thread sanitizer, which needed one entry in `tools/tsan.supp` for the assert-only thread id Jolt's mutex writes in its Debug build, reached once a pile of bodies falls asleep on several workers. Not run on a phone, on Windows or on macOS here: CI builds those, and the sample's new scripts need nothing of them.

Decisions made along the way that the ADR left open: `self.slot` counts from one, like a Lua array; a sleeping body keeps its contacts (Jolt reports them as removed, and a static sensor sees only awake bodies), so a pair is kept dormant until a body is gone or both are awake without the contact; a trigger sees dynamic and kinematic bodies, and static ones only while they move, since Jolt makes a sensor see static bodies only from an active kinematic one at a cost a pickup does not need; the declarations are sorted by name since a Lua table has no order.

Deferred: repeated members are not visible from Lua (`entity:get("Scripts")` leaves `slots` `nil`), and no other component has one yet; no event for a continuing contact, which a script keeps its own state for; the runtime does not clamp a property to its `min` and `max`, only the inspector's widgets do; a property of an enum or a list type; the top-level code of a script runs when the inspector first asks for its properties in edit mode; reordering slots rebuilds their instances, losing their state; and a trigger on a folded-in child collider is ignored.

## M12: Animation and effects

- ADR first: where morph targets and particle simulation run.
- `world` and `assets`: blending and crossfades between clips, animation events that call script functions, several clips on one entity, root motion.
- Morph targets from glTF, in the skinning compute pass.
- Particles: an emitter component, simulated in a compute pass and drawn indirectly with the machinery of [M7](#m7-gpu-driven-rendering), previewed in the editor.

Done when a character crossfades from idle to walk with a footstep event, a morph-target sample plays, and a particle sample runs in the editor, the player, on Android and on iOS.

## M13: Rendering quality

- ADR first: temporal anti-aliasing and the depth pyramid.
- Temporal anti-aliasing with motion vectors, the default over FXAA, which stays selectable.
- Occlusion culling from a depth pyramid in the culling pass; the surviving counts read back so the statistics report what drew rather than what was submitted; tighter bounds for skinned meshes.
- Transparency in the shadow and depth passes beyond alpha masking.
- Compressed environment maps in the bundle (BC6H, and an ASTC HDR format on mobile), instead of uncompressed RGBA16F.

Done when golden-image tests with a tolerance show the showcase scene without ghosting, an occluder-heavy benchmark submits fewer draws for identical output, and the `renderer_tests "[benchmark]"` numbers are recorded here.

## M14: Performance targets and 1.0

- Measure the README's targets for the first time on the hardware they name: 10 000 visible draws and 100 dynamic lights at 1080p in 16.6 ms on a 2020-era mid-range desktop GPU, and in 33 ms on a 2022 flagship phone (the Galaxy S25 Ultra and the iPhone 15 Pro Max are at hand). Fix what misses, starting with the per-frame fill, which is bandwidth-bound and so needs writing less ([Known gaps](#the-per-frame-fill-is-bandwidth-not-computation)).
- Close the Known gaps a release should not carry: [#59](https://github.com/Pacheco95/sonnet/issues/59), the thread sanitizer's blind spots where feasible, and a parallel transform hierarchy only if a benchmark asks.
- Freeze the formats: audit the versions of the scene, prefab, project, sidecar and bundle formats, write down the migration policy, bump to 1.0.0 and tag it.

Done when the targets are met, or the shortfall is documented with numbers, CI is green on every job, and 1.0.0 is tagged.

## Known gaps

Work M8 named rather than did, and what closing it turned up, each with what was measured and what would close it, so the next change starts from the evidence rather than from the summary. A gap that has been closed keeps its entry, saying what closed it and what it measured. These are engineering debts; the feature backlog is [Later](#later).

### Scene loading blocked the frame

Closed. M8 made the draw list request its meshes, but loading a scene still blocked: `loadModelPrefab` needs a model's node hierarchy before it can create the entities, and `AssetDatabase::model` imported the whole glTF file to give it one — every mesh, every image decoded and, on a first open, cooked — for every model in the project, since each is placed as a prefab when the project opens.

The plan was the hierarchy ahead of the payloads, and it held, with two corrections to what this entry predicted. The sidecar did not carry enough: it lists sub-assets by name and identity, but not the nodes' parents or transforms. The file's JSON does, and none of what made the import slow was ever needed for it, so `model` now parses the JSON without loading a buffer or an image and walks the nodes through the same function the full import does ([assets.md](assets.md#database)). And the player never had the problem: in a bundle the model is a payload of its own, a decode away from its meshes. The fallback, a prefab that fills in, was not needed, so no instance is ever an entity missing its children.

What the entities then ask for arrives through the request form: meshes already did, and `SkinPalette` and `AnimationPlayback` now request their skins and clips too, since the first ran every frame in the editor and would have imported a skinned model's file on the frame after its scene loaded. `assets_tests "[benchmark]"`, placing the basic sample's three models in Release:

| | warm cache | first open, cooking the textures |
|---|---|---|
| full import, as `model` did | 0.38 ms | 11.2 ms |
| hierarchy only | 0.023 ms | 0.023 ms |

The sample's files are small; the old figure grew with a file's images and the new one grows only with its JSON. The cooking still happens on a first open, on the job system, off the frame.

### The per-frame fill is bandwidth, not computation

M7 left the per-frame object, material and light fill as the larger part of a frame's CPU cost and expected M8 to spread it over workers. It does not spread. `renderer_tests "[benchmark]"` in Release, ten thousand draws on an RTX 4090:

| workers | 0 | 1 | 3 | 15 |
|---|---|---|---|---|
| fill | 0.261 ms | 0.189 ms | 0.180 ms | 0.199 ms |

One extra worker takes about a quarter off and the rest add scheduling, which is the shape of a bandwidth limit rather than a divisible loop: ten thousand draws of a 160-byte `ObjectData` is 1.6 MB written every frame into a `MemoryUsage::CpuToGpu` buffer, which on a discrete GPU is host-visible device memory across the bus. Threads cannot remove that. Writing less can, and there were two independent levers.

The first is taken. `ObjectData::normalMatrix` was 64 of the 160 bytes, the inverse transpose of `model`, computed per draw with `glm::inverse` and sent in full. The vertex shader now derives it as the cofactor matrix of `model`'s upper 3×3, which is the inverse transpose times the determinant and so correct for any scale without a flag or a special case ([rendering.md](rendering.md#gpu-driven-submission)). Same benchmark:

| workers | 0 | 1 | 3 | 15 |
|---|---|---|---|---|
| fill, 160-byte entries | 0.261 ms | 0.189 ms | 0.180 ms | 0.199 ms |
| fill, 96-byte entries | 0.150 ms | 0.151 ms | 0.149 ms | 0.157 ms |

The forward pass's GPU time did not move within the noise of the measurement, about 0.21 ms either way, so the derivation is free where it runs. And the pool no longer changes the fill at all, which is the bandwidth limit with nothing left in front of it. The per-draw inverse was the part a worker could take; it is gone, and what remains is bytes.

The second lever is what is left. **Most objects do not move between frames.** The array is rebuilt from scratch every frame because it lives in a per-frame transient allocation. A persistent device-local buffer written only where a draw's transform, colour or material changed would cut the traffic to what actually moved, at the cost of a dirty list and a stable slot per draw, which the draw list does not have today. At 0.15 ms for ten thousand draws it is no longer the frame's largest CPU cost, so it waits for a scene that needs it.

### The thread sanitizer cannot see two libraries the engine depends on

`linux-tsan` polices the engine's own concurrency and was blind in two places, one of them now closed, both because vcpkg ships the library without instrumentation and the sanitizer cannot reason about synchronization it did not compile.

- **Jolt** — closed. It was suppressed wholesale by `race:JPH::` in `tools/tsan.supp`, on reports about `TempAllocatorImpl`, whose plain `mTop` is written from several threads by design, ordered by job dependencies that resolve inside `JobSystemWithBarrier.cpp`, in the library; the suppression matched any frame, so it also stopped the sanitizer policing how `physics` drives Jolt. A triplet closed it with less than the overlay port this entry first proposed, since the port itself needs no change: `triplets/x64-linux-tsan.cmake` is the `x64-linux` triplet plus `-fsanitize=thread` under `if(PORT STREQUAL "joltphysics")`, registered as an overlay triplet in `vcpkg-configuration.json` and named by the preset. What was to be checked rather than assumed, the compiler, needed nothing: vcpkg passes `CC` and `CXX` through on Linux, and Jolt's build cache names `clang++-20` with the sanitizer flags, its debug library carrying two thousand `__tsan_` references. The other ports are not instrumented, but are rebuilt once under the new triplet's name, which the plan said they would not be. With `race:JPH::` removed, every suite passes under the sanitizer, `physics_tests`, `world_tests` and `runtime_tests` eight runs out of eight, so nothing replaced it: no race in how `physics` drives Jolt, and the `Mutex::try_lock` read of `mLockedThreadID` exists only under `JPH_ENABLE_ASSERTS`, which the port does not define. [build.md](build.md#presets) describes the triplet in the preset's row.
- **The Vulkan driver.** Mesa's Lavapipe rasterizes on a pool of its own and reports races and lock-order inversions from inside it by the hundred, through stacks that enter at `VulkanDevice`, so no suppression narrow enough to spare engine frames exists. The preset points `VK_DRIVER_FILES` at `/dev/null` so the GPU cases skip, which leaves `rhi`, `ui` and `runtime` running a handful of tests each under the sanitizer.

The second gap matters less than it looks, because `rhi` is main-thread-only by design, and that rule is now enforced rather than assumed: every device records the thread it was created on and asserts it on the frame bracket, resource creation and destruction, and the uploads and transient allocations ([rendering.md](rendering.md#frame-structure)). It catches the case the sanitizer is missing — a later change scheduling rhi work onto the pool — without needing the driver instrumented, and it found nothing to fix, which is the answer the assertion exists to keep true. With Jolt instrumented, what remains of this gap is the driver, which is not ours to instrument.

### A pending texture draws as nothing in particular

Closed. A texture still importing draws as a placeholder, and a mesh still importing draws nothing. ADR-0013's decision says the request form returns "a placeholder handle" and its consequences that every caller "has to be able to draw nothing for that asset"; this reads the first as the texture case and the second as the mesh case, a clarification rather than a change of direction, so no ADR supersedes it. The request form itself still returns an invalid handle for anything not loaded; the placeholder lives in the material that reads the texture.

**Meshes: nothing, as before.** A stand-in mesh would have to be the right size to help, and nothing of the right size is to hand before the import: a unit box where a building goes misleads more than an empty space, and geometry that pops from a box into a mesh reads worse than geometry that appears. The entity is already whole meanwhile — in the hierarchy panel, selectable, with its transform — since a model is placed from its hierarchy ([Scene loading blocked the frame](#scene-loading-blocked-the-frame)). A box fitted to the glTF `POSITION` accessor's `min` and `max`, which the JSON carries and `importGltfStructure` could return, is the option to revisit if a user asks for one.

**Textures: a placeholder, which needed more than one.** A texture was not requested at all where it mattered most: `AssetDatabase::resolve`, which turns a material's source into the renderer's description, called the synchronous `texture` for every slot, so the first frame that drew a file material imported and cooked its textures in place — the stall M8 removed for meshes, still there for a `.material.json` and its images. What was shown for a texture that did not load was the renderer's fallback, the same whether it failed or had not arrived. What closed it ([assets.md](assets.md#database)):

1. `resolve` asks with `requestTexture`, so a material is created at once and its textures import on the job system.
2. A base colour still in flight resolves to a neutral grey placeholder the database creates on first use and keeps, like the built-in meshes, though not as an asset, so it is never offered in the browser; the other slots keep the renderer's white and flat-normal fallbacks, which already mean "no effect". Whether a texture is pending is `m_pending` holding it, or holding its glTF file.
3. The main-thread job that publishes a requested texture, or a glTF file's images, calls `refreshMaterials`, which only a re-import did before; a failed import calls it too, so the placeholder gives way to the fallback.
4. Two bugs the change would have made common, both older than it: a synchronous `texture`, `mesh` or re-import that met a request in flight for the same file imported it a second time, and the later publish overwrote the first texture without destroying it. They now finish the request first. And a glTF material resolving an image that failed to decode would have requested the very file being published, which is now marked before its materials resolve.
5. The test, `a material requests its textures and shows a placeholder until they arrive` in `assets_tests`, on a pool with no workers: a material file whose texture is not cooked yet is created without touching the cache, reads the placeholder in its base-colour slot and the fallback in its normal slot while `loading()` is true, and reads the texture in both after `waitForLoads`; a material whose texture fails reads the white fallback.

### The transform hierarchy stays single-threaded

Closed by measurement. `TransformSystem` is the one world system with something to gain from threads and the one flecs cannot split: it depends on `cascade` to write parents before children, and flecs' workers cross tables with no barrier between them ([ADR-0013](decisions/0013-job-system.md)). Making it parallel would mean grouping the matched entities by depth and running a `parallelFor` per level. The benchmark that was to decide it, `world_tests "[benchmark]"` in Release, ten thousand entities with every transform recomputed each frame, as it is:

| shape | a frame |
|---|---|
| flat: ten thousand roots | 0.10 ms |
| wide: a hundred roots of a hundred children | 0.19 ms |
| deep: a hundred chains a hundred long | 0.76 ms |
| deep: ten chains a thousand long | 0.78 ms |
| the wide shape's arithmetic in a plain loop | 0.18 ms |

It is not worth doing. The wide shape costs what its arithmetic costs, so threads could divide it, but the whole of it is under a fifth of a millisecond at ten thousand entities. The deep shapes cost four times their arithmetic, and the rest is flecs iterating a table per entity, since `ChildOf` is a pair and every parent in a chain makes a table of its own: threads cannot divide that, and a barrier per level would add to it, because a level of a deep hierarchy holds only as many entities as there are chains. If deep hierarchies ever matter, what would move them is not recomputing the subtrees that did not change, which removes the iteration and the arithmetic together. The `Static` tag exists but promises nothing today, since a static body is still moved in the editor, so that change would start by giving it a meaning.

### A mirrored single-sided draw renders inside out

Closed. Found while deriving the normal matrix in the shader ([rendering.md](rendering.md#gpu-driven-submission)), and older than that change. `world` allows a negative scale — the inspector takes one, and `Transform::fromMatrix` decomposes a mirrored basis into one — but every pipeline was built with a counter-clockwise front face, fixed at creation, and nothing flipped it per draw. A mirroring transform reverses a triangle's winding on screen, so the rasterizer took a mirrored draw's outside for its back: a single-sided one was culled inside out, showing its far faces through its near ones, in the shadow, depth, id and selection passes as well. A double-sided one drew the right faces and shaded correctly, because the cofactor's sign and `SV_IsFrontFace` both flipped and cancelled. What closed it:

1. `rhi`: the front face is dynamic state on every graphics pipeline, `vk::DynamicState::eFrontFace`, core since Vulkan 1.3 and part of the extended dynamic state in the [Vulkan baseline](rendering.md#vulkan-baseline). Binding a graphics pipeline sets it to counter-clockwise, since a dynamic state has to be set before a draw; `ICommandList::setFrontFace` sets clockwise until the next bind, and the null device traces it as `setFrontFace Clockwise`.
2. `renderer`: a draw is mirrored when its transform's upper 3×3 has a negative determinant, worked out where `prepareFrame` already transforms the bounds. Mirrored draws sort into batches of their own, after double-sidedness and before the mesh, and a mirrored batch sets its front face before its indirect call; the blended draws, recorded one by one, set it when it changes. Every drawing pass shares the batches, so the shadow, depth, id and selection passes follow.
3. `sonnet.slang`: with the winding corrected, `SV_IsFrontFace` no longer flips for a mirrored draw, so `transformNormal` multiplies the cofactor by the sign of the determinant, `dot(cross(x, y), z)`, as `mirrorSign`. The plan missed one more sign: the bitangent `cross(n, t) * w` of a mirrored normal and tangent is the mirrored bitangent negated, so a normal map's green channel read upside down on a mirrored draw, double-sided or not, before this change as well. The vertex shader multiplies the tangent's `w` by the same sign.
4. The tests. `mirrored draws are batched apart and drawn with a clockwise front face`, on the null device: a mirrored and a plain box make two batches in each of the six drawing passes, and each mirrored batch's call follows `setFrontFace Clockwise`. `a mirrored single-sided draw renders as its baked mirror image on a GPU`, on Lavapipe: a turned, mirrored box with a normal map tilted along the bitangent shades as the same box with the transform baked into its vertices, its triangles rewound and its bitangent sign flipped; it fails with the fixed front face, and with the front face fixed but the bitangent sign left alone.

### macOS could not create a device

Closed. Found by running the editor on an Apple M4 Max under macOS 26. MoltenVK 1.4.1 and 1.4.2 have no `drawIndirectCount`, which [ADR-0012](decisions/0012-gpu-driven-rendering.md) had made a required feature, so device selection rejected the only GPU. Metal cannot take a draw count from a buffer, so the feature is not coming. [ADR-0014](decisions/0014-indirect-draws-without-count.md) makes it optional. What closed it:

1. `rhi`: `drawIndirectCount` is enabled where present and reported as `DeviceInfo::drawIndirectCountSupported`. `ICommandList::drawIndexedIndirect` draws a fixed number of commands. `DeviceDesc::disableDrawIndirectCount` lets the GPU tests take MoltenVK's path on Lavapipe. A failed device selection now lists why each device was rejected.
2. `renderer`: without the count, `cull.slang` writes one slot per candidate, with no instances for the culled ones. Each batch draws its whole range with `drawIndexedIndirect`, and the counter-clearing dispatch is skipped. Everything else is shared with the counted path, which is unchanged: the benchmark on the RTX 4090 gives the same 12 indirect calls and the same pass times within noise.
3. The scene shaders then failed to compile on Metal. `ObjectData` held a `Vertex *`, Slang loads a storage-buffer element as a whole struct, and SPIRV-Cross translates the pointer inside it into invalid MSL. Reading its fields one by one in the source was tried on the Mac and changes nothing. [ADR-0015](decisions/0015-bindless-vertex-buffers.md) pulls vertices through a bindless array of storage buffers instead, with an index in `ObjectData`. `IDevice::storageBufferIndex` hands out the slots, and a full array gives no slot and skips the draw rather than write past its end.
4. Export found no player to copy: the export dialog takes it from the editor's directory, and a build only ever put it under `apps/player/`. A post-build step now copies it beside the editor, on every platform.
5. The tests. On the null device, the same scene records each path's calls. On Lavapipe, `rhi_tests` draws zero-instance slots, and the culling test and the picking tests run once on each path. `an unselected draw beside a selected one gets no outline on a GPU` fails on the uncounted path if a culled slot keeps its instance. `storage buffers take bindless slots, reuse freed ones and get none from a full array` fills all 4096 slots on Lavapipe, and fails through validation if a full array writes a descriptor anyway.

What remained was measuring the path on macOS, and it mattered: MoltenVK encodes one Metal draw per slot when the frame is submitted, 4.96 ms of every frame on the M4 Max at ten thousand draws. [ADR-0016](decisions/0016-instanced-batches.md), instanced batches, supersedes ADR-0014: a batch is one command whose instances are its survivors, found through a visible list, on every platform, so the uncounted path, `drawIndirectCount` and `DeviceDesc::disableDrawIndirectCount` are gone. On the RTX 4090 in Release the frame lost nothing, 0.60–0.62 ms of GPU time before and 0.50 ms after ([rendering.md](rendering.md#gpu-driven-submission)). `indirectCallCount` reports the batches the samples make, which no longer span a mesh's submeshes: 60 calls a frame for the basic sample's start scene, ten batches from fourteen draws in each of the six scene passes, and 24 for the playground, four from seventeen. On the M4 Max the submission fell from 5.01–5.16 ms to 0.25–0.26 ms, short of the ADR's 0.1 ms but no longer paying per draw: at a hundred draws it is still about 0.2 ms, so what remains is MoltenVK's fixed cost per frame ([rendering.md](rendering.md#gpu-driven-submission)), shadow cascade 0 from 0.38 ms of GPU time to 0.015 ms, and the frame from 4.7 ms of GPU time to 2.1 ms.

### Two GPU tests shade differently on MoltenVK

Closed by [ADR-0017](decisions/0017-depth-images-in-their-own-bindless-array.md), after a first workaround that did not reach the cause. The first full `ctest` on an Apple M4 Max (macOS 26.7, MoltenVK 1.4.1) left two pixel tests failing, both in `renderer_tests` and both passing on an RTX 4090 and on Lavapipe: the sun-lit ground beside a box read at ambient, and a sphere lit by a blue sky read brighter than the sky.

The first round read it as two problems. A fragment shader that kept a `SampleCmp` result live seemed to lose the sun's lighting arithmetic, so `DeviceInfo::comparisonSamplersUsable` made MoltenVK compare shadow depth in the shader. The sphere's overshoot was put down to the split-sum approximation, and the environment test was loosened by 20 of 255. On the Mac the basic sample still rendered washed out, with the sun lighting the sides of objects and no visible shadows.

A probe branch (`agents/mac-lighting-probes`) added a debug view of the forward shading's terms and read them back on both machines. The shadow factor was right. The BRDF lookup table's bias read as large as its scale, although a blit of the table's texels matched the RTX to four decimals, and a sampler-free `Load` of the same texture returned green equal to red. MoltenVK's translated fragment shader declared the whole sampled-image array as `depth2d`: SPIRV-Cross types an array as Metal depth textures when any use of it compares, so every colour read in the forward shader returned its red channel alone. That tilted every normal, dropped the colour from textures and turned the table's bias into its scale. The first workaround kept the `SampleCmp` behind a runtime branch, which is why it changed nothing.

ADR-0017 moves depth images into a bindless array of their own, removes the workaround and restores the strict environment test. Two GPU tests now read a green texture's albedo beside the shadow comparison and the lookup table's scale and bias.

### `assets_tests` hangs intermittently

Closed ([issue #24](https://github.com/Pacheco95/sonnet/issues/24)). Twice on CI, `assets_tests` stopped producing output and was killed at ctest's 300-second timeout: on the Windows job of run 35780091420 (2026-09-22), in the asynchronous import cases, and on the Linux ASan job of run 35994756004 (2026-09-24), in `a changed source is re-imported by polling and materials follow their textures`, right after `re-imported painted.material.json`. Both runners had two cores. This entry guessed that the main thread waited on a request whose publish needed the main thread. That was wrong: the job system is not involved.

The hang is in KTX-Software's vendored Basis Universal. `cookKtx2` compresses to UASTC with one thread per core, and `ktxTexture2_CompressBasisEx` builds a `basisu::job_pool` for the call and destroys it on return. The pool's destructor set its kill flag without holding the pool's mutex, then called `notify_all`. A pool thread that had just read the flag as false in its wait's predicate, and had not yet blocked, missed the notification and slept for ever; the destructor's `join` waited for it. Looping the case on two cores (with `hardware_concurrency` reporting two, as a CI runner does) reproduced it once in about 2,600 runs, under the ASan preset after 333 seconds. The stacks were the CI log's step: the main thread in `setTextureSettings` → `reimport` → `texture` → `loadFileTexture` → `cookKtx2` → `ktxTexture2_CompressBasisEx` → `~job_pool` → `std::thread::join`, the pool's one thread in `job_pool::job_thread` waiting on `m_has_work`, and both engine workers idle in `JobSystem::workerLoop`. A loop of compressions alone with two threads hung within 5,000 in each of five runs. What closed it:

1. `ports/ktx`: `0009-job-pool-kill-flag-under-mutex.patch` sets the flag under the mutex, as upstream Basis Universal now does ([ports/README.md](../ports/README.md)). With it, ten runs of 100,000 compressions on two cores finished.
2. `assets`: `compressing KTX2 textures back to back never hangs basisu's job pool` compresses a 4×4 texture 20,000 times with two threads, on a thread of its own, and fails if that takes more than 60 seconds; it takes half a second. Without the patch it failed by that deadline in five runs of five. The race is between two of Basis Universal's threads, so there is nothing to order by hand; the loop is sized to lose it.

### Undefined behaviour does not fail the sanitizer job

Closed. The `linux-asan` preset enables the undefined-behaviour sanitizer, but a finding only printed: nothing made it fatal, unlike `linux-tsan`'s `halt_on_error=1`. So CI passed with undefined behaviour in the log. The Linux ASan job on CI found one: `ByteReader::read` (`modules/assets/src/BinaryIo.h`) called `std::memcpy` with a null destination when it read an empty array. `size` was 0, so nothing was copied, but a null argument to `memcpy` is undefined behaviour all the same. It happened while opening a cooked bundle, at mesh "Box", whose skin is empty. What closed it:

1. `assets`: `ByteReader::read` returns before `memcpy` when `size` is 0. It is the file's only `memcpy`; `ByteWriter` appends through `std::vector::insert`, which takes an empty range. `an empty array and an empty string read back without touching memcpy` reads an empty array, an empty string and a value after them; with the early return removed it fails under the fatal sanitizer.
2. `physics`: made fatal, the sanitizer stopped four suites on its `vptr` check, which the module had turned off for itself alone. vcpkg builds Jolt without RTTI, so the module was compiled without it too, and its objects had vtables without typeinfo. `physics_tests` stopped on a `flecs::term` whose vtable the linker took from `physics`, and `scripting_tests`, `runtime_tests` and `editor_tests` when they destroyed the `IPhysicsWorld`. The manifest now asks for the `joltphysics` port's `rtti` feature, and the module drops `-fno-rtti` and its `-fno-sanitize=vptr` ([physics.md](physics.md#jolt)). `the physics world carries its type information into other modules` takes `typeid` of the world and casts it; it crashed in a plain Debug build before the change.
3. The build: `SONNET_SANITIZERS` compiles with `-fno-sanitize-recover=undefined` rather than setting `halt_on_error=1` in the test preset's `UBSAN_OPTIONS`. The flag is compiled into the binary, so the first finding aborts however the binary runs, `ctest` or a single case run by hand; an environment variable in the test preset holds only under `ctest --preset` ([build.md](build.md#presets)). The whole suite then passed under `linux-asan` with no other finding.

### The macOS export needs the Vulkan SDK

Closed by [M10](#m10-ios-export)'s `moltenvk` port. An exported game on macOS used to start only where the Vulkan SDK was installed. Run from its export directory with the SDK's variables cleared, the player used to stop before it opened a window:

```
VK_ICD_FILENAMES= DYLD_LIBRARY_PATH= ./sonnet_player
[critical] [platform] [SdlEntryPoint.cpp:54] startup failed: SDL_CreateWindow failed: Installed Vulkan Portability library doesn't implement the VK_KHR_surface extension (Platform, SdlWindow.cpp:24)
```

The export copies the player, the shaders and the bundle, but no Vulkan driver. SDL then searches the machine: it finds no `vkGetInstanceProcAddr` in the process, and loads the first of its known library names that opens (`SDL_cocoavulkan.m`). Here that was a loader with no driver registered, which offers only its own instance extensions, so the surface extension SDL needs was missing. On a Mac with no loader at all, the same search ends in "Failed to load Vulkan Portability library". [ADR-0018](decisions/0018-mobile-export.md) closes it the way it carries MoltenVK on iOS: the player links the static MoltenVK from Khronos's pinned release, which SDL finds in the process before it searches the machine.

The mechanism was confirmed on the Mac that reported this: the Vulkan SDK's system install put a loader (`libvulkan.1.dylib`, 1.4.341) and `libMoltenVK.dylib` in `/usr/local/lib`, with their driver manifests in `/usr/local/share/vulkan/icd.d`. That is where SDL's search found a loader. `vkprobe`, which links the static MoltenVK the fix uses, created an instance and passed on the M4 Max with every Vulkan SDK variable cleared ([M9, checked before the code](#checked-before-the-code)). The entry stayed open until the player itself linked it: `agents/m10-mac-checks` ran the command above, from a real export directory built by the `moltenvk`-linked player, with every Vulkan SDK variable cleared, and it got past window and device creation, found "Apple M4 Max" through MoltenVK, and wrote a screenshot.

### A fresh macOS build fails where the Vulkan SDK installed its headers system-wide

Open, reported by the Mac run of ADR-0018's questions. A fresh `vcpkg install` for `arm64-osx` in a new install root failed in `vk-bootstrap` with `unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?`. The Vulkan SDK's system install put its 1.4.341 headers in `/usr/local/include/vulkan`, which Apple Clang searches by default, and they shadow the manifest's `vulkan-headers` 1.4.357, which vk-bootstrap 1.4.357 is written against. Existing build directories are unaffected, because their install root was built before. A new clone, or a new build directory on that Mac, would hit it. CI's macOS runners have no SDK in `/usr/local`, so they cannot see it.

Reproduced by the follow-up run. `vcpkg install` of `vk-bootstrap` alone, with binary caching off so the port compiles, fails the same way with `VULKAN_SDK` empty, which it was in both runs, so that variable is not the cause. The failing command is `/usr/bin/c++ -I<vk-bootstrap's src> -isystem <install root>/arm64-osx/include ...`, and the compiler's note places `PFN_vkGetLatencyTimingsNV` in `/usr/local/include/vulkan/vulkan_core.h` (`VK_HEADER_VERSION` 341). `/usr/local/include` is the first of Apple Clang's default system directories. The run concluded that the install root is searched after it. That is not how Clang orders them: `-isystem` directories are searched before the default system directories. So why the SDK's header wins is not explained yet. The next check, on that Mac: whether `<install root>/arm64-osx/include/vulkan/vulkan_core.h` exists when the port compiles, and the same compile with `-H`, which prints every header in the order it is opened.

What would close it: that check, then either keeping `/usr/local/include` out of the ports' builds or not installing the SDK's headers system-wide, whichever the check points at.

## Later

Nested scene instances beyond prefabs, C++ game-code module hook, terrain, game UI, native file dialogs, incremental cooking. Temporal anti-aliasing and particles moved into [M13](#m13-rendering-quality) and [M12](#m12-animation-and-effects).
