# iPad stress-scene GPU passes and TAA check

Measured 2026-10-10 at engine commit `48346ce9161cac3b4bdf2ca47e367ebb23eb6173` (current main including PR 171). Only this report is committed.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang. Charging over the Mac connection: a temporary diagnostic reported battery state 2 (charging) in all nine runs, and a 60 Hz display maximum.
- Unlocked, awake, full screen, on a stand, Auto-Lock disabled and unused during the runs, as specified by the owner. Every run stayed thermally nominal, so no five-minute cooldown was required.
- Local, uncommitted `TARGETED_DEVICE_FAMILY=1,2` build override added this one line after the phone entry in the generated plist’s `UIDeviceFamily` array:

```diff
+    <integer>2</integer>
```

The local blank scene copied exactly Camera (`Transform`, `Camera`, audio listener removed) and Sun (`Transform`, `DirectionalLight`) from `main.scene.json`, with no other entities. Fresh `sonnet_cook --platform ios` reported 40 assets, seven scenes and prefabs, zero warnings; each blank capture loaded as two entities. Each launch emitted four MoltenVK startup warnings about blending on `VK_FORMAT_R32_UINT`, also seen in the earlier runs; no engine error or blank-scene loading warning appeared.

For Part B, changed the single `RendererSettings::antialiasing` initializer in the renderer header from `AntiAliasing::Taa` to `AntiAliasing::None`; the player inherits it through `GameDesc`, with no scene/project override, and all four Part B capture lines confirm the `taa` pass is absent. Both builds used temporary info-level swapchain logging and a small Objective-C++ display/battery diagnostic. The header and both diagnostic source files were restored with `git checkout`; the blank file was deleted and the normal bundle re-cooked (six scenes and prefabs, zero warnings) before committing.

## Runs

Each run was a fresh process with `--console --terminate-existing`, `--scene scenes/stress.scene.json --play 3` or `--scene scenes/blank.scene.json --play 5`, and a distinct screenshot. Runs are listed in execution order. Every scaled stress run used `--render-size 1920x1080` and logged:

```text
capture draws at 1920x1080, scaled to the window
```

Every run logged `exit ok` and the same native swapchain:

```text
swapchain 1640x2360, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
```

Times average the last 100 frames; CPU excludes GPU/display waits. Values are as logged, and all GPU totals and passes happened to have numeric results; no timestamp-driven retries were made.

| Configuration / run | CPU ms | Interval ms | GPU total ms | Thermal part, as logged |
|---|---:|---:|---:|---|
| TAA on, stress 1920×1080 / 1 | 11.49 | 44.00 | 43.064 | `; thermal nominal -> nominal (worst nominal)` |
| TAA on, stress 1920×1080 / 2 | 12.02 | 44.21 | 43.568 | `; thermal nominal -> nominal (worst nominal)` |
| TAA on, stress 1920×1080 / 3 | 11.70 | 43.88 | 43.199 | `; thermal nominal -> nominal (worst nominal)` |
| TAA on, stress native / 1 | 11.90 | 48.99 | 48.112 | `; thermal nominal -> nominal (worst nominal)` |
| TAA on, stress native / 2 | 11.95 | 49.45 | 48.454 | `; thermal nominal -> nominal (worst nominal)` |
| TAA off, blank native / 1 | 0.35 | 16.67 | 7.173 | `; thermal nominal -> nominal (worst nominal)` |
| TAA off, blank native / 2 | 0.41 | 16.67 | 7.586 | `; thermal nominal -> nominal (worst nominal)` |
| TAA off, stress 1920×1080 / 1 | 11.39 | 40.50 | 39.673 | `; thermal nominal -> nominal (worst nominal)` |
| TAA off, stress 1920×1080 / 2 | 11.67 | 40.50 | 39.674 | `; thermal nominal -> nominal (worst nominal)` |

## Per-pass GPU times

Means in ms, with numeric-run count in parentheses. These are every stress pass above 0.5 ms in any of the three configurations; “rest” is the mean sum of all other logged passes per run. `— (0)` means the pass was absent, not a measured zero. Rounded pass sums can differ slightly from logged totals.

