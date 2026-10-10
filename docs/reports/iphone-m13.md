# M13 rendering verification on iPhone

Verification of [issue 140](https://github.com/Pacheco95/sonnet/issues/140), 2026-10-09, at engine commit `d07d3a1`. GPU: Apple A17 Pro; iOS 27.0; MoltenVK 1.4.2, Vulkan 1.4.357. The player was built with `ios-debug`, Apple Clang from Xcode 26.2, and the existing local development certificate. The basic sample was freshly cooked with `sonnet_cook --platform ios`.

## Results

TAA, depth-pyramid occlusion culling, survivor readback and hashed-alpha shadows ran successfully on the Apple GPU. ASTC HDR sampling also passed a diagnostic run, but the issue's expected automatic ASTC HDR selection did not occur: this MoltenVK device reports BC support too, so the unchanged loader chooses BC6H. [Issue 143](https://github.com/Pacheco95/sonnet/issues/143) tracks the remaining automatic-selection case on a device that genuinely reports ASTC HDR without BC.

| Check | Result | Evidence |
|---|---|---|
| ASTC and ASTC HDR, without BC | Expected capability combination not reproduced | `blockCompressionSupported`, `bc6hSupported`, `astcSupported` and `astcHdrSupported` are all `true`; the ordinary device line says `BC, ASTC, ASTC HDR` |
| Cooked iOS bundle opens | Pass | Original basic sample: 39 assets, five scenes and prefabs, zero cook warnings; capture exits `ok` |
| HDR environment | Pass for BC6H and diagnostic ASTC HDR | Normal target `BC6HUfloat`; temporarily preferring ASTC selects `ASTC4x4Sfloat`, with the encoded blocks unchanged |
| TAA | Pass for the exercised sample | Resolve pass executes; `temporalHistoryUsed=true`; repeated captures differ by at most one RGB level, with no visible trailing silhouette in the spinning-box capture |
| Occlusion culling and counters | Pass | The fixture keeps 2 of 27 draws and 14 of 314 triangles; disabling occlusion keeps all 27 draws and 314 triangles, with pixel-identical output |
| Hashed-alpha shadows | Pass | The same ground region darkens progressively at blended alpha 0, 0.5 and 1 |
| Frame time and thermals | Frame times recorded; thermals not measured | Short capture runs, not a sustained thermal test |

All captures are the player's own scene PNGs, retrieved from its app data container with `devicectl`. The iPhone output is 1290×2796. [Sanitized evidence](iphone-m13-captures/evidence.txt) includes device capabilities, selected HDR formats, history and survivor counters, per-pass timings and successful exits. [Comparisons and PNG hashes](iphone-m13-captures/comparisons.json) preserve the measured RGB errors. Report artifacts omit device and team identifiers, container paths, serial numbers and personal names.

The diagnostic builds changed only the capture settings and logging, exposed the renderer's existing statistics, and, for the ASTC run, swapped the HDR target preference. The [patches and fixture files](#reproducing-the-diagnostic-runs) are retained as report artifacts; all temporary engine edits were restored after the runs. No production preference rule changes are proposed.

## HDR environment and image-based lighting

The original [basic-sample capture](iphone-m13-captures/main.png) has the expected sky, diffuse lighting, reflections and shadows. The [macOS reference](iphone-m13-captures/macos-main.png), taken from the same iOS bundle with the same `--play 3` arguments on Apple M4 Max, is visually consistent in the overlapping scene. Its landscape aspect ratio differs, so it is not a pixel-equality reference.

The diagnostic [BC6H frame](iphone-m13-captures/main-diag.png) and [ASTC HDR frame](iphone-m13-captures/main-astc.png) use the same phone, scene, simulation and renderer settings. The ASTC target's data size and every decompressed encoded block match the KTX payload before the library's ASTC-target call: no block conversion or RGBA fallback occurred.

| Same-phone comparison | Mean absolute RGB difference, 0–255 | Largest channel difference |
|---|---:|---:|
| BC6H versus ASTC HDR, final frame | 0.028944 | 4 |
| [BC6H diffuse IBL](iphone-m13-captures/main-bc6h-ibl.png) versus [ASTC HDR diffuse IBL](iphone-m13-captures/main-astc-ibl.png) | 0.038326 | 1 |
| [BC6H specular IBL](iphone-m13-captures/main-bc6h-specular.png) versus [ASTC HDR specular IBL](iphone-m13-captures/main-astc-specular.png) | 0.029402 | 1 |

The final-frame difference is consistent with the Galaxy S25 Ultra result reported in [PR 137](https://github.com/Pacheco95/sonnet/pull/137): 0.03 levels on average, 4 at worst. That PR also records the Linux comparison against an uncompressed environment. Matching Linux and Android PNGs were not available locally, so this run compares the iPhone formats directly and uses macOS for the visual reference; it does not claim a fresh pixel comparison against either other platform.

## TAA and occlusion

The default `--play 3` capture advances 180 fixed simulation steps after the default ten settle frames. [Repeating it](iphone-m13-captures/main-repeat.png) gives a mean absolute RGB error of 0.0000125 and a largest channel error of 1. The diagnostic build preserves the original frame to the same one-level limit. The `taa` GPU pass and `temporalHistoryUsed=true` confirm that the resolve used history, rather than merely creating its pipeline. The [TAA-disabled reference](iphone-m13-captures/shadow-main-no-taa.png) uses the same simulated scene. No persistent displaced silhouette is visible around the spinning box in the resolved image. This is a sample-level visual check, not a phone port of the desktop automated ghost-trail or camera-cut tests.

The [basic sample without occlusion](iphone-m13-captures/main-nocull.png) differs from its culling-enabled diagnostic capture by 0.0000083 RGB levels on average and 1 at most. Both report 12 opaque/masked survivors and 3,280 surviving triangles; the sample's submitted totals also include blended draws, which the survivor counters exclude.

The [occlusion fixture](iphone-m13-fixtures/scenes/occlusion.scene.json) places 25 small boxes behind a large opaque wall, with a ground plane and a fixed camera. It settles for 64 frames with TAA disabled to make the culling comparison exact. With occlusion on, the readback reports `known=true`, 2 visible draws and 14 visible triangles. With occlusion off, it reports 27 and 314. The [enabled](iphone-m13-captures/occlusion.png) and [disabled](iphone-m13-captures/occlusion-nocull.png) captures are pixel-identical. The enabled graph executes `depth pyramid`, `occlusion cull` and `depth late`; the disabled graph omits those passes. This exercises the compute read of the pre-pass depth and the delayed counter readback on the Apple GPU.

## Hashed shadows

Three fixture scenes use the same ground, camera, sun and blended box, changing only the material's alpha: [zero](iphone-m13-fixtures/scenes/shadow-zero.scene.json), [half](iphone-m13-fixtures/scenes/shadow-half.scene.json) and [one](iphone-m13-fixtures/scenes/shadow-one.scene.json). They capture `--shading-term shadow-factor` after 64 settle frames, with TAA disabled. The caster remains `Blend` even at alpha 1.

| Blended alpha | Mean red in a 40×40 ground region | Capture |
|---|---:|---|
| 0 | 235.000 | [No shadow](iphone-m13-captures/shadow-zero.png) |
| 0.5 | 212.123 | [Partial shadow](iphone-m13-captures/shadow-half.png) |
| 1 | 27.371 | [Full shadow](iphone-m13-captures/shadow-one.png) |

The region is `[990, 1180, 1030, 1220)` in the original PNG, on exposed ground beside the caster. These are displayed, tone-mapped RGB levels, not a linear estimate of transmittance. The intermediate darkness and visible ordered pattern demonstrate partial shadow coverage.

## Timing and limitations

The following are the capture logger's means over its last 100 frames, excluding the screenshot-copy frame. CPU time covers simulation and recording, excluding GPU/display waits. GPU time sums the graph's pass timestamps. These are Debug builds at portrait phone resolution, not comparable performance targets against the Android Release build or the desktop benchmark.

| Basic sample run | CPU work | Frame interval | GPU total | TAA | Depth pyramid |
|---|---:|---:|---:|---:|---:|
| Original defaults | 3.20 ms | 16.67 ms | 12.955 ms | 2.890 ms | 1.100 ms |
| Repeated defaults | 3.28 ms | 16.67 ms | 12.717 ms | 2.823 ms | 1.078 ms |
| Diagnostic BC6H | 2.59 ms | 16.67 ms | 12.922 ms | 2.856 ms | 1.080 ms |
| Diagnostic ASTC HDR | 2.66 ms | 16.67 ms | 12.995 ms | 3.254 ms | 1.049 ms |

No thermal-state telemetry, temperature measurement or sustained soak was performed. These short runs do not establish throttling behavior. Background/resume and touch were outside M13's requested rendering checks.

Khronos validation is unavailable in the phone package: its log warns that `VK_LAYER_KHRONOS_validation` is not installed. MoltenVK also emits four integer-attachment blending warnings during pipeline creation, the previously documented [issue 59](https://github.com/Pacheco95/sonnet/issues/59) ([investigation](moltenvk-id-blending.md)); it remains open. No engine error, failed capture or additional warning category appeared in the rendering runs. Successful rendering does not constitute a validation-layer-clean device run.

The expected automatic ASTC HDR path is the outstanding coverage item in [issue 143](https://github.com/Pacheco95/sonnet/issues/143). The diagnostic preference swap proves ASTC HDR sampling on this GPU, while preserving ADR-0024's production rule.

## Reproducing the diagnostic runs

Use a disposable checkout of engine commit `d07d3a1`. Use Apple Clang for the iOS dependency build as well as the Xcode target; this host needed `CC`, `CXX` and the front of `PATH` pointed at Xcode's toolchain to avoid selecting Homebrew LLVM 22, whose libc++ rejects the dependencies' older iOS deployment target. The signing team and device identifier must come from local configuration; neither is part of the artifacts. Keep the phone unlocked. The normal build, install, launch and file-copy commands are in [player.md](../player.md#running-on-ios).

1. Cook and capture the unchanged basic sample with `--play 3 --screenshot main.png`; repeat as `main-repeat.png`.
2. Apply [diagnostics.patch](iphone-m13-fixtures/diagnostics.patch). It logs the four feature flags, HDR format, history and survivor counts. Screenshot basenames containing `nocull` disable `RendererSettings::occlusionCulling`; basenames starting with `shadow` or `occlusion` select `AntiAliasing::None`. These are temporary experiment controls, not player CLI options.
3. Copy the basic project to a temporary project directory, retaining its assets and sidecars. Add the report's [fixture scenes](iphone-m13-fixtures/scenes/occlusion.scene.json) and the three corresponding material JSON files and `.meta` sidecars from `iphone-m13-fixtures/assets/`. Cook that project for `ios` and rebuild with its bundle. It contains 42 assets, nine scenes and prefabs, and cooks without warnings; the existing basic scenes and assets remain unchanged.
4. Capture `main-diag.png` and `main-nocull.png` with `--play 3`. Capture `occlusion.png` and `occlusion-nocull.png` with `--scene scenes/occlusion.scene.json --settle-frames 64`. Capture `shadow-zero.png`, `shadow-half.png` and `shadow-one.png` with their respective scene paths, `--shading-term shadow-factor --settle-frames 64`. Capture `shadow-main-no-taa.png` from the basic start scene with `--play 3`.
5. Capture `main-bc6h-ibl.png` and `main-bc6h-specular.png` with `--play 3` and, respectively, `--shading-term ibl-diffuse` and `--shading-term ibl-specular`.
6. Apply [astc-preference.patch](iphone-m13-fixtures/astc-preference.patch) after the diagnostic patch, rebuild and reinstall. Repeat the final and IBL captures as `main-astc.png`, `main-astc-ibl.png` and `main-astc-specular.png`. The patch also verifies that the ASTC-target call preserves the encoded blocks.
7. Restore both patches, re-cook the original basic project and rebuild/reinstall the original player. Compare RGB channels at equal dimensions; use the mean absolute channel error and maximum channel error recorded in `comparisons.json`. Do not compare compressed PNG bytes or resized previews.

The macOS reference used the same iOS bundle and `--play 3`; only the desktop window's aspect ratio and GPU differed. All images were inspected from the saved engine captures, without capturing the host screen.
