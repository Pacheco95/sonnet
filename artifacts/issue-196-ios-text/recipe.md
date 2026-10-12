# iOS text validation recipe

Source: `feat/gui-text` at `1bf3cdb`. No source changes.

Copy `apps/samples/basic` to a throwaway directory. Replace its stress script with the included `stress.lua`, add `label.lua` and its sidecar under `assets`, and add `label.scene.json` under `scenes`. Set the sample font's sidecar preload to `[[32,64]]`; keep its existing UUID. The label uses the same font UUID and the test's script UUID.

Build the desktop cook tool with `macos-release-local`. Cook the throwaway project with `--platform ios`. Configure `ios-release` with `SONNET_IOS_BUNDLE` pointing to the resulting `game.sbundle`. Use Apple Clang for the vcpkg compiler environment (`CC`/`CXX`), and the machine's existing signing configuration. Build with `-allowProvisioningUpdates -allowProvisioningDeviceRegistration`.

Install the resulting `RelWithDebInfo-iphoneos/sonnet_player.app` using `xcrun devicectl device install app`. Run each capture with `xcrun devicectl device process launch --terminate-existing --console --device <device> io.github.pacheco95.sonnet`, followed by:

| Capture | Arguments |
|---|---|
| Menu | `--scene scenes/menu.scene.json --play 1 --screenshot menu.png` |
| Label before | `--scene scenes/label.scene.json --play 0.166666667 --screenshot label-before.png` |
| Label after | `--scene scenes/label.scene.json --play 0.183333333 --screenshot label-after.png` |
| HUD A | `--scene scenes/stress.scene.json --play 3 --screenshot hud-A1.png` |
| HUD B | `--scene scenes/stress.scene.json --play 5 --screenshot hud-B1.png` |

Repeat A/B three times, using separate output filenames, with at least 45 seconds idle between stress runs. Default ten settle frames; native 1290×2796 render size. Retrieve each PNG through `devicectl device copy from`, using the appDataContainer domain and source `Library/Application Support/sonnet/player/<capture>.png`. Run device tools outside the sandbox. Redact personal and hardware identifiers before sharing logs.

The supplied HUD has 200 entities, 67 with text, and changes label 1 to `Hello World` at update 250. Its reference layout is wider than the portrait viewport: right columns extend beyond the captured area; all entities are still laid out and submitted. No layout or render-size adjustment was made to the issue's fixture.

Additional first-use captures use `--play 4.133333333` and `--play 4.15`, with filenames `hud-frame249.png` and `hud-frame250.png`, respectively. The screenshot simulation update is 249 and 250. These diagnostic runs are separate from the cooled A/B timing set.
