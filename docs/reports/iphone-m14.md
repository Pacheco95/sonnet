# M14 stress-scene measurement on iPhone

Measurement of [issue 149](https://github.com/Pacheco95/sonnet/issues/149), 2026-10-10, at engine commit `ed9f65911bbf16e94a50c17f9aff4ea9bf5b105a` (current `main`, including the stress scene and its icosphere correction). No engine code was changed.

## Setup

- GPU generation: Apple A17 Pro.
- OS: iOS 27.0. MoltenVK 1.4.2, Vulkan 1.4.357.
- Build: `ios-release`, RelWithDebInfo, Apple Clang from Xcode 26.2, `-O2 -g -DNDEBUG`, signed with the existing local development configuration.
- Cook: current basic sample, `sonnet_cook --platform ios`, 40 assets, six scenes and prefabs, zero warnings. The host cook tool was rebuilt with `macos-release-local`, the machine's Apple Clang variant of `macos-release`.
- Scene: `scenes/stress.scene.json`; its script creates 10,000 alternating boxes and 320-triangle icospheres and 100 point lights. Visibility still depends on the camera, frustum and occlusion culling; no survivor count was captured, so this is not proof of 10,000 surviving visible draws.
- Capture: 1290×2796, 3,606,840 pixels, **1.74 times** 1920×1080's pixels. This is the native portrait output rather than a controlled 1080p measurement.

The [engine-written PNG](iphone-m14-captures/stress.png) was copied from the app's data container and inspected: it shows the alternating box-and-icosphere grid. All three short runs exited `ok`. [Sanitized timing evidence](iphone-m14-captures/evidence.txt) preserves the complete final timing lines.

## Measurements

Times are milliseconds, averaged over the last 100 frames before the screenshot-copy frame. CPU work excludes GPU/display waits; interval is time between frames. GPU total is the sum of logged per-pass timestamps. The GPU readings below are retained verbatim but **are not reliable evidence of GPU cost or headroom**: the third successful run reports zero for every pass. [Issue 152](https://github.com/Pacheco95/sonnet/issues/152) tracks this failure separately from the inflated Debug macOS timings in [issue 141](https://github.com/Pacheco95/sonnet/issues/141).

| Measurement (ms) | Short 1 | Short 2 | Short 3 | Sustained, `--play 600` |
|---|---:|---:|---:|---:|
| CPU work | 6.24 | 6.36 | 6.36 | 9.58 |
| Frame interval | 22.35 | 22.37 | 22.72 | 49.01 |
| Logged GPU total | 0.657 | 0.222 | 0.000 | 0.000 |
| cull | 0.003 | 0.001 | 0.000 | 0.000 |
| shadow cascade 0 | 0.001 | 0.000 | 0.000 | 0.000 |
| shadow cascade 1 | 0.005 | 0.002 | 0.000 | 0.000 |
| shadow cascade 2 | 0.022 | 0.008 | 0.000 | 0.000 |
| shadow cascade 3 | 0.147 | 0.049 | 0.000 | 0.000 |
| depth | 0.039 | 0.014 | 0.000 | 0.000 |
| depth pyramid | 0.031 | 0.011 | 0.000 | 0.000 |
| occlusion cull | 0.001 | 0.000 | 0.000 | 0.000 |
| depth late | 0.000 | 0.000 | 0.000 | 0.000 |
| light clustering | 0.000 | 0.000 | 0.000 | 0.000 |
| forward | 0.272 | 0.094 | 0.000 | 0.000 |
| taa | 0.099 | 0.032 | 0.000 | 0.000 |
| bloom down 0 | 0.011 | 0.003 | 0.000 | 0.000 |
| bloom down 1 | 0.002 | 0.001 | 0.000 | 0.000 |
| bloom down 2 | 0.000 | 0.000 | 0.000 | 0.000 |
| bloom down 3 | 0.000 | 0.000 | 0.000 | 0.000 |
| bloom down 4 | 0.001 | 0.000 | 0.000 | 0.000 |
| bloom up 3 | 0.001 | 0.000 | 0.000 | 0.000 |
| bloom up 2 | 0.001 | 0.000 | 0.000 | 0.000 |
| bloom up 1 | 0.000 | 0.001 | 0.000 | 0.000 |
| bloom up 0 | 0.002 | 0.000 | 0.000 | 0.000 |
| tonemap | 0.007 | 0.003 | 0.000 | 0.000 |
| present | 0.010 | 0.003 | 0.000 | 0.000 |

The short-run interval averages 22.48 ms (44.5 frames/s), with a range of 0.37 ms. These runs are not at a 16.67 ms display-limited interval. Their low or zero GPU totals do not justify classifying the bottleneck or calculating GPU headroom.

## Sustained run

The operator turned on the room air conditioner during the cooldown, before the sustained launch. Room and device temperatures were not measured. The sustained result therefore reflects that cooling condition; it is not a controlled comparison at a measured, identical ambient temperature.

The initial sustained launch failed to produce measurement evidence: the CoreDevice console connection was invalidated after 614.4 s (CoreDevice error 3, Mercury error 1001), with no application timing line or sustained PNG. A player process remained, and the operator saw the ordinary particles-and-rotating-cube basic scene. The launch had omitted `--terminate-existing`, so it did not guarantee a fresh process receiving the stress arguments. The cause of the connection failure was not established. This attempt is excluded from the performance table; [issue 158](https://github.com/Pacheco95/sonnet/issues/158) records it and the launch safeguard. The player was stopped and another full 15-minute cooldown preceded the fresh-process retry.

The fresh-process run launched at 14:59 local after at least 15 minutes with the player stopped and finished at 15:24:44: **1,544.0 s (25 min 44 s)** wall time for 36,000 simulation frames plus loading, settling and capture. The operator confirmed the stress grid shortly after launch and again after more than 20 minutes, with the phone awake and Sonnet in front. No post-launch background transition or engine error was logged. The capture exited `ok`; the [sustained PNG](iphone-m14-captures/stress-sustained.png) shows the grid.

The last 100 frames took **49.01 ms apart**, 2.18 times the short-run mean (118% longer), and **9.58 ms CPU work**, against 6.32 ms across the short runs (52% higher). Sustained throughput therefore degraded substantially. This is consistent with throttling, but no thermal-state telemetry or temperature reading was obtained, so the cause cannot be established from these measurements alone. The brief idle Instruments Power Profiler probe did not produce usable thermal telemetry; no profiler was attached during the successful measurement. GPU total and every pass again report `0.000 ms`, so GPU-time degradation and headroom cannot be quantified.

## Reproduction

Use the existing local signing configuration; device identifiers and signing credentials are intentionally omitted.

```sh
cmake --build --preset macos-release-local --target sonnet_cook_app
./build/macos-release-local/apps/cook/sonnet_cook apps/samples/basic --platform ios --out build/ios-bundle
cmake --build --preset ios-release -- -allowProvisioningUpdates -allowProvisioningDeviceRegistration
xcrun devicectl device install app --device <device-id> build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app
xcrun devicectl device process launch --console --terminate-existing --device <device-id> io.github.pacheco95.sonnet --scene scenes/stress.scene.json --play 3 --screenshot stress.png
# Repeat the short launch three times; after cooling, replace --play 3 with --play 600.
xcrun devicectl device copy from --device <device-id> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/stress.png" --destination stress.png
```

The pre-existing `ios-release` configuration packages `build/ios-bundle/game.sbundle`; a fresh configuration must set `SONNET_IOS_BUNDLE` to that file as documented in [Running on iOS](../player.md#running-on-ios).

## Verdict against 33 ms

The three short runs meet the 33 ms frame-interval threshold at native 1290×2796 output (22.35–22.72 ms), but the sustained run **does not**: its final interval is 49.01 ms, about 20.4 frames/s, versus the target’s approximately 30 frames/s. The 33 ms sustained phone target is therefore not demonstrated on Apple A17 Pro under this stress-scene run, even with room air conditioning. This is a native-resolution scene result, not a controlled 1080p or verified 10,000-survivor benchmark; no resolution scaling is used to infer a 1080p result. Zero/unreliable GPU timestamps prevent a GPU-headroom verdict. Follow-ups are [sustained slowdown (#159)](https://github.com/Pacheco95/sonnet/issues/159), [timestamp reporting (#152)](https://github.com/Pacheco95/sonnet/issues/152) and [fresh-process capture launches (#158)](https://github.com/Pacheco95/sonnet/issues/158).