| Pass | TAA on, stress 1920×1080 | TAA on, stress native | TAA off, stress 1920×1080 |
|---|---:|---:|---:|
| forward | 25.848 (3) | 30.203 (2) | 25.681 (2) |
| shadow cascade 3 | 6.285 (3) | 5.976 (2) | 6.202 (2) |
| depth | 3.566 (3) | 2.183 (2) | 2.800 (2) |
| taa | 2.597 (3) | 4.784 (2) | — (0) |
| shadow cascade 2 | 2.113 (3) | 1.014 (2) | 2.079 (2) |
| depth pyramid | 0.855 (3) | 1.605 (2) | 0.857 (2) |
| bloom down 0 | 0.298 (3) | 0.587 (2) | 0.316 (2) |
| tonemap | 0.215 (3) | 0.591 (2) | 0.223 (2) |
| present | 0.482 (3) | 0.502 (2) | 0.506 (2) |
| Rest, summed | 1.019 (3) | 0.839 (2) | 1.011 (2) |

## What misses 33 ms

At 1920×1080 with TAA, forward (25.848 ms) and shadow cascade 3 (6.285 ms) dominate: together they consume 32.133 ms before depth (3.566 ms), TAA and the other passes. Mean logged GPU total is 43.277 ms and interval is 44.03 ms. Forward falls only 1.17-fold from native despite the 1.867-fold pixel reduction, so it has a pixel-sensitive contribution but does not behave like a purely pixel-bound pass in this comparison. Cascade 3 does not shrink at all; it is slightly more expensive at the smaller target.

The miss is mixed, rather than confined to post-processing or to resolution-independent work. Forward is the largest cost, while shadow cascades 2 and 3 contribute 8.398 ms at 1920×1080; cascade 2 and depth are actually more expensive there than at native size. TAA and depth pyramid shrink approximately with pixel count (1.84-fold and 1.88-fold), with bloom down 0 and tonemap also declining; cull and light clustering are small and nearly unchanged. Changed aspect ratio can change visible geometry and shadow coverage, so these comparisons do not isolate pixel count from scene coverage, and depth cannot be assumed independent of render size from these logs.

## TAA check

Blank with TAA off averaged 7.380 ms logged GPU total while both intervals remained 16.67 ms. Relative to [the earlier blank-native measurements](ipad-floor.md), the total fell by 6.455 ms, but the sum of non-TAA passes rose by about 2.330 ms: bloom down 0 now averaged 2.178 ms, bloom up 0 0.842 ms and tonemap 1.531 ms, all higher than before. Thus removing TAA reduced most of its previously attributed cost without improving the FIFO-limited interval, and some cost shifted into other passes; this supports timing-attribution/waiting sensitivity, but does not establish that the entire TAA timing was waiting rather than shader work. The TAA-on blank baseline came from the earlier session, not a same-session paired capture, which further limits that inference.

Stress with TAA off averaged 39.674 ms GPU and 40.50 ms between frames: reductions of 3.604 ms and 3.53 ms versus this session’s TAA-on 1920×1080 runs, still above 33 ms. Removing the 2.597 ms TAA pass did not cause a comparably large increase elsewhere: depth instead fell from 3.566 to 2.800 ms and forward from 25.848 to 25.681 ms, while the remaining pass changes were small. Those additional changes make the total improvement larger than the removed TAA timing and prevent treating the toggle as an exact isolated TAA-work measurement; the stress result shows no large migration of waiting into another measured pass.

## Limits

GPU timestamps remain independently unvalidated; no Xcode per-encoder capture was taken. The 1920×1080 image is scaled into a portrait 1640×2360 window with a changed aspect ratio, so visibility and workload can differ as well as pixel count. The 60 Hz FIFO cap hides blank-scene improvements below 16.7 ms in intervals; active scanout cadence and the precise division of GPU work from waiting remain unverified. These short captures do not establish the cause of sustained slowdown, and apply only to this iPad, not the untested iPhone.

Context: [iPad stress and sustained report](ipad-m14.md), [presentation measurement](ipad-framerate.md), and [blank-scene floor report](ipad-floor.md).

