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

Five open issues come before M9. Two touch it directly. [#27](https://github.com/Pacheco95/sonnet/issues/27) decides where the Slang library and the Vulkan loader come from, and M9's build work changes the same lines. [#29](https://github.com/Pacheco95/sonnet/issues/29) is a shadow artifact in the screenshots that M9's device checks compare against by eye. The other three are small editor bugs. Each is its own branch and pull request, in the order below: the first changes the build under everything else, and the second changes the reference screenshots the rest are checked with. Scene tabs ([#12](https://github.com/Pacheco95/sonnet/issues/12)), planar translate handles ([#15](https://github.com/Pacheco95/sonnet/issues/15)) and snapping ([#16](https://github.com/Pacheco95/sonnet/issues/16)) are features and wait until after M9.

### 1. Linux binaries load vcpkg's Vulkan loader ([#27](https://github.com/Pacheco95/sonnet/issues/27))

`assets` links Slang's shared `libslang-compiler.so`, so CMake gives every binary that links `assets` a RUNPATH into `build/<preset>/vcpkg_installed/x64-linux/lib`. That directory also holds vcpkg's `libvulkan.so.1`, pulled in by the `vulkan` stub port, and SDL's `dlopen` finds it there before the system's. That loader is built without window-system support, so the editor cannot create a window. It was found on a fresh Ubuntu 24.04 machine with no Vulkan SDK installed. PR [#28](https://github.com/Pacheco95/sonnet/pull/28) gives the loader X11 and Wayland support as an interim fix.

It also reproduces on the development machine (RTX 4090) once the SDK's variables are cleared. There the SDK's `LD_LIBRARY_PATH` normally comes ahead of the RUNPATH and hides the bug. Without those variables, SDL loads the build directory's loader, and the editor stops with the issue's error: `Installed Vulkan doesn't implement either the VK_KHR_xcb_surface extension or the VK_KHR_xlib_surface extension`. With `LD_LIBRARY_PATH=/lib/x86_64-linux-gnu` it opens on the system's loader.

In `linux-debug`, the editor, the player, `sonnet_cook` and the test suites from `assets` up to `editor` carry the RUNPATH. `core`, `platform`, `rhi`, `renderer` and `ui` do not. Whether the player and the cook also list Slang as a runtime dependency (`NEEDED`) depends on the compiler: with GCC 14 only the editor does, and with Clang 22 all three do.

1. `ShaderCompiler` moves from `assets` to `editor`, as [ADR-0018](decisions/0018-mobile-export.md) decided, and `find_package(slang)` moves with it. Every binary below the editor then loses the RUNPATH. An exported player no longer needs Slang, whatever the toolchain.
2. The editor and `editor_tests` keep Slang. Its libraries are copied beside those binaries, and those binaries' RUNPATH becomes `$ORIGIN` instead of the vcpkg directory. Two alternatives get measured against this first: `platform` naming the system loader explicitly, and an overlay of the `vulkan` stub port that keeps the loader out of the install tree.
3. A test in `editor_tests` asserts that the loader `Platform` keeps mapped is not under `vcpkg_installed`. It must fail on the build before the change. `platform_tests` cannot catch this, since it has no RUNPATH.
4. PR [#28](https://github.com/Pacheco95/sonnet/pull/28) is closed in favour of this one. Its Linux setup section in [build.md](build.md) is kept, and its loader features are dropped because they no longer reach anything.

Done when `readelf -d` shows no RUNPATH into `vcpkg_installed` on any binary, and when the editor, with the SDK's variables cleared and `vcpkg.json` without loader features, opens a window on the system's loader, as `LD_DEBUG=libs` shows. If the choice in step 2 moves a cross-module decision ([ADR-0004](decisions/0004-vcpkg-first.md) on where the loader comes from, [ADR-0006](decisions/0006-vulkan-object-ownership.md) on which one the process uses), it gets an ADR.

### 2. Shadow seam and dashed shadow edge in the basic sample ([#29](https://github.com/Pacheco95/sonnet/issues/29))

Two artifacts in the basic sample come from the shadows, since both show in the `shadow-factor` shading term. One is a straight seam across the ground at a fixed depth, with evenly spaced ticks, on Intel and on Lavapipe. The other is a row of lit dashes along a cube's shadow edge, on Intel only. `forward.slang` switches cascades hard at each view-depth split, samples a 3×3 kernel with no margin at a cascade's edge, and multiplies the bias by the cascade's index, so the bias jumps at every split.

1. A `cascade` shading term, the cascade index as a colour, confirms whether the seam lies on a split. It stays, since the phones will need it too.
2. The fix follows what that shows. Likely changes: a cascade is chosen only if the kernel's footprint fits inside its bounds, the last stretch before a split blends with the next cascade, and a normal-offset bias scaled by each cascade's texel size replaces the per-cascade multiplier.
3. A `renderer_tests` GPU test renders a sun-lit plane with no occluders across every split and asserts a shadow factor of 1 everywhere on it. It must fail before the fix.

Done when the test passes on Lavapipe, and screenshots of both samples on the RTX 4090 and Lavapipe show neither artifact. The dashes appear only on the reporter's Intel GPU, so that machine confirms them.

### 3. The mouse leaks into the UI while flying the viewport camera ([#14](https://github.com/Pacheco95/sonnet/issues/14))

Right-drag in the viewport switches on relative mouse mode, but every SDL event still reaches Dear ImGui. SDL keeps reporting a moving cursor position in relative mode, so ImGui's cursor wanders over the other panels and hovers them. It was reproduced on Linux. While the camera looks, mouse motion is kept from ImGui but still turns the camera, and button events still pass, so releasing the button ends the look. When the look ends, ImGui is given the real cursor position once. An `editor_tests` case feeds a motion event while the camera is active, and checks that ImGui's cursor did not move and the look delta did.

### 4. The export dialog does not close on OK ([#11](https://github.com/Pacheco95/sonnet/issues/11))

The dialog stays open after a successful export on purpose, to show what was written. Nothing says how to close it, though. After a successful export, OK and Cancel become a single Close button, which Enter and Escape also trigger. A failed export keeps both buttons and shows the error.

### 5. Export the current scene only ([#13](https://github.com/Pacheco95/sonnet/issues/13))

The bundle always starts at the project's `startScene` and carries every scene in the project. `CookOptions` gains an optional scene. When it is given, the manifest's start scene is that scene and it is the only scene in the bundle. Every prefab and asset is still cooked, because scripts reach them at run time. The export dialog gets a "Current scene only" checkbox. It is available when a scene file is open, and says when that scene has unsaved changes, since the export reads it from disk. `sonnet_cook` gains `--scene` to match. An `assets_tests` case cooks the playground alone and reads the bundle back. Done when an export of the playground with the checkbox set runs in the player from its own directory and shows the playground.

## M9: Mobile export

Mobile export was one milestone and is now two, because each platform is blocked on something different. Android needs the NDK build and a device on [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s 1.3 path. iOS needs a Mac, signing and a phone in hand. Either one would have held the whole milestone back. M9 is the work both platforms share, plus Android; iOS is [M10](#m10-ios-export). [ADR-0018](decisions/0018-mobile-export.md) decides how both are done and what counts as running, and its decisions hold across the split.

- Shared: ASTC texture cooking (`CookPlatform` gains `android` and `ios`), the ASTC formats and `astcSupported` in `rhi`, touch input mapping, and the OS-owned loop through the SDL3 callbacks.
- Android: NDK build of the player, Android 16+ device testing, packaging into an APK.

Done when the basic sample runs on an Android 16 device.

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

## M10: iOS export

- Xcode build of the player from a macOS host, MoltenVK linked statically, packaging into an app bundle.

It builds on M9's shared work: ASTC cooking, touch input and the SDL3 callback loop. Most of what could stop it was already checked on the device ([Checked before the code](#checked-before-the-code)).

Done when the basic sample runs on an iOS device.

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

Open. An exported game on macOS starts only where the Vulkan SDK is installed. Run from its export directory with the SDK's variables cleared, the player stops before it opens a window:

```
VK_ICD_FILENAMES= DYLD_LIBRARY_PATH= ./sonnet_player
[critical] [platform] [SdlEntryPoint.cpp:54] startup failed: SDL_CreateWindow failed: Installed Vulkan Portability library doesn't implement the VK_KHR_surface extension (Platform, SdlWindow.cpp:24)
```

The export copies the player, the shaders and the bundle, but no Vulkan driver. SDL then searches the machine: it finds no `vkGetInstanceProcAddr` in the process, and loads the first of its known library names that opens (`SDL_cocoavulkan.m`). Here that was a loader with no driver registered, which offers only its own instance extensions, so the surface extension SDL needs was missing. On a Mac with no loader at all, the same search ends in "Failed to load Vulkan Portability library". [ADR-0018](decisions/0018-mobile-export.md) closes it the way it carries MoltenVK on iOS: the player links the static MoltenVK from Khronos's pinned release, which SDL finds in the process before it searches the machine. The check that closes this entry is the command above, from an export directory, passing, and the same run on a Mac with no SDK installed.

The mechanism is confirmed. On the Mac that reported this, the Vulkan SDK's system install put a loader (`libvulkan.1.dylib`, 1.4.341) and `libMoltenVK.dylib` in `/usr/local/lib`, with their driver manifests in `/usr/local/share/vulkan/icd.d`. That is where SDL's search found a loader. `vkprobe`, which links the static MoltenVK the fix uses, created an instance and passed on the M4 Max with every Vulkan SDK variable cleared ([M9, checked before the code](#checked-before-the-code)). The entry stays open until the player itself links it.

### A fresh macOS build fails where the Vulkan SDK installed its headers system-wide

Open, reported by the Mac run of ADR-0018's questions. A fresh `vcpkg install` for `arm64-osx` in a new install root failed in `vk-bootstrap` with `unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?`. The Vulkan SDK's system install put its 1.4.341 headers in `/usr/local/include/vulkan`, which Apple Clang searches by default, and they shadow the manifest's `vulkan-headers` 1.4.357, which vk-bootstrap 1.4.357 is written against. Existing build directories are unaffected, because their install root was built before. A new clone, or a new build directory on that Mac, would hit it. CI's macOS runners have no SDK in `/usr/local`, so they cannot see it.

Reproduced by the follow-up run. `vcpkg install` of `vk-bootstrap` alone, with binary caching off so the port compiles, fails the same way with `VULKAN_SDK` empty, which it was in both runs, so that variable is not the cause. The failing command is `/usr/bin/c++ -I<vk-bootstrap's src> -isystem <install root>/arm64-osx/include ...`, and the compiler's note places `PFN_vkGetLatencyTimingsNV` in `/usr/local/include/vulkan/vulkan_core.h` (`VK_HEADER_VERSION` 341). `/usr/local/include` is the first of Apple Clang's default system directories. The run concluded that the install root is searched after it. That is not how Clang orders them: `-isystem` directories are searched before the default system directories. So why the SDK's header wins is not explained yet. The next check, on that Mac: whether `<install root>/arm64-osx/include/vulkan/vulkan_core.h` exists when the port compiles, and the same compile with `-H`, which prints every header in the order it is opened.

What would close it: that check, then either keeping `/usr/local/include` out of the ports' builds or not installing the SDK's headers system-wide, whichever the check points at.

## Later

Temporal anti-aliasing, nested scene instances beyond prefabs, C++ game-code module hook, terrain, particles, game UI.
