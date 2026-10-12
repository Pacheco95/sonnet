# Issue 196: iOS text verification

Validated `feat/gui-text` at `1bf3cdb` on 2026-10-12. iPhone, Apple A17 Pro, iOS 27.0, MoltenVK 1.4.2, Vulkan 1.4.357. Signed `ios-release` build (RelWithDebInfo), native 1290×2796 output, default ten settle frames. No engine edits, commits or merge.

1. **PASS — Menu.** `menu.png` shows crisp white Play, Options and Quit labels centred in their buttons. No solid blocks or missing glyphs. The throwaway sample uses the same font subset, with preload reduced to `[[32,64]]` for the HUD check; menu letters therefore also exercise first-use rasterisation.
2. **PASS — Same-frame text.** `label-before.png` shows Score 0, and `label-after.png` shows fully formed Score 12. The fixture comes from `labelProject` in runtime tests, with its scene added to the throwaway basic project and its font reference using the sample font. Ten and eleven fixed play steps precede the screenshot's next simulation frame; the latter captures update 12.
3. **INCONCLUSIVE — Full performance acceptance; PASS for first-frame glyph appearance.** The supplied HUD creates 200 elements, 67 with text, and changes the first label at update 250. `hud-frame249.png` shows 0:00; `hud-frame250.png` shows fully formed Hello World on the first update that uses those letters. The font preload is `[[32,64]]`, so those letters were not preloaded. The exact fixture's portrait layout extends off the right edge, and the long first label overlaps the next rectangle; these are layout properties, not missing glyphs. No layout adjustment was made.

The six required A/B captures each completed successfully. A uses `--play 3`, B uses `--play 5`; each timing line covers the last 100 frames. Runs alternate A/B, with at least 45 seconds of idle cooldown between stress runs. All started and ended nominal, with worst thermal state nominal. “Cold” here means a fresh app process after idle cooldown and nominal thermal state; no physical temperature sensor reading was taken.

| Run | CPU/frame (ms) | Between frames (ms) | GPU total | overlay | Thermal |
|---|---:|---:|---|---|---|
| A1 | 5.82 | 18.85 | n/a | n/a | nominal |
| B1 | 5.81 | 19.00 | n/a | n/a | nominal |
| A2 | 5.75 | 19.01 | n/a | n/a | nominal |
| B2 | 5.87 | 19.01 | n/a | n/a | nominal |
| A3 | 5.73 | 18.96 | n/a | n/a | nominal |
| B3 | 5.88 | 19.13 | n/a | n/a | nominal |
| A mean | 5.77 | 18.94 | n/a | n/a | nominal |
| B mean | 5.85 | 19.05 | n/a | n/a | nominal |

Every GPU pass in the stress runs reports unavailable timestamps, matching the existing [issue 152](https://github.com/Pacheco95/sonnet/issues/152). The 0.2 ms overlay budget therefore remains unverified. B minus A is approximately 0.11 ms in the 100-frame average. One missed frame would contribute approximately 0.19 ms; the run-to-run spread and three repetitions cannot prove or exclude such a miss. The direct update-250 PNG proves same-frame glyph visibility, not uninterrupted display cadence. No GPU readback changes or blocking timestamp waits were introduced.

The menu run had available GPU timestamps: overlay 0.428 ms, GPU total 10.291 ms, between frames 18.08 ms, nominal thermal state. This is a different scene and a short startup-inclusive capture, not the HUD budget measurement.

Capture logs contain the known MoltenVK R32_UINT blending warning ([issue 59](https://github.com/Pacheco95/sonnet/issues/59)). The label fixture also logs its expected no-Camera fallback warning. No other warning/error category or text-related error was found. Device captures use the bundled MoltenVK path and do not establish validation-layer silence. No new failure was identified beyond the existing timestamp limitation; no new issue was opened.

See `recipe.md` and `fixtures/` for reproduction. PNGs and redacted capture logs are included in `artifacts.zip`. Private build/device records and the device-specific helper are excluded. The checkout remains clean on `feat/gui-text`. Issue 196 is not present on the private project board, so no board status was changed.
