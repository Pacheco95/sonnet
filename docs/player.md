# Player

The player is the generic runtime an exported game ships as ([ADR-0007](decisions/0007-data-driven-game-structure.md)): one binary per platform that opens a project folder or a cooked bundle and runs its start scene. Two pieces, as the editor has two: `runtime` is the module, `apps/player` the executable that owns the window, device and swapchain.

## The runtime module

`sonnet/runtime/Game.h` is the whole public surface. `Game` owns the renderer, the render graph, the target it draws into, the asset database, the world, the input state and the subsystems — physics, scripts, animation and audio — in the same order the editor constructs them, for the same reason: they register into the world and must be destroyed before it, scripts after physics so their fixed update follows the physics step ([ADR-0009](decisions/0009-physics-and-scripting.md)), and the audio device last so it hears entities where the frame left them.

It is the editor's play mode with the editing removed. There is no edit mode to switch out of: the world is playing from the first frame, and `runtime` links neither `ui` nor `editor`.

Per frame, in the order `apps/player/main.cpp` calls them:

1. `event` for every translated platform event, which all becomes game input; there is no panel to compete for it, so the whole window is the game's.
2. `update(dt)` polls the database for changed sources (nothing in bundle mode), hands the audio device the camera as its fallback listener, runs `World::progress` — the fixed steps with physics and the scripts' `fixedUpdate`, the scripts' `update`, the animators, physics interpolation, the transform system, the skin palettes and the audio — and then builds the draw list, the light list, the sun and the environment from the world ([world.md](world.md#draw-list)).
3. `render(commands, swapchainImage)` resets the graph, sizes the target to the acquired image, declares the scene passes into it and the present pass into the swapchain image, and executes ([rendering.md](rendering.md#frame-structure)). Without an image — minimised, or a swapchain being recreated — the simulation still ran and nothing is drawn.
4. `afterPresent`, once the frame is submitted, writes the screenshot the frame copied, when a capture asked for one ([Capture runs](#capture-runs)).

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
./build/linux-debug/apps/player/sonnet_player apps/samples/basic   # a project folder
./build/linux-debug/apps/player/sonnet_player game.sbundle         # a cooked bundle
./sonnet_player                                                    # game.sbundle beside the binary
./sonnet_player --help                                             # the capture flags and the exit codes
```

Its command line is `sonnet_player [game] [capture flags]`, in any order. With no game it opens `game.sbundle` through `Platform::openContent`, from the content root: next to the binary on desktop, which is what an export writes, so an exported game starts by being double-clicked, and the APK's `assets/game.sbundle` on Android ([platform.md](platform.md#paths)). The game is the one argument that is not a flag, a path from the working directory, which the player makes absolute before opening it, since a relative path would be read from the content root. The flags make the run a capture ([Capture runs](#capture-runs)). A mistake in the arguments is logged, since a phone shows only the log, and ends the run with exit code 1 before a window opens. The shaders are content too, read from `shaders/` in the same root. The window closes on its close button or the platform's quit; everything else the window receives is the game's.

`SONNET_BUILD_PLAYER` builds it, on by default on every platform ([build.md](build.md#options)).

## Capture runs

The player writes a screenshot of its scene and quits, from the command line, as the editor does ([editor.md](editor.md#screenshots)), so that a run on a phone repeats and an agent that cannot see the screen can read it ([ADR-0018](decisions/0018-mobile-export.md)):

```bash
sonnet_player apps/samples/basic --scene scenes/playground.scene.json --play 3 --screenshot playground.png
sonnet_player --play 3 --shading-term albedo --screenshot albedo.png   # game.sbundle beside the binary
```

The flags are the editor's, with the editor's meaning, from one table and one parser in `runtime` (`sonnet/runtime/Capture.h`) that both applications use. The player takes five of the seven:

| Flag | Effect |
|---|---|
| `--screenshot FILE` | The scene, at the window's size, as a PNG. A relative path is under the preferences directory (below) |
| `--scene FILE` | Opens this scene instead of the start scene, relative to the project: a file of a project folder, or the scene of that path a bundle holds, since the cook keeps every scene under its project-relative path. Alone, without a screenshot, it makes a plain run of that scene |
| `--play SECONDS` | Plays for this many seconds, not steps, before the capture, and captures while playing: `--play 3` is 180 steps of 1/60 s |
| `--shading-term TERM` | One term of the forward shading instead of the final image: `final`, `albedo`, `normal`, `sun-direct`, `shadow-factor`, `ibl-diffuse`, `ibl-specular`, `brdf-lut` or `cascade`, in any case |
| `--settle-frames N` | Frames drawn after the assets have loaded, before playing or capturing; 10 by default |

`--screenshot-window` and `--select` are the editor's alone, since the player has no panels and no selection, and the player refuses them as unknown flags. A capture needs `--screenshot`, and any other flag without one is an error, apart from `--scene` alone, which runs that scene and captures nothing. The game is optional, as in a plain run. `sonnet_player --help` prints the list from the table, and `tools/check_docs.py` fails when a flag the player takes is missing here.

**The run** is the editor's, stepped through the player's game rather than the editor: the same `runtime::CaptureRun`, which the editor drives through `editor::CaptureRun` and the player through `runtime::GameCaptureTarget`. The game opens paused (`GameDesc::paused`), so the world does not simulate while the run waits for the assets, giving up after two minutes, and draws the settle frames. The run then seeds the scripts' `math.random` with a fixed value and plays (`Game::play`) at a fixed 1/60 s a frame whatever the frame rate, and the next frame copies the scene out (`Game::requestScreenshot`, written by `afterPresent`, reported by `takeScreenshotResult`). The image is the game's render target after tone mapping, as the present pass reads it: opaque, at the window's size. Without `--play` it is the scene as it loads, unsimulated, as in the editor. The same flags give the same image run after run on one GPU; two runs of the playground with `--play 3` on the RTX 4090 were byte-identical. The exit code is 0 once the file is written and 1 on any failure, which the log says.

The editor's image of the same flags is not the player's. The editor draws through its fly camera into its viewport, 968×662 in its 1600×900 window, and the player through the scene's first camera into its whole window, 1280×720 on the desktop and 1080×2340 on the phone in portrait. Only the simulation they capture is the same.

**Where the file goes.** A relative `--screenshot` resolves against `Platform::prefPath("sonnet", "player")`, the one directory a phone lets the player write: `~/.local/share/sonnet/player/` on Linux, `%APPDATA%\sonnet\player\` on Windows, `~/Library/Application Support/sonnet/player/` on macOS, and the app's internal storage, `files/` in its data directory, on Android, where SDL ignores the two names. An absolute path is written where it says.

**What the log says.** Besides the usual lines, a capture run logs:

- `capture run on "Adreno (TM) 830", Vulkan 1.3.284, writing /data/data/io.github.pacheco95.sonnet/files/final.png`: the device, its Vulkan version and the file.
- `screenshot 1080x2340 written to ...` once it is.
- `capture frame times over the last 100 frames: CPU 2.04 ms a frame, 5.27 ms apart; GPU 4.961 ms: skinning 0.002 ms, cull 0.008 ms, ...`: over the last hundred frames before the one that copies the screenshot, or fewer when the run drew fewer, the mean CPU time of a frame, the mean time between frames, and each render-graph pass's mean GPU time with their sum ([rendering.md](rendering.md#render-graph)). The CPU time is the simulation and the recording, without the waits for a frame slot and a swapchain image, which are the GPU's and the display's. The frame that copies the screenshot is left out, since that copy is the capture's work, not the game's.
- `capture failed: <reason>` when it fails.
- The last line, `exit ok` for exit code 0 or `exit with failure` for 1. On a phone nobody sees the exit code, and this line is how the run reports it.

**On a phone.** The arguments go in the `args` extra, split on whitespace ([Running on Android](#running-on-android)). The whole `am start` is quoted once for `adb shell`, so the phone's shell keeps the extra as one string, and `-S` stops a player that is running first, since an activity already running takes no new arguments. The file is in the app's `files/`, which `run-as` reads, byte for byte through `adb shell` or `adb exec-out`:

```bash
adb logcat -c
adb shell "am start -S -W --user 0 -n io.github.pacheco95.sonnet/.SonnetActivity --es args '--play 3 --shading-term albedo --screenshot albedo.png'"
adb logcat -d -s Sonnet | tail -3        # wait for "exit ok" or "exit with failure"
adb shell run-as io.github.pacheco95.sonnet cat files/albedo.png > albedo.png
adb shell run-as io.github.pacheco95.sonnet rm files/albedo.png
```

A packaged bundle's other scenes are a flag away: `--es args '--scene scenes/playground.scene.json --play 3 --screenshot playground.png'`. Keep the phone's screen on and the player in front during a run: on Android nothing iterates in the background ([The lifecycle](#the-lifecycle)), so a run that loses the foreground waits and carries on when it comes back rather than failing. Only the two minutes it may wait for the assets count time away. Settings > Developer options > Stay awake, or `adb shell svc power stayon usb`, keeps the screen on while the phone is plugged in.

### Reporting a device run

ADR-0018 decides what counts as running on a device and how a device's agent reports it. On Android, with the `android-debug` or `android-release` APK and the basic sample cooked for `android` and packaged ([Running on Android](#running-on-android)):

1. **The Vulkan description.** `adb shell cmd gpu vkjson`, checked against every feature and limit in [rendering.md](rendering.md#vulkan-baseline); a 1.3 device takes [ADR-0019](decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s path, which the device's log line names.
2. **The launcher.** The app starts from its icon, or from `adb shell monkey -p io.github.pacheco95.sonnet -c android.intent.category.LAUNCHER 1`, which sends the launcher's intent, and draws the start scene (`adb exec-out screencap -p`).
3. **Capture runs** of the start scene with `--play 3` for the `final`, `albedo` and `normal` shading terms, and one of the playground (`--scene scenes/playground.scene.json --play 3`), each ending in `exit ok` with no error and no warning in the log. The debug APK's warning that the validation layer is missing is the one exception, since a phone has no layer.
4. **The background** and back, by hand ([The lifecycle](#the-lifecycle)).
5. **A finger** held on the playground, by hand ([scripting.md](scripting.md#input)).
6. **The frame times** of step 3, from the capture runs' logs, recorded in the roadmap beside the desktop's and the Mac's.

The report returns the PNGs, the logs (`adb logcat -d -s Sonnet`, and `adb logcat -d -b crash`, which should be empty), the Vulkan description, the device model and OS version (`adb shell getprop ro.product.model` and `ro.build.version.release`), and one line per check saying pass or fail, and for checks 4 and 5 who did them. The PNGs are compared by eye with a Linux capture of the same arguments, not byte for byte, since the GPUs differ and the phone's window is portrait. Serial numbers, build fingerprints and home paths stay out of it.

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

A desktop bundle runs on the phone too, with its UASTC transcoded on load ([Opening a game](#opening-a-game)). The device's log line says `ASTC` when the device has it, and each texture's debug line names its format. A capture run on the phone is in [Capture runs](#capture-runs).

A default cook puts every scene of the project in the bundle, so the packaged bundle runs another of them with `--scene` in `args`, rather than a cook with `sonnet_cook --scene`, which makes a bundle of that scene alone. Fingers are the game's through `input.touches()` ([scripting.md](scripting.md#input)), and `adb` can send them. `input motionevent` sends one event at a time, so a finger stays down between a `DOWN` and its `UP` for as long as the commands take, while `input swipe` is a timed drag and `input tap` a tap. Coordinates are the screen's pixels, which are the window's in a full-screen app:

```bash
adb shell "am start -S --user 0 -n io.github.pacheco95.sonnet/.SonnetActivity --es args '--scene scenes/playground.scene.json'"
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
