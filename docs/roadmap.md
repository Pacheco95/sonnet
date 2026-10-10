# Roadmap

Milestones are sequential and each ends with something runnable. Every feature listed in the README maps to one milestone here. Finished milestones keep their scope, their "done when" line and what they deferred; the per-step record of M9 and M10 is in [reports/m9-mobile-export.md](reports/m9-mobile-export.md), the closed Known gaps and the issues queued before M9 are in [reports/roadmap-closed-work.md](reports/roadmap-closed-work.md), and commit-level history is in `git log`.

## M0: Foundation

Build scaffolding and the two lowest modules.

- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `cmake/` helpers, `.clang-format`, `.editorconfig`, `.gitignore`, CI workflow.
- `core`: handles with generations, logging, assertions, UUID, GLM configuration, Tracy zones.
- `platform`: `IWindow`, input events, SDL3 implementation with the callback-based main loop.
- `rhi`: instance and device creation with vk-bootstrap adopted into RAII, VMA, swapchain, per-frame resources, a triangle drawn through the render hardware interface, Slang compiled at build time.
- Tests: `core`, `platform` (headless), `rhi` on Lavapipe.

Done when the editor executable opens a window, clears it and draws a triangle, all three desktop platforms build and test in CI, and the triangle test passes on Lavapipe. Windows and macOS runners have no Vulkan implementation, so the rhi tests skip there and the triangle is verified on Linux.

## M1: Editor shell

- `ui`: ImGui with SDL3 and Vulkan backends in dynamic-rendering mode, docking, multi-viewport on desktop.
- `renderer`: render graph with transient resources and barriers, an offscreen viewport target.
- `editor`: docking layout, viewport panel with fly camera (right mouse plus WASD, Q/E), log panel, frame statistics overlay (CPU and GPU times per pass, draw count, VMA budget).
- Primitives: box, sphere, plane, cylinder, capsule for testing without assets (cone, torus, ramp, stairs, hemisphere, arch and icosphere followed).

Done when the editor shows a lit primitive scene in a dockable viewport with live statistics. Two items named in the docs waited for a later milestone: runtime shader compilation for hot reload came with asset hot reload in M3, and the log panel's `file:line` links needed the preferences of M2's project handling.

## M2: World

- `world`: flecs integration, core components, hierarchy, reflection registration, scene serializer with versioning, prefabs.
- `editor`: hierarchy panel, inspector generated from reflection, translate/rotate/scale gizmos, picking, selection outline, undo/redo command stack, play/stop with snapshot restore, project open and create.
- `apps/samples/basic` project.

Done when a scene can be authored from primitives, saved, reopened, and played with a script-free rotating object driven by a system. The sample's spinning box is that object. Two refinements wait for later: native file dialogs instead of the path modal, and the fixed timestep, which arrived with physics in M4.

## M3: Assets and PBR

- `assets`: UUID sidecars, database, glTF import, image import, KTX2 cooking, hot reload.
- `renderer`: clustered forward pipeline with depth pre-pass, PBR materials, directional and punctual lights, cascaded shadow maps, image-based lighting, skybox, bloom, tone mapping, anti-aliasing.
- `editor`: asset browser, material editing, import settings in the inspector.

Done: the sample renders with shadows and image-based lighting. The README's target was measured for the first time with `renderer_tests "[benchmark]"`, ten thousand draws and a hundred lights at 1080p on an RTX 4090: about 1.1 ms of GPU time per frame, but about 95 ms of CPU time recording six passes of ten thousand draws each, which is why M7 moved culling and draws to the GPU. Deferred: BC5 for normal maps, transparency in the shadow and depth passes beyond alpha masking ([M13](#m13-rendering-quality)), and 2020-era mid-range hardware for the target itself ([M14](#m14-performance-targets)).

## M4: Physics and scripting

- `physics`: `IPhysicsWorld` with Jolt: static, dynamic and kinematic bodies, box, sphere, capsule and mesh shapes, raycasts, debug draw.
- `scripting`: `IScriptRuntime` with Lua and sol2: entity and component access through reflection, input, logging, hot reload.
- `editor`: physics and script components in the inspector, physics running in play mode only.

