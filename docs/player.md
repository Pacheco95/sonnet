# Player

The player is the generic runtime an exported game ships as ([ADR-0007](decisions/0007-data-driven-game-structure.md)): one binary per platform that opens a project folder or a cooked bundle and runs its start scene. Two pieces, as the editor has two: `runtime` is the module, `apps/player` the executable that owns the window, device and swapchain.

## The runtime module

`sonnet/runtime/Game.h` is the whole public surface. `Game` owns the renderer, the render graph, the target it draws into, the asset database, the world, the input state and the subsystems — physics, scripts, animation and audio — in the same order the editor constructs them, for the same reason: they register into the world and must be destroyed before it, scripts after physics so their fixed update follows the physics step ([ADR-0009](decisions/0009-physics-and-scripting.md)), and the audio device last so it hears entities where the frame left them.

It is the editor's play mode with the editing removed. There is no edit mode to switch out of: the world is playing from the first frame, and `runtime` links neither `ui` nor `editor`.

Per frame, in the order `apps/player/main.cpp` calls them:

1. `event` for every translated platform event, which all becomes game input; there is no panel to compete for it, so the whole window is the game's.
2. `update(dt)` polls the database for changed sources (nothing in bundle mode), hands the audio device the camera as its fallback listener, runs `World::progress` — the fixed steps with physics and the scripts' `fixedUpdate`, the scripts' `update`, the animators, physics interpolation, the transform system, the skin palettes and the audio — and then builds the draw list, the light list, the sun and the environment from the world ([world.md](world.md#draw-list)).
3. `render(commands, swapchainImage)` resets the graph, sizes the target to the acquired image, declares the scene passes into it and the present pass into the swapchain image, and executes ([rendering.md](rendering.md#frame-structure)). Without an image — minimised, or a swapchain being recreated — the simulation still ran and nothing is drawn.

The scene is drawn through its first `Camera` ([world.md](world.md#components)). A scene with none is drawn from a fallback view above and to the side of the origin, warned about once rather than every frame, so a scene that forgot a camera shows something rather than nothing.

## Opening a game

`Game::open` takes either, and decides by what the path is:

- A **project folder** is opened through `assets::Project`, its asset roots scanned, its `.prefab.json` files loaded, and its `startScene` read as JSON. Sources are polled for changes as the editor polls them, so running a project folder is a usable way to try a change without the editor.
- A **bundle** is opened through `AssetDatabase::openBundle`; the prefabs and the start scene are the CBOR file entries the cook put in it ([assets.md](assets.md#the-bundle)). There are no sidecars, no re-import and no hot reload.

Either way every model is loaded as a prefab under its own identity, so a scene can place one ([world.md](world.md#prefabs)), and the window takes the project's name as its title. A failure to open leaves an empty world and returns the error; the application stops rather than showing an empty scene.

`runtime_tests` runs the basic sample headless on Lavapipe, as a project folder and then cooked into a bundle, and checks that both load the same scene, that the scene and present passes run, that a scene without a camera falls back, and that neither path logs a warning — including past the game's destruction, where a bundle that released nothing would show up as the renderer's leak warnings.

## The player application

`apps/player/main.cpp` owns the window, device and swapchain and calls the four steps in order, like `apps/editor/main.cpp` ([architecture.md](architecture.md#application-lifecycle)). It runs:

```bash
./build/linux-debug/apps/player/sonnet_player samples/basic   # a project folder
./build/linux-debug/apps/player/sonnet_player game.sbundle    # a cooked bundle
./sonnet_player                                               # game.sbundle beside the binary
```

With no argument it looks for `game.sbundle` next to itself, which is what an export writes, so an exported game starts by being double-clicked. The window closes on its close button or the platform's quit; everything else the window receives is the game's.

`SONNET_BUILD_PLAYER` builds it, on by default on every platform ([build.md](build.md#options)).

## Running on Android

On Android the player is `libsonnet_player.so` inside `sonnet_player.apk`, which the `android-debug` and `android-release` presets build ([build.md](build.md#android)). The APK's package is `io.github.pacheco95.sonnet` and its one activity is `io.github.pacheco95.sonnet.SonnetActivity`, in `apps/player/android/` with the manifest and the icon. The activity extends SDL's `SDLActivity`. SDL is linked into the player statically, so `getLibraries()` names the player alone, and SDL's Java side loads it and calls its `SDL_main` on its own thread. The manifest requires Vulkan 1.3 (`android.hardware.vulkan.version` `0x403000`), since [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) accepts a 1.3 device with the engine's 1.4 features as extensions, and the device selector checks those features when the player starts. Android 16 (API 36) is the minimum and the target.

The player's arguments come from the launch intent's string extra `args`, split on whitespace, with double quotes keeping an argument with spaces together. `adb shell` hands its command to the device's shell, which removes one level of quoting, so an argument with spaces needs two:

```bash
adb install -r build/android-debug/apps/player/sonnet_player.apk
adb shell am start -n io.github.pacheco95.sonnet/.SonnetActivity                   # no arguments
adb shell am start -n io.github.pacheco95.sonnet/.SonnetActivity --es args game.sbundle
adb shell "am start -n io.github.pacheco95.sonnet/.SonnetActivity --es args '\"my game.sbundle\" --flag'"
adb logcat -s Sonnet SDL                                                           # the arguments as the activity passed them
adb shell run-as io.github.pacheco95.sonnet ls                                     # the app's data, in a debuggable APK
```

On a phone with a second user profile (a Samsung Secure Folder, for one), add `--user 0` to `adb install` and `am start`. The activity logs the arguments it passes under the `Sonnet` tag. The engine's own log does not reach logcat yet, and the player cannot yet read a bundle or shaders from the APK: `Platform::openContent`, ASTC cooking, touch input, the lifecycle and the capture are later M9 steps ([roadmap.md](roadmap.md#the-android-apk)).

## What an export is

An exported game is a directory holding the player binary for the target, the `shaders/` folder of compiled engine shaders, the runtime libraries the platform needs beside a binary, and one `.sbundle`. Nothing else: no project folder, no importers, no compiler, no SDK ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)). macOS falls short of that today: an exported game there needs the Vulkan SDK installed, because the export carries no Vulkan driver ([roadmap.md](roadmap.md#the-macos-export-needs-the-vulkan-sdk)). The editor's export dialog assembles one ([editor.md](editor.md#export)); `sonnet_cook` writes the bundle half on its own ([assets.md](assets.md#cooking-and-export)).

## See also

- [Architecture](architecture.md), for the editor and player split
- [Assets](assets.md), for cooking and the bundle format
- [Editor](editor.md#export), for the export dialog and what play mode shares with this
- [Rendering](rendering.md), for the passes and the present pass
