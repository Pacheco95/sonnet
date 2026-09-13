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

Done when a glTF sample scene renders with shadows and IBL and the performance targets in the README are measured for the first time.

## M4: Physics and scripting

- `physics`: `IPhysicsWorld` with Jolt: static, dynamic and kinematic bodies, box, sphere, capsule and mesh shapes, raycasts, debug draw.
- `scripting`: `IScriptRuntime` with Lua and sol2: entity and component access through reflection, input, logging, hot reload.
- `editor`: physics and script components in the inspector, physics running in play mode only.

Done when a sample level has scripted behaviour and physics that runs in play mode and resets on stop.

## M5: Audio and animation

- `audio`: `IAudioDevice`, sources, listener, mixing with miniaudio or SDL3 audio.
- Skeletal animation: skins and clips from glTF, animation player component, GPU skinning.

Done when the animated glTF samples play with sound in the editor and the player.

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