Done: the basic sample's playground plays in the editor with its scripts and physics and is back as it was on stop, which `editor_tests` checks headless on Lavapipe. Deferred: contact and trigger events, compound bodies, per-instance script properties, several scripts per entity and `require` (all closed in [M11](#m11-gameplay-core)), and Jolt on the engine's job system (closed in M8).

## M5: Audio and animation

- `audio`: `IAudioDevice`, sources, listener, mixing with miniaudio.
- Skeletal animation: skins and clips from glTF, animation player component, GPU skinning.

Done: the basic sample's start scene plays its skinned and node animations with a spatial hum in the editor, which `editor_tests` checks headless on Lavapipe, and the Khronos animated samples (CesiumMan, Fox, RiggedFigure, RiggedSimple, BoxAnimated, InterpolationTest) import and play. Deferred: blending, morph targets, animation events and root motion (all closed in [M12](#m12-animation-and-effects)); animation in edit mode, streaming long sounds instead of decoding them whole, sound cooking into the bundle, and several clips on one entity.

## M6: Player and desktop export

- `apps/player`: opens a project or a cooked bundle, no editor code.
- Cook tool: cooked asset bundle, binary scenes, manifest.
- Export dialog in the editor for Windows, Linux and macOS.

Done: `sonnet_cook` cooks the basic sample into a 680 KB bundle with no warnings, and the player runs it from a directory holding only the binary, `shaders/` and `game.sbundle`: no project folder, no importers, no SDK. `runtime_tests` checks the same two paths headless on Lavapipe. Deferred: compressing the bundle's environment maps, which are stored as uncompressed RGBA16F ([M13](#m13-rendering-quality)); Lua bytecode instead of source; incremental cooking, which redoes the whole project; native file dialogs for the export folder; and cross-compiling a player from the editor, which stays CI's job.

## M7: GPU-driven rendering

M3 measured the engine against the README's target and found the gap is CPU-side. [ADR-0012](decisions/0012-gpu-driven-rendering.md) moves culling and draw submission to the GPU: culling into indirect draws, batched per pipeline and mesh. It comes before the job system because it removes the work rather than spreading it over threads. [ADR-0016](decisions/0016-instanced-batches.md) later made a batch one command over its survivors.

