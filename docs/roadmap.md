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

Done when the basic sample runs on an Android 16 device. It did, on a Galaxy S25 Ultra, with all six of ADR-0018's checks passing and the scene upright in every orientation. Eight steps, each a pull request, with the evidence in [reports/m9-mobile-export.md](reports/m9-mobile-export.md).

Each step's pull request and finding is listed in [the report](reports/roadmap-closed-work.md#m9-the-eight-steps). In short: the Vulkan 1.3 device path (PR #38), the Android build and APK (PR #39, #40), `openContent` (PR #41), the Adreno SPIR-V rewrite (PR #42), ASTC cooking (PR #43), touch input and the lifecycle (PR #44, #45), and the capture and rotation (PR #48, #49), version 0.11.0.

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

Done when golden-image tests with a tolerance show the showcase scene without ghosting, an occluder-heavy benchmark submits fewer draws for identical output, and the `renderer_tests "[benchmark]"` numbers are recorded in the report.

Done (version 0.14.0; PRs #132 to #137): the three criteria hold, on Linux and then on the other platforms.

The evidence (no ghosting, fewer draws with identical output, the numbers, and Windows, Mac, iPhone and Galaxy S25 Ultra results) is in [the report](reports/roadmap-closed-work.md#m13-the-evidence). The wall scene submits 61 of 10,001 draws with occlusion on against 7,345 off, and the TAA resolve costs 0.05 to 0.11 ms of GPU time.

Deferred: the Debug build's occlusion timings on MoltenVK, which look inflated even with validation off and are not reproduced in Release ([#141](https://github.com/Pacheco95/sonnet/issues/141)); the automatic choice of ASTC HDR on a device that has it and no BC, which no device checked is, since the iPhone and the Mac both report BC ([#143](https://github.com/Pacheco95/sonnet/issues/143)); a capture flag to choose the anti-aliasing mode, and a debug log line naming the transcode target of a cooked texture, which the Windows check missed.

## M14: Performance targets

- Measure the README's targets for the first time on the hardware they name: 10 000 visible draws and 100 dynamic lights at 1080p in 16.6 ms on a 2020-era mid-range desktop GPU, and in 33 ms on a 2022 flagship phone (the Galaxy S25 Ultra and the iPhone 15 Pro Max are at hand). Fix what misses, starting with the per-frame fill, which is bandwidth-bound and so needs writing less ([Known gaps](#the-per-frame-fill-is-bandwidth-not-computation)).
  - **Desktop, measured:** met on an RTX 2050 laptop, with the GPU well inside the budget in every scene ([report](reports/rtx2050-m14.md)).
  - **Phone, measured:** met at 1080p on the Galaxy S25 Ultra and the iPhone 15 Pro Max by a thin margin under sustained load, missed at the iPhone's native size and on an iPad; a 2022 phone itself is unmeasured. Details and issue links: [the report](reports/roadmap-closed-work.md#m14-the-measurements).
- Close the Known gaps a release should not carry: [#59](https://github.com/Pacheco95/sonnet/issues/59), the thread sanitizer's blind spots where feasible, and a parallel transform hierarchy only if a benchmark asks.
- Fix two format-versioning gaps the audit of the file formats found, both done: the project file now carries a schema version ([#153](https://github.com/Pacheco95/sonnet/issues/153)) and a sidecar from a newer engine is skipped instead of read ([#154](https://github.com/Pacheco95/sonnet/issues/154)).

Done when the targets are met, or the shortfall is documented with numbers, and CI is green on every job.

Done (version 0.15.0): the desktop target is met with a wide margin, the phone target on the Galaxy S25 Ultra by a thin one, and the shortfall on the iPhone is documented in its report. Carried forward rather than blocking: the iPhone's sustained slowdown ([#159](https://github.com/Pacheco95/sonnet/issues/159)), which cannot be split into GPU and thermal causes until the zero GPU timestamps are explained ([#152](https://github.com/Pacheco95/sonnet/issues/152)); the MoltenVK blend warning ([#59](https://github.com/Pacheco95/sonnet/issues/59)), which needs a Mac to reproduce; and [the thread sanitizer's blind spot](#the-thread-sanitizer-cannot-see-the-vulkan-driver), which no change in this milestone could reach. The parallel transform hierarchy waited for a benchmark, and none asked.

## M15: Game UI and the game-module hook

Accepted 2026-10-11. Two features that [ADR-0007](decisions/0007-data-driven-game-structure.md) and the "Later" list deferred, in the order the [1.0.0](#100) rule gives: game UI adds components, a font asset and a bundle entry, so it lands while the formats can still change; the hook adds one field to `project.json` and no file format, so it follows. Each starts with an ADR, as M12 and M13 did, and neither starts before its ADR is accepted.

**Game UI.** What is there today is Dear ImGui in the `ui` module, which the editor and debug builds use and the player never links (`tools/check_dependencies.py` enforces it): immediate mode, the editor's look, no release builds. A game needs a UI that ships in the player, is authored in the editor, and is data like everything else a game is made of.

- [ADR-0026](decisions/0026-game-ui.md) first. Questions it settles: the elements are entities with registered components in the scene, so the inspector, undo, prefabs, the serializer and the scripting bindings work on them unchanged, against a separate UI asset format (the recommendation: no new file format, scene version unchanged because components are added, not changed); which module owns the layout and the draw list (a `gui` module between `physics` and `scripting`, so scripts hear its events as they hear physics', named apart from `ui`, which stays Dear ImGui); how the renderer draws it (a batched overlay pass after the tone map, texture atlas in the bindless array, one indirect or instanced draw per batch, budgeted on a phone).
- Components: a canvas (reference resolution, scale to the window, safe-area insets from the rotation and notch handling of [M9](#m9-mobile-export)), a rectangle with anchors, pivot, size and margins, an image or solid colour (nine-slice for a panel), a label, a button with normal, pressed and disabled looks, and row and column layouts.
- Text: a font asset (TrueType) cooked into the bundle and drawn from a signed-distance glyph atlas so one size looks right at every density. The first version uses `stb_truetype` (already installed with the `Stb` port, so no new dependency): Latin and similar left-to-right scripts, kerning, no shaping, no right-to-left text, no input method. FreeType and HarfBuzz are the upgrade if a game needs them, and the ADR names the limit.
- Input and events: a pointer or touch that lands on an element is consumed by the UI before the game sees it (`input.touches()` and the mouse synthesis of [M9](#m9-mobile-export) stay for the rest); `onClick`, `onPress` and `onRelease` reach scripts as events, as contact events do ([ADR-0022](decisions/0022-gameplay-events-and-script-properties.md)); a script sets a label's text or an element's visibility through the component API it already has.
- Editor: the canvas is drawn in the viewport at its reference resolution, elements are selected and moved with the existing inspector and gizmo, and play mode shows what ships.
- Format: the font and the atlas settings are an asset type with a sidecar, and the bundle gains the font entry and its version (`BundleVersion` 5), done in this milestone because it is the last chance before the freeze.

Not in the first version: rich text and inline images, text input fields and IME, animation and tweens beyond a script moving an element, scrolling lists, themes and styles as a system of their own, world-space UI (a health bar over a character), and gamepad focus navigation (the ADR says where it would attach).

**The game-module hook.** [ADR-0007](decisions/0007-data-driven-game-structure.md) kept native game code out of the first version and named the hook as the way back in: a project's own C++ (components, systems, script bindings) linked into a player built for that project.

- [ADR-0027](decisions/0027-game-modules.md), accepted: the engine's build takes the game in rather than the game building against an installed engine. Configured with `-DSONNET_GAME=<project folder>`, it links the project's module into the player and the editor it builds; without it both stay generic. Nothing is loaded at run time and the engine is not installed as a package, so there is no ABI to state and no shared library in the editor.
- A module is one static library per project, under `native/`, made with `sonnet_add_game_module`, with one entry point, `registerGame(runtime::GameRegistry &)`. The registry gives the `World`, for components (which reach the inspector, scenes, prefabs and Lua through reflection) and systems (play-only through `addToSimulation`), and `bindLua` for native Lua functions. A module reaches modules up to `runtime`.
- `runtime::Game` and `editor::Editor` call it before any scene loads, so what plays in the editor is what ships.
- Formats: `project.json` gains `module` and goes to version 2; the bundle records the module and goes to version 6. A player or an editor refuses a project or a bundle whose module is not the one it was linked with, so a generic editor cannot open a module's scene and drop its components on save.
- A sample, `apps/samples/hook`, with a C++ `Orbit` component and system and a Lua script that shows it in a label. CI builds it with `SONNET_GAME` on the three desktops and plays it in `Game` and the editor; the generic jobs check the refusal. The editor's export copies the module's player, as it copies the generic one.
- Not in the first version: more than one module per project, a module in a prebuilt editor, hot reload of native code (a rebuild and a restart is the cost), installing the engine as a package, and debugging support beyond the platform's own. The phones need no design of their own, since the APK and the app bundle come from the same configure, but they are checked after the desktops.

**Done when** a sample game has a main menu, a heads-up display and a pause screen made of UI entities, with the title screen loading the game scene on a click and a Quit button that ends the game through `game.quit()` (hidden where `game.canQuit()` is false, on iOS), and runs from its project folder and from the cooked bundle on Linux, Windows, macOS, the Galaxy S25 Ultra and the iPad, the golden capture matching and the HUD costing no more than 0.2 ms of GPU time on the S25 at 200 elements; and a second sample, a game module with a C++ component and system and a Lua script that reads it, builds with `SONNET_GAME` in CI, plays in the editor, and exports a custom player on all three desktops. CI is green on every job, and `tools/check_dependencies.py` still shows the player free of `ui` and `editor`.

The HUD budget's scene is `scenes/hud_stress.scene.json` in the basic sample ([gui.md](gui.md#the-sample)): its `overlay` pass takes 0.008 ms on an RTX 4090, 0.085 ms on the Galaxy S25 Ultra and 0.06 to 0.08 ms on the iPad under load, so the 0.2 ms budget is met. The hook landed in [PR #209](https://github.com/Pacheco95/sonnet/pull/209) ([player.md](player.md#game-modules)) and runs on every platform the player ships on. The per-device numbers and checks are in [the report](reports/roadmap-closed-work.md#m15-the-hud-budget-and-the-hooks-verification).

Done (version 0.19.0). The basic sample's menu, HUD and pause screen were checked on every platform: Options and Close, Play into the playground's HUD, Pause, Resume, Main menu, and Quit from the pause menu and from the title screen.

- **Evidence:** macOS ([#213](https://github.com/Pacheco95/sonnet/issues/213)), Windows ([#214](https://github.com/Pacheco95/sonnet/issues/214)), Linux, the Galaxy S25 Ultra and the iPad ([report](reports/ipad-m15.md)): [the report](reports/roadmap-closed-work.md#m15-the-hud-budget-and-the-hooks-verification). The S25 run found the Options popup off the screen in portrait, fixed by [PR #215](https://github.com/Pacheco95/sonnet/pull/215).

The menu golden matches in CI, and the HUD costs 0.085 ms on the S25 at 200 elements, under the 0.2 ms budget. The hook sample builds with `SONNET_GAME` in CI and plays the same in the editor and in `Game`. It exports a custom player that runs on all three desktops, and also runs on the S25 and the iPad. CI is green on every job, and `tools/check_dependencies.py` keeps the player free of `ui` and `editor`. The generic desktop player's refusal of a module's bundle was checked on Linux only. The next work is [M16](#m16-nested-scene-instances).

## M16: Nested scene instances

Accepted 2026-10-12. [ADR-0028](decisions/0028-nested-scene-instances.md) follows Godot's model of instancing, addressed by identity rather than by name path. It comes before terrain by the [1.0.0](#100) rule, since it changes the scene and prefab formats, and because a scene today drops what was changed inside a prefab instance without a warning ([world.md](world.md#prefabs)).

- `world`: an instance's descendants get identities derived from the instance and their source entity, so a scene can address them; a scene stores what a descendant changed (components, a rename, removed components) as `instanced` entries, compared by value, since flecs copies an instance's children; children added under an instance are saved; a source child is hidden with `Disabled` rather than deleted; prefabs nest to any depth and load in dependency order through one function the editor and the player share, which refuses cycles.
- Formats: the scene and prefab version becomes 4 and the bundle's 7.
- `editor`: "Save as prefab" from a selection, "Make local" on an instance, the inspector marking and reverting what an instance's descendant changed, and an instance's descendants editable in the hierarchy.
- `scripting`: `entity:child("Axle/Hub")`, a path of names from an entity.

Done when the playground has a prefab built from other prefabs, a scene that places it, changes one of its nested children, adds a child under another and hides a third, and keeps all three after a save and a reload in the editor, the player and the cooked bundle; a change to the inner prefab reaches what the scene did not change; undo restores a deleted instance with its changes; "Save as prefab" and "Make local" work in the editor; and CI is green on every job.

Done (version 0.20.0, [PR #218](https://github.com/Pacheco95/sonnet/pull/218)). The playground places `prefabs/pallet.prefab.json`, three instances of the crate prefab with Crate B's lid turned blue inside the prefab. The scene turns Crate A's lid red, two levels down, adds a lantern under Crate B and hides Crate C.

- **Evidence:** desktops (`runtime_tests`, `world_tests`, `editor_tests`) and both phones, the Galaxy S25 Ultra ([#221](https://github.com/Pacheco95/sonnet/issues/221)) and the iPad ([#222](https://github.com/Pacheco95/sonnet/issues/222)), are in [the report](reports/roadmap-closed-work.md#m16-what-was-checked). The phones found a disabled crate's lid still drawing; the draw list now leaves out a disabled entity's subtree.

CI is green on every job. Prefabs are not listed in the asset browser yet ([#219](https://github.com/Pacheco95/sonnet/issues/219)). Per-field overrides, starting with script properties, are [ADR-0028](decisions/0028-nested-scene-instances.md)'s first follow-up.

Not in the first version: per-field overrides, which come next, starting with script properties; placing a scene file; live updates of instances when a prefab changes; reordering or deleting an instance's source children; creating a prefab variant from the editor; applying an instance's changes back to its prefab.

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

Terrain, native file dialogs, incremental cooking. Nested scene instances moved into [M16](#m16-nested-scene-instances). The game UI and the game-module hook moved into [M15](#m15-game-ui-and-the-game-module-hook). Temporal anti-aliasing and particles moved into [M13](#m13-rendering-quality) and [M12](#m12-animation-and-effects).