<details>
<summary>Complete final capture lines, in run order</summary>

TAA on, stress 1920×1080 / 1:

```text
capture frame times over the last 100 frames: CPU 11.49 ms a frame, 44.00 ms apart; GPU 43.064 ms: cull 0.182 ms, shadow cascade 0 0.174 ms, shadow cascade 1 0.311 ms, shadow cascade 2 2.108 ms, shadow cascade 3 6.287 ms, depth 3.534 ms, depth pyramid 0.846 ms, occlusion cull 0.030 ms, depth late 0.021 ms, light clustering 0.055 ms, forward 25.733 ms, taa 2.603 ms, bloom down 0 0.284 ms, bloom down 1 0.050 ms, bloom down 2 0.039 ms, bloom down 3 0.030 ms, bloom down 4 0.023 ms, bloom up 3 0.023 ms, bloom up 2 0.024 ms, bloom up 1 0.016 ms, bloom up 0 0.015 ms, tonemap 0.206 ms, present 0.472 ms; thermal nominal -> nominal (worst nominal)
```

TAA on, stress 1920×1080 / 2:

```text
capture frame times over the last 100 frames: CPU 12.02 ms a frame, 44.21 ms apart; GPU 43.568 ms: cull 0.180 ms, shadow cascade 0 0.171 ms, shadow cascade 1 0.343 ms, shadow cascade 2 2.122 ms, shadow cascade 3 6.299 ms, depth 3.564 ms, depth pyramid 0.862 ms, occlusion cull 0.029 ms, depth late 0.023 ms, light clustering 0.060 ms, forward 26.088 ms, taa 2.594 ms, bloom down 0 0.298 ms, bloom down 1 0.050 ms, bloom down 2 0.036 ms, bloom down 3 0.030 ms, bloom down 4 0.028 ms, bloom up 3 0.024 ms, bloom up 2 0.018 ms, bloom up 1 0.018 ms, bloom up 0 0.014 ms, tonemap 0.224 ms, present 0.493 ms; thermal nominal -> nominal (worst nominal)
```

TAA on, stress 1920×1080 / 3:

```text
capture frame times over the last 100 frames: CPU 11.70 ms a frame, 43.88 ms apart; GPU 43.199 ms: cull 0.185 ms, shadow cascade 0 0.164 ms, shadow cascade 1 0.369 ms, shadow cascade 2 2.109 ms, shadow cascade 3 6.268 ms, depth 3.599 ms, depth pyramid 0.857 ms, occlusion cull 0.031 ms, depth late 0.021 ms, light clustering 0.059 ms, forward 25.723 ms, taa 2.594 ms, bloom down 0 0.313 ms, bloom down 1 0.039 ms, bloom down 2 0.041 ms, bloom down 3 0.029 ms, bloom down 4 0.024 ms, bloom up 3 0.024 ms, bloom up 2 0.025 ms, bloom up 1 0.015 ms, bloom up 0 0.015 ms, tonemap 0.216 ms, present 0.480 ms; thermal nominal -> nominal (worst nominal)
```

TAA on, stress native / 1:

```text
capture frame times over the last 100 frames: CPU 11.90 ms a frame, 48.99 ms apart; GPU 48.112 ms: cull 0.185 ms, shadow cascade 0 0.070 ms, shadow cascade 1 0.068 ms, shadow cascade 2 1.049 ms, shadow cascade 3 5.991 ms, depth 2.196 ms, depth pyramid 1.575 ms, occlusion cull 0.030 ms, depth late 0.022 ms, light clustering 0.058 ms, forward 30.044 ms, taa 4.773 ms, bloom down 0 0.584 ms, bloom down 1 0.135 ms, bloom down 2 0.042 ms, bloom down 3 0.026 ms, bloom down 4 0.020 ms, bloom up 3 0.023 ms, bloom up 2 0.024 ms, bloom up 1 0.024 ms, bloom up 0 0.076 ms, tonemap 0.586 ms, present 0.510 ms; thermal nominal -> nominal (worst nominal)
```

TAA on, stress native / 2:

