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

A bundle is refused before anything in it loads when the device cannot sample the textures its platform was cooked with, and the error names the platform. A desktop bundle (`windows`, `linux`, `macos`) runs on every device: its compressed textures are UASTC, which `readKtx2` transcodes to BC7, ASTC 4×4 or RGBA8, whichever the device has, and the rest are RGBA8 and the environment's RGBA16F, which every device samples. An Android or iOS bundle holds ASTC, uploaded as it is, so it needs `DeviceInfo::astcSupported` ([assets.md](assets.md#textures)). Phones and Apple silicon have ASTC and desktop GPUs do not, so a desktop bundle runs on a phone and a phone's bundle does not run on a desktop. `assets::canRun` is the rule, and the null device, which reports BC and no ASTC like a desktop GPU, is what `runtime_tests` checks it on. A texture a bundle holds in a format the device lacks, such as a KTX2 source cooked as it is in BC7, fails on its own when it loads and falls back like a failed import.

Either way every model is loaded as a prefab under its own identity, so a scene can place one ([world.md](world.md#prefabs)), and the window takes the project's name as its title. A failure to open leaves an empty world and returns the error; the application stops rather than showing an empty scene.

`runtime_tests` runs the basic sample headless on Lavapipe, as a project folder and then cooked into a bundle, and checks that both load the same scene, that the scene and present passes run, that a scene without a camera falls back, and that neither path logs a warning — including past the game's destruction, where a bundle that released nothing would show up as the renderer's leak warnings.

## The player application

`apps/player/main.cpp` owns the window, device and swapchain and calls the four steps in order, like `apps/editor/main.cpp` ([architecture.md](architecture.md#application-lifecycle)). It runs:

```bash
./build/linux-debug/apps/player/sonnet_player samples/basic   # a project folder
./build/linux-debug/apps/player/sonnet_player game.sbundle    # a cooked bundle
./sonnet_player                                               # game.sbundle beside the binary
```

With no argument it opens `game.sbundle` through `Platform::openContent`, from the content root: next to the binary on desktop, which is what an export writes, so an exported game starts by being double-clicked, and the APK's `assets/game.sbundle` on Android ([platform.md](platform.md#paths)). An argument is a path from the working directory, which the player makes absolute before opening it, since a relative path would be read from the content root. The shaders are content too, read from `shaders/` in the same root. The window closes on its close button or the platform's quit; everything else the window receives is the game's.

`SONNET_BUILD_PLAYER` builds it, on by default on every platform ([build.md](build.md#options)).

## The lifecycle

A phone sends the player to the background and brings it back, and the player handles the events `platform` translates for it ([platform.md](platform.md#background-and-foreground)), between two frames:

- **`WillEnterBackground`**: it waits for the device to go idle, suspends the swapchain, which releases the surface the OS is taking away ([rendering.md](rendering.md#suspend-and-resume)), and pauses the audio ([audio.md](audio.md#pausing)).
- **`DidEnterForeground`**: it resumes the swapchain at the window's size and then the audio, and restarts its frame clock. If the swapchain cannot be created again, the error is logged and the player ends, since it has nothing left to show.
- **`LowMemory` and `Terminating`** are logged at `info`, and that is all: the player holds nothing it could give back, and SDL ends the loop after `Terminating`, whose quit tears the player down as a quit does.

Each step logs at `info`, and coming back says how long the player was away and how many frames ran meanwhile: `back from the background after 64.8 s, 0 frames in it`.

**Time away is not simulated.** A frame's `dt` is the time since the previous frame, clamped to 0.1 s, and the world runs at most four fixed steps a frame ([world.md](world.md#phases-and-play-mode)). Without the clock restarting, the first frame back would have simulated 0.1 s whatever the time away, of which the world keeps four steps. With it, that frame simulates its own time. On Android nothing iterates in the background at all, so the game stops where it was. A ball the player sent to the background at the top of a jump, and brought back 65 s later, was at the same height on its first frame back, 15 ms of game time later, and landed 0.65 s of game time after that. The sound stops while away, and a sound carries on from where it stopped.

The events do not arrive on the desktop, where minimising the window is what stops the drawing, as it always did. The editor has no lifecycle handling, since it runs only on the desktop. `runtime_tests` plays the basic sample through the player's steps on Lavapipe, since the handling itself lives in `apps/player/main.cpp`, which no test reaches: three frames, the swapchain suspended and the audio paused, three frames that simulate but acquire nothing and mix nothing, then both resumed and three frames that draw into the present pass again, with nothing warned about.

## Running on Android

On Android the player is `libsonnet_player.so` inside `sonnet_player.apk`, which the `android-debug` and `android-release` presets build ([build.md](build.md#android)). The APK's package is `io.github.pacheco95.sonnet` and its one activity is `io.github.pacheco95.sonnet.SonnetActivity`, in `apps/player/android/` with the manifest and the icon. The activity extends SDL's `SDLActivity`. SDL is linked into the player statically, so `getLibraries()` names the player alone, and SDL's Java side loads it and calls its `SDL_main` on its own thread. The manifest requires Vulkan 1.3 (`android.hardware.vulkan.version` `0x403000`), since [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) accepts a 1.3 device with the engine's 1.4 features as extensions, and the device selector checks those features when the player starts. Android 16 (API 36) is the minimum and the target.

The player's arguments come from the launch intent's string extra `args`, split on whitespace, with double quotes keeping an argument with spaces together. `adb shell` hands its command to the device's shell, which removes one level of quoting, so an argument with spaces needs two:

```bash
adb install -r build/android-debug/apps/player/sonnet_player.apk
adb shell am start -n io.github.pacheco95.sonnet/.SonnetActivity                   # no arguments
adb shell am start -n io.github.pacheco95.sonnet/.SonnetActivity --es args /data/user/0/io.github.pacheco95.sonnet/files/game.sbundle
adb shell "am start -n io.github.pacheco95.sonnet/.SonnetActivity --es args '\"my game.sbundle\" --flag'"
adb logcat -s Sonnet SDL                                                           # the arguments and the engine's log
adb shell run-as io.github.pacheco95.sonnet ls                                     # the app's data, in a debuggable APK
```

On a phone with a second user profile (a Samsung Secure Folder, for one), add `--user 0` to `adb install` and `am start`. The activity logs the arguments it passes under the `Sonnet` tag, and the engine's own log goes to logcat under the same tag from its first line ([platform.md](platform.md#logging)).

With no argument the player runs the APK's `assets/game.sbundle`, which is there when the APK was built with `SONNET_ANDROID_BUNDLE` ([build.md](build.md#android)), and it reads its shaders from `assets/shaders/`. An argument must be an absolute path, since the working directory of an Android app is `/`. That runs a bundle pushed into the app's data directory:

```bash
adb push game.sbundle /data/local/tmp/
adb shell run-as io.github.pacheco95.sonnet cp /data/local/tmp/game.sbundle files/
adb shell am start --user 0 -n io.github.pacheco95.sonnet/.SonnetActivity \
  --es args /data/user/0/io.github.pacheco95.sonnet/files/game.sbundle
adb logcat -s Sonnet
```

A game for the phone is cooked for `android`, which encodes its compressed textures as ASTC ([assets.md](assets.md#textures)), and packaged into the APK at configure ([build.md](build.md#android)):

```bash
./build/linux-debug/apps/cook/sonnet_cook apps/samples/basic --platform android --out build/android-bundle
cmake --preset android-debug -DSONNET_ANDROID_BUNDLE=$PWD/build/android-bundle/game.sbundle
cmake --build --preset android-debug
adb uninstall --user 0 io.github.pacheco95.sonnet   # when the installed APK came from the other build directory
adb install --user 0 build/android-debug/apps/player/sonnet_player.apk
```

A desktop bundle runs on the phone too, with its UASTC transcoded on load ([Opening a game](#opening-a-game)). The device's log line says `ASTC` when the device has it, and each texture's debug line names its format. The capture is a later M9 step ([roadmap.md](roadmap.md#the-mobile-lifecycle)).

The player takes no `--scene` yet, so a bundle that starts on another scene is cooked with `sonnet_cook --scene`. Fingers are the game's through `input.touches()` ([scripting.md](scripting.md#input)), and `adb` can send them. `input motionevent` sends one event at a time, so a finger stays down between a `DOWN` and its `UP` for as long as the commands take, while `input swipe` is a timed drag and `input tap` a tap. Coordinates are the screen's pixels, which are the window's in a full-screen app:

```bash
./build/linux-debug/apps/cook/sonnet_cook apps/samples/basic --platform android --out build/android-bundle \
    --scene scenes/playground.scene.json
adb shell input motionevent DOWN 540 1500   # a finger held on the playground
adb shell input motionevent MOVE 560 1450
adb shell input motionevent UP 560 1450
adb shell input swipe 540 1500 900 1200 2000   # from one point to another over 2 s
adb exec-out screencap -p > frame.png
```

`adb` sends the player to the background and back ([The lifecycle](#the-lifecycle)) the ways a hand does. `adb logcat -v threadtime -s Sonnet SDL` shows the engine's lines between SDL's Java side's (`surfaceDestroyed()`, `nativePause()`, `nativeResume()`), and `adb logcat -d -b crash` has any native crash:

```bash
adb shell input keyevent KEYCODE_HOME                     # the home button
adb shell am start --user 0 -n io.github.pacheco95.sonnet/.SonnetActivity   # back
adb shell input keyevent KEYCODE_APP_SWITCH               # recents; a second press switches to the previous app
adb shell input keyevent KEYCODE_SLEEP                    # screen off
adb shell input keyevent KEYCODE_WAKEUP                   # screen on: the lock screen, if the phone has one
adb logcat -s Sonnet | grep -E "background|swapchain|audio"
```

A second `KEYCODE_APP_SWITCH` goes to the application used before the one in front, so from the player it opens another app and the next pair comes back. After `KEYCODE_WAKEUP` a locked phone stays on its lock screen, with the player in the background, until it is unlocked by hand.

## What an export is

An exported game is a directory holding the player binary for the target, the `shaders/` folder of compiled engine shaders, the runtime libraries the platform needs beside a binary, and one `.sbundle`. An Android or iOS export is the bundle alone: the player there is an APK or an app bundle, which the build packages the bundle into ([Running on Android](#running-on-android)), and the editor builds neither. Nothing else: no project folder, no importers, no compiler, no SDK ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)). macOS falls short of that today: an exported game there needs the Vulkan SDK installed, because the export carries no Vulkan driver ([roadmap.md](roadmap.md#the-macos-export-needs-the-vulkan-sdk)). The editor's export dialog assembles one ([editor.md](editor.md#export)); `sonnet_cook` writes the bundle half on its own ([assets.md](assets.md#cooking-and-export)).

## See also

- [Architecture](architecture.md), for the editor and player split
- [Assets](assets.md), for cooking and the bundle format
- [Editor](editor.md#export), for the export dialog and what play mode shares with this
- [Rendering](rendering.md), for the passes and the present pass