Done: `renderer_tests "[benchmark]"` on an RTX 4090 in Release, ten thousand draws of two meshes and a hundred lights at 1080p, records 12 indirect calls where it recorded 60000 draw calls; the six scene passes fall from 2.69 ms of CPU to 0.28 ms, and GPU time falls from 1.24 ms to 0.64 ms. Deferred: occlusion culling and a depth pyramid, meshlets, a shared index arena, sorting the blended draws on the GPU, reading the surviving counts back so the statistics report what drew, and tighter bounds for a skinned mesh (all in [M13](#m13-rendering-quality)).

## M8: Job system and asynchronous loading

[ADR-0013](decisions/0013-job-system.md) settles the shape first, above all how flecs, Jolt and the engine's own parallel loops share one machine without starving each other.

- `core`: the job system: jobs with dependencies, a parallel for, main-thread affinity, Tracy zones on the workers.
- `physics`: `JPH::JobSystem` implemented on it, replacing `JobSystemSingleThreaded`.
- `assets`: the asynchronous request form, which returns at once with a placeholder until the job finishes ([assets.md](assets.md#database)).
- `world`: not threaded after all; a benchmark showed the transform hierarchy costs under 0.2 ms at ten thousand entities.
- `renderer`: the per-frame object, material and light upload spread over workers.
- A `linux-tsan` preset.

Done: physics scales 2.33 times on fifteen workers (`physics_tests "[benchmark]"`, a pile of about a thousand boxes, 1.23 ms per step against 2.87 ms), and the suites pass under the thread sanitizer apart from the GPU cases ([Known gaps](#the-thread-sanitizer-cannot-see-the-vulkan-driver)). The renderer's fill did not spread, being bandwidth-bound ([Known gaps](#the-per-frame-fill-is-bandwidth-not-computation)). Scene loading no longer blocks the frame, and a pending texture draws as a placeholder.

## M9: Mobile export

Mobile export was one milestone and became two, because each platform is blocked on something different: Android needs the NDK build and a device on [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s 1.3 path, iOS needs a Mac, signing and a phone. M9 is the work both platforms share, plus Android; iOS is [M10](#m10-ios-export). [ADR-0018](decisions/0018-mobile-export.md) decides how both are done and what counts as running.

- Shared: ASTC texture cooking (`CookPlatform` gains `android` and `ios`), the ASTC formats and `astcSupported` in `rhi`, touch input, and the OS-owned loop through the SDL3 callbacks.
- Android: NDK build of the player, Android 16+ device testing, packaging into an APK.

Done when the basic sample runs on an Android 16 device. It did, on a Galaxy S25 Ultra, with all six of ADR-0018's checks passing and the scene upright in every orientation. Eight steps, each a pull request, with the evidence in [reports/m9-mobile-export.md](reports/m9-mobile-export.md):

1. **Vulkan 1.3 devices** (PR #38): `rhi` accepts a 1.3 device with the four 1.4 features as extensions; `rhi_tests` also runs capped at 1.3.
2. **The Android build** (PR #39): `android-debug` and `android-release`, the player as `libsonnet_player.so`; the NDK floor is r30, since r27 to r29 stop at API 35.
3. **The APK** (PR #40): `sonnet_add_apk` without Gradle; the manifest asks for Vulkan 1.3 (`0x403000`), not ADR-0018's 1.4.
4. **Reading content from the APK** (PR #41): `Platform::openContent` and the logcat sink.
5. **The Adreno shader compiler** (PR #42): two Adreno SPIR-V bugs, worked around by `tools/spirv_for_adreno.py` after `slangc` ([rendering.md](rendering.md#shaders)).
6. **ASTC cooking** (PR #43): the phone has BC as well, so a Linux bundle transcodes to BC7 there; ASTC 4×4 is for a device with ASTC and no BC.
7. **Touch input** (PR #44) and **the mobile lifecycle** (PR #45): Android frees the surface before `WillEnterBackground`, so a lost surface is handled in `acquire`.
8. **The capture in the player** (PR #48) and **rotation** (PR #49, issue #47): the capture moved from `editor` to `runtime`; the swapchain asks for the identity pre-transform. Version 0.11.0.

Findings the ADRs did not have are listed at the end of [the report's capture step](reports/m9-mobile-export.md#the-capture-in-the-player); the pre-code questions answered on the Mac and the iPhone are in [its "Checked before the code"](reports/m9-mobile-export.md#checked-before-the-code).

## M10: iOS export

- Xcode build of the player from a macOS host, MoltenVK linked statically, packaging into an app bundle.

Done when the basic sample runs on an iOS device. It did, on an iPhone 15 Pro Max (iOS 27.0), with ADR-0018's six checks passing: the build ([PR #58](https://github.com/Pacheco95/sonnet/pull/58): `ios-debug` and `ios-release`, the `moltenvk` overlay port, `sonnet_add_ios_bundle()`, an iOS CI job), the Mac-only checks (which also closed the macOS export gap, since MoltenVK is linked into the player on every Apple platform) and the device checks. One warning is tracked rather than blocking: [#59](https://github.com/Pacheco95/sonnet/issues/59), blending enabled on the `R32_UINT` id attachment. Under the Xcode generator a custom command's `${EFFECTIVE_PLATFORM_NAME}` is never substituted, so destinations are built from `CMAKE_CURRENT_BINARY_DIR`, `$<CONFIG>` and a literal `-iphoneos`; the details are in [the report](reports/m9-mobile-export.md#m10-ios-export-detail).

## M11: Gameplay core

Chosen from what M4 to M8 deferred, ordered by one rule: [1.0.0](#100) waits for the formats to stop changing, so whatever touches the scene, prefab or bundle formats should land first.

- [ADR-0022](decisions/0022-gameplay-events-and-script-properties.md): events are data that physics records and scripting delivers; a `Scripts` component of slots with declared properties replaces `Script`, and the scene format goes to version 3.
- `physics` and `scripting`: contact and trigger events delivered to scripts (`onContact`, `onTriggerEnter`, `onTriggerExit`); compound bodies from a hierarchy's colliders.
- `scripting`: per-instance script properties, shown and edited in the inspector and stored in the scene; several scripts on one entity; `require` between scripts.

Done (version 0.12.0, PR #130): the playground's `Coin` is a trigger that takes the ball, and `Crate coin`, the same script with other properties, is taken by the first crate to land under the spawner; `editor_tests` and `runtime_tests` play it from the project folder and the cooked bundle and get the same log. The thousand-box physics benchmark moved about 3 percent, within noise.

Deferred: repeated members are not visible from Lua (`entity:get("Scripts")` leaves `slots` `nil`); no event for a continuing contact; the runtime does not clamp a property to its `min` and `max`, only the inspector's widgets do; enum and list property types; the top-level code of a script runs when the inspector first asks for its properties in edit mode; reordering slots rebuilds their instances, losing their state; and a trigger on a folded-in child collider is ignored.

## M12: Animation and effects

- [ADR-0023](decisions/0023-animation-blending-morph-targets-and-particles.md): morph targets run in the skinning pass, blending is by layer with crossfade a fade of layers, animation events are data that `world` records and `scripting` delivers, and particles are simulated in a compute pass of their own and drawn with one indirect call each.
- `world` and `assets`: blending and crossfades between clips, animation events that call script functions, several clips on one entity, root motion.
- Morph targets from glTF, in the skinning compute pass.
- Particles: an emitter component, simulated in a compute pass and drawn indirectly with the machinery of [M7](#m7-gpu-driven-rendering), previewed in the editor.

Done when a character crossfades from idle to walk with a footstep event, a morph-target sample plays, and a particle sample runs in the editor, the player, on Android and on iOS.

Done on the desktop (version 0.13.0, PR #131; the bundle's version becomes 2): `runtime_tests` plays the start scene from its project folder and from the cooked bundle and finds the same footsteps and the jelly's morph weight; `world_tests`, `renderer_tests` and `editor_tests` cover crossfades, events across loops, root motion, a morphed box and the emitters. On an RTX 4090 in Release (Clang 22), ten emitters of 20 000 slots (about 175 000 live at 1080p) simulate in 0.014 ms of GPU time and the forward pass with them takes 0.136 ms. The Android player library builds with NDK r30, including `particles.slang` through the Adreno rewrite.

Not done here: the criterion's Android and iOS halves. No phone was attached, so the particle shaders' pointer reads and the morph targets have not run on an Adreno or an Apple GPU, and the Adreno rewrite's two known compiler bugs ([M9](reports/m9-mobile-export.md#the-adreno-shader-compiler)) are the likeliest place for a surprise; the iOS build needs the Mac. Both are the first device checks of the next session that has the hardware. Deferred: sidecar `animationEvents` (events come from the glTF only), masks and additive layers, rotation root motion, sparse morph deltas, sorting and sprite sheets inside an emitter, particle collisions and trails, and a depth pyramid for particles and soft edges against the scene (culling by morphed bounds is done in [M13](#m13-rendering-quality)).

## M13: Rendering quality

- [ADR-0024](decisions/0024-temporal-anti-aliasing-and-occlusion-culling.md) first : temporal anti-aliasing, the depth pyramid and compressed HDR environments.
- Temporal anti-aliasing with motion vectors, the default over FXAA, which stays selectable.
- Occlusion culling from a depth pyramid in the culling pass; the surviving counts read back so the statistics report what drew rather than what was submitted; tighter bounds for skinned and morphed meshes (done: posed bounds from per-joint boxes).
- Transparency in the shadow passes beyond alpha masking (done: blended draws cast through a hashed alpha test; they stay out of the depth pre-pass).
- Compressed environment maps in the bundle (done: UASTC HDR, transcoded on load to BC6H, ASTC HDR or RGBA16F; the bundle's version is 3, and the `ktx` port moved to KTX-Software 5.0.0-rc2 for it).

Done when golden-image tests with a tolerance show the showcase scene without ghosting, an occluder-heavy benchmark submits fewer draws for identical output, and the `renderer_tests "[benchmark]"` numbers are recorded here.

Done (version 0.14.0; PRs #132 to #137): the three criteria hold, on Linux and then on the other platforms.

- **No ghosting.** `runtime_tests "[golden]"` compares the showcase on Lavapipe with `modules/runtime/tests/golden/showcase_taa.png` (mean error at most 1.5 levels, none over 96) and with the same frame unresolved, and `renderer_tests "[taa]"` checks that a box crossing a dark background leaves no trail and keeps its brightness, that a moving camera and a skinned box get motion vectors, and that a still scene settles to 0.1 level a frame. A person looked at TAA, FXAA and no anti-aliasing captures on Windows and found no ghosting or smearing behind the moving objects, and the iPhone's repeated captures differ by one level at most.
- **Fewer draws, identical output.** `renderer_tests "occlusion culling draws the same pixels as frustum culling while the camera moves"` holds the output equal frame by frame. The benchmark's wall scene, ten thousand draws of two meshes and a hundred lights at 1080p, on an RTX 4090 in Release with GCC 14:

  | | draws surviving culling | triangles | GPU per frame |
  |---|---|---|---|
  | wall, occlusion off | 7,345 of 10,001 | 866,604 | 0.456 ms |
  | wall, occlusion on | 61 of 10,001 | 7,728 | 0.407 ms |
  | no wall, occlusion off | 5,396 of 10,000 | 638,636 | 0.481 ms |
  | no wall, occlusion on | 5,396 of 10,000 | 638,636 | 0.508 ms |

  The wall-clock time per frame, 120 frames submitted back to back, is 0.86 to 0.89 ms in all four cases, so the CPU side, not the draws the wall removes, sets the pace; what occlusion culling changes is how many of them reach the GPU, and the one frame's timestamps cannot tell the 0.05 ms differences in the GPU column from noise. Windows on an RTX 4090 agrees: 7,345 to 61 draws, 0.504 to 0.359 ms. The macOS Release wall clock goes the way it did in the first check, +5.6% without a wall and -11.9% with one under no anti-aliasing. On the iPhone a fixture drops from 27 to 2 visible draws, pixel-identical to the same scene with culling off.
- **The other numbers.** The TAA resolve costs 0.05 to 0.11 ms of GPU time against FXAA's 0.03 to 0.05, and moves the wall clock per frame by between -0.03 and +0.06 ms, within what one frame's timestamps can tell apart ([rendering.md](rendering.md#temporal-anti-aliasing)). A blended wall's shadow on the ground reads 218, 206, 185, 141 and 32 at alpha 0, a quarter, a half, three quarters and 1 ([rendering.md](rendering.md#shadow-sampling)). The basic sample's environment fell from 262,264 to 1,216 bytes in the bundle and its bundle from 759,199 to 498,151 bytes, and a frame under the cooked environment differs from the uncompressed one by 0.2 levels on average ([assets.md](assets.md#environments)).
- **Other platforms.** Windows on an RTX 4090 ([#138](https://github.com/Pacheco95/sonnet/issues/138)) passes the GPU, HDR, shadow and benchmark suites with the BC6H branch running. A Mac ([#139](https://github.com/Pacheco95/sonnet/issues/139), [report](reports/macos-m13-validation.md)) passes them without skips, all three anti-aliasing modes pass the stability and trail checks, and its environments select BC6H. An iPhone 15 Pro Max with an A17 Pro ([#140](https://github.com/Pacheco95/sonnet/issues/140), [report](reports/iphone-m13.md)) executes TAA with history, culls the fixture and casts the blended shadow, and its ASTC HDR path, forced for a diagnostic, matches the BC6H frame within 0.03 levels on average; the basic sample at 1290×2796 in Debug takes 3.20 ms of CPU work and 12.955 ms of GPU passes in a 16.67 ms frame, and thermals were not measured. The Galaxy S25 Ultra ran the hashed shadows, TAA as the default and the environment through BC6H and, forced, ASTC HDR.

Deferred: the Debug build's occlusion timings on MoltenVK, which look inflated even with validation off and are not reproduced in Release ([#141](https://github.com/Pacheco95/sonnet/issues/141)); the automatic choice of ASTC HDR on a device that has it and no BC, which no device checked is, since the iPhone and the Mac both report BC ([#143](https://github.com/Pacheco95/sonnet/issues/143)); a capture flag to choose the anti-aliasing mode, and a debug log line naming the transcode target of a cooked texture, which the Windows check missed.

## M14: Performance targets

- Measure the README's targets for the first time on the hardware they name: 10 000 visible draws and 100 dynamic lights at 1080p in 16.6 ms on a 2020-era mid-range desktop GPU, and in 33 ms on a 2022 flagship phone (the Galaxy S25 Ultra and the iPhone 15 Pro Max are at hand). Fix what misses, starting with the per-frame fill, which is bandwidth-bound and so needs writing less ([Known gaps](#the-per-frame-fill-is-bandwidth-not-computation)).
  - **Desktop, measured:** met on an RTX 2050 laptop, the stand-in for a 2020-era mid-range card, with the GPU well inside the budget in every scene and the CPU not the limit, so the per-frame fill needs no rework for the desktop target ([report](reports/rtx2050-m14.md)).
  - **Phone, measured:** met on a Galaxy S25 Ultra, by a thin margin under sustained load, with the GPU clock throttled ([report](reports/s25-m14.md)). Not demonstrated on an iPhone with an A17 Pro, where the sustained run is slower than the target and the GPU timestamps read zero ([report](reports/iphone-m14.md), [#152](https://github.com/Pacheco95/sonnet/issues/152), [#159](https://github.com/Pacheco95/sonnet/issues/159)).
- Close the Known gaps a release should not carry: [#59](https://github.com/Pacheco95/sonnet/issues/59), the thread sanitizer's blind spots where feasible, and a parallel transform hierarchy only if a benchmark asks.
- Fix two format-versioning gaps the audit of the file formats found: the project file has no schema version ([#153](https://github.com/Pacheco95/sonnet/issues/153)) and a sidecar from a newer engine is read instead of refused ([#154](https://github.com/Pacheco95/sonnet/issues/154)). They are hardening and do not wait for a release.

Done when the targets are met, or the shortfall is documented with numbers, and CI is green on every job.

Done (version 0.15.0): the desktop target is met with a wide margin, the phone target on the Galaxy S25 Ultra by a thin one, and the shortfall on the iPhone is documented in its report. Carried forward rather than blocking: the iPhone's sustained slowdown ([#159](https://github.com/Pacheco95/sonnet/issues/159)), which cannot be split into GPU and thermal causes until the zero GPU timestamps are explained ([#152](https://github.com/Pacheco95/sonnet/issues/152)); the MoltenVK blend warning ([#59](https://github.com/Pacheco95/sonnet/issues/59)), which needs a Mac to reproduce; and [the thread sanitizer's blind spot](#the-thread-sanitizer-cannot-see-the-vulkan-driver), which no change in this milestone could reach. The parallel transform hierarchy waited for a benchmark, and none asked.

## 1.0.0

Not a milestone and not dated: the engine is in development and nothing waits for a release. 1.0.0 is tagged when the project, scene, prefab, material, sidecar and bundle formats have stopped changing, and not before. The audit of how each format is versioned today and a proposed compatibility promise (read every older schema, refuse a newer one, rebuild bundles instead of migrating them) are in the closed [PR #151](https://github.com/Pacheco95/sonnet/pull/151), to start from when the time comes.

## Known gaps

Engineering debts, each with what was measured and what would close it, so the next change starts from the evidence. The gaps that have been closed, with what closed them and what they measured, are in [reports/roadmap-closed-work.md](reports/roadmap-closed-work.md): scene loading blocking the frame, the pending-texture placeholder, the transform hierarchy (measured, not worth threading), mirrored single-sided draws, macOS device creation ([ADR-0014](decisions/0014-indirect-draws-without-count.md), [ADR-0015](decisions/0015-bindless-vertex-buffers.md), [ADR-0016](decisions/0016-instanced-batches.md)), the two MoltenVK shading failures ([ADR-0017](decisions/0017-depth-images-in-their-own-bindless-array.md)), the `assets_tests` hang (issue #24), fatal undefined behaviour, and the macOS export needing the Vulkan SDK. The feature backlog is [Later](#later).

### The per-frame fill is bandwidth, not computation

M7 left the per-frame object, material and light fill as the larger part of a frame's CPU cost and expected M8 to spread it over workers. It does not spread: on an RTX 4090 in Release, ten thousand draws fill in 0.261 ms with no workers and 0.189 ms with one, then stay flat to fifteen. That is the shape of a bandwidth limit: 1.6 MB of 160-byte `ObjectData` written every frame into `MemoryUsage::CpuToGpu` memory, which on a discrete GPU is host-visible device memory across the bus. Writing less is the lever, and there were two.

The first is taken. The normal matrix, 64 of the 160 bytes, is derived in the vertex shader as the cofactor of `model`'s upper 3×3, so entries are 96 bytes and the fill is 0.15 ms whatever the worker count, with no change in the forward pass's GPU time ([rendering.md](rendering.md#gpu-driven-submission)).

The second is what is left. **Most objects do not move between frames.** A persistent device-local buffer written only where a draw's transform, colour or material changed would cut the traffic to what moved, at the cost of a dirty list and a stable slot per draw, which the draw list does not have. At 0.15 ms for ten thousand draws it is no longer the frame's largest CPU cost, so it waits for a scene that needs it ([M14](#m14-performance-targets)).

### The thread sanitizer cannot see the Vulkan driver

`linux-tsan` polices the engine's own concurrency. Jolt, which was suppressed wholesale, is now built under the sanitizer through `triplets/x64-linux-tsan.cmake` and its suppression is gone ([build.md](build.md#presets)). What remains is the driver: Mesa's Lavapipe rasterizes on a pool of its own and reports races and lock-order inversions by the hundred through stacks that enter at `VulkanDevice`, so no suppression narrow enough to spare engine frames exists. The preset points `VK_DRIVER_FILES` at `/dev/null` so the GPU cases skip, which leaves `rhi`, `ui` and `runtime` running a handful of tests each under the sanitizer.

It matters less than it looks, because `rhi` is main-thread-only by design and every device asserts the thread it was created on at the frame bracket, resource creation and destruction, uploads and transient allocations ([rendering.md](rendering.md#frame-structure)). That catches a later change scheduling rhi work onto the pool without instrumenting the driver, which is not ours to do.

### A fresh macOS build fails where the Vulkan SDK installed its headers system-wide

Fix written, not yet verified on the Mac that showed it. A fresh `vcpkg install` for `arm64-osx` in a new install root failed in `vk-bootstrap` with `unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?`, and a fresh engine build failed the same way in `VulkanDevice.cpp`. The Vulkan SDK's system install put its 1.4.341 headers in `/usr/local/include/vulkan`, and they shadow the manifest's `vulkan-headers` 1.4.357, which vk-bootstrap 1.4.357 is written against. Existing build directories are unaffected, because their install root was built before. CI's macOS runners have no SDK in `/usr/local`, so they cannot see it.

The cause is in the compiler path, not in the include order ([the diagnosis](reports/macos-vulkan-header-search.md)): `/usr/bin/clang++` adds `-I/usr/local/include` to the compile it runs, which the CMake command line does not have, and a plain `-I` is searched before vcpkg's `-isystem` directories. It happens with an empty environment. Apple's own toolchain compiler (`xcrun --find clang++`) does not add it, and puts the install root first. Why the shim adds it was not inspected. The earlier reading, that the install root is searched after `/usr/local/include`, was wrong.

The fix is in the top-level `CMakeLists.txt`: for an `-osx` triplet, when neither `CMAKE_CXX_COMPILER`, `CMAKE_C_COMPILER`, `CXX` nor `CC` was chosen, it configures with the compilers `xcrun --find` returns. A build directory that has a compiler cached keeps it, so existing directories do not change. Still to check on that Mac: a fresh `cmake --preset macos-debug` with `/usr/local/include/vulkan` present, the build, and `ctest`; and that the iOS Xcode generator and the Homebrew LLVM local preset ([build.md](build.md#presets)) are untouched.

## Later

Nested scene instances beyond prefabs, C++ game-code module hook, terrain, game UI, native file dialogs, incremental cooking. Temporal anti-aliasing and particles moved into [M13](#m13-rendering-quality) and [M12](#m12-animation-and-effects).