```text
capture frame times over the last 100 frames: CPU 11.95 ms a frame, 49.45 ms apart; GPU 48.454 ms: cull 0.178 ms, shadow cascade 0 0.074 ms, shadow cascade 1 0.125 ms, shadow cascade 2 0.978 ms, shadow cascade 3 5.961 ms, depth 2.169 ms, depth pyramid 1.635 ms, occlusion cull 0.031 ms, depth late 0.021 ms, light clustering 0.060 ms, forward 30.362 ms, taa 4.796 ms, bloom down 0 0.590 ms, bloom down 1 0.138 ms, bloom down 2 0.038 ms, bloom down 3 0.030 ms, bloom down 4 0.017 ms, bloom up 3 0.023 ms, bloom up 2 0.024 ms, bloom up 1 0.024 ms, bloom up 0 0.091 ms, tonemap 0.595 ms, present 0.494 ms; thermal nominal -> nominal (worst nominal)
```

TAA off, blank native / 1:

```text
capture frame times over the last 100 frames: CPU 0.35 ms a frame, 16.67 ms apart; GPU 7.173 ms: depth 0.095 ms, light clustering 0.223 ms, forward 0.612 ms, bloom down 0 2.328 ms, bloom down 1 0.367 ms, bloom down 2 0.040 ms, bloom down 3 0.052 ms, bloom down 4 0.045 ms, bloom up 3 0.047 ms, bloom up 2 0.054 ms, bloom up 1 0.051 ms, bloom up 0 0.840 ms, tonemap 1.540 ms, present 0.881 ms; thermal nominal -> nominal (worst nominal)
```

TAA off, blank native / 2:

```text
capture frame times over the last 100 frames: CPU 0.41 ms a frame, 16.67 ms apart; GPU 7.586 ms: depth 0.192 ms, light clustering 0.423 ms, forward 0.949 ms, bloom down 0 2.028 ms, bloom down 1 0.453 ms, bloom down 2 0.044 ms, bloom down 3 0.056 ms, bloom down 4 0.047 ms, bloom up 3 0.047 ms, bloom up 2 0.048 ms, bloom up 1 0.048 ms, bloom up 0 0.843 ms, tonemap 1.523 ms, present 0.884 ms; thermal nominal -> nominal (worst nominal)
```

TAA off, stress 1920×1080 / 1:

```text
capture frame times over the last 100 frames: CPU 11.39 ms a frame, 40.50 ms apart; GPU 39.673 ms: cull 0.187 ms, shadow cascade 0 0.170 ms, shadow cascade 1 0.330 ms, shadow cascade 2 2.080 ms, shadow cascade 3 6.211 ms, depth 2.789 ms, depth pyramid 0.867 ms, occlusion cull 0.028 ms, depth late 0.021 ms, light clustering 0.062 ms, forward 25.667 ms, bloom down 0 0.311 ms, bloom down 1 0.053 ms, bloom down 2 0.030 ms, bloom down 3 0.029 ms, bloom down 4 0.027 ms, bloom up 3 0.023 ms, bloom up 2 0.022 ms, bloom up 1 0.023 ms, bloom up 0 0.013 ms, tonemap 0.222 ms, present 0.510 ms; thermal nominal -> nominal (worst nominal)
```

TAA off, stress 1920×1080 / 2:

```text
capture frame times over the last 100 frames: CPU 11.67 ms a frame, 40.50 ms apart; GPU 39.674 ms: cull 0.183 ms, shadow cascade 0 0.181 ms, shadow cascade 1 0.310 ms, shadow cascade 2 2.078 ms, shadow cascade 3 6.193 ms, depth 2.810 ms, depth pyramid 0.847 ms, occlusion cull 0.035 ms, depth late 0.023 ms, light clustering 0.064 ms, forward 25.695 ms, bloom down 0 0.321 ms, bloom down 1 0.049 ms, bloom down 2 0.029 ms, bloom down 3 0.030 ms, bloom down 4 0.026 ms, bloom up 3 0.023 ms, bloom up 2 0.021 ms, bloom up 1 0.018 ms, bloom up 0 0.013 ms, tonemap 0.224 ms, present 0.501 ms; thermal nominal -> nominal (worst nominal)
```

</details>
