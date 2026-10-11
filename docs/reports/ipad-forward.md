# iPad forward pass: lights against objects

Measured 2026-10-11 at engine commit `99c725ce1786302e2e6a3fc219131717aead8b7e` (main including PR 172), for issue 174. Only this report is committed.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang, default renderer settings including TAA. Charging, as confirmed by the owner; no battery diagnostic or engine instrumentation was added.
- The owner confirmed unlocked, awake, full screen, on a stand, Auto-Lock disabled and unused during captures. Every run remained thermally nominal; no five-minute cooldown was required.
- Local, uncommitted `TARGETED_DEVICE_FAMILY=1,2` build override. The generated plist's `UIDeviceFamily` array gained this line after the phone entry, and the built plist was checked to contain `[1, 2]`:

```diff
+    <integer>2</integer>
```

Only `SIDE` and `LIGHTS` in `apps/samples/basic/scripts/stress.lua` were edited locally. Each configuration was cooked with `sonnet_cook --platform ios`, rebuilt and reinstalled; the packaged bundle was checked byte-for-byte against the fresh cook. All cooks reported 40 assets, six scenes and prefabs, zero warnings. Retrieved screenshots were inspected: the smaller grid has a visibly nearer far edge, and the lit variants show localized brighter patches that disappear with zero lights. The exact variant script was also found in the cooked bundle for C and D, and checked in the packaged bundle for E and the final A. The scene file and engine sources were unchanged. After the final A, the script was restored with `git checkout`, the normal bundle re-cooked, and the checkout checked to contain only this report.

Every run was a fresh process with `--console --terminate-existing --scene scenes/stress.scene.json --play 3 --render-size 1920x1080 --screenshot <distinct-name>.png`. The order was two runs each of A, B, C, D, E, then A again. All twelve runs logged `exit ok`, the target line below, and the same portrait native swapchain:

```text
capture draws at 1920x1080, scaled to the window
swapchain 1640x2360, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
```

Each launch logged four engine-forwarded MoltenVK startup warnings about blending on `VK_FORMAT_R32_UINT`, as in the earlier reports; no other engine warning or error was found. Times average the last 100 frames; CPU excludes GPU/display waits. All passes and totals were numeric, with no retries to replace unavailable timestamps.

## Runs

Values are as logged; A start and A end are the same configuration at opposite ends of the session.

| Variant / run | SIDE | LIGHTS | Objects | CPU ms | Interval ms | GPU total ms | Thermal part, as logged |
|---|---:|---:|---:|---:|---:|---:|---|
| A start / 1 | 100 | 100 | 10,000 | 11.00 | 44.17 | 43.151 | `; thermal nominal -> nominal (worst nominal)` |
| A start / 2 | 100 | 100 | 10,000 | 11.56 | 44.21 | 43.434 | `; thermal nominal -> nominal (worst nominal)` |
| B / 1 | 100 | 0 | 10,000 | 6.87 | 29.45 | 28.491 | `; thermal nominal -> nominal (worst nominal)` |
| B / 2 | 100 | 0 | 10,000 | 8.48 | 29.32 | 28.504 | `; thermal nominal -> nominal (worst nominal)` |
| C / 1 | 100 | 25 | 10,000 | 8.34 | 34.27 | 33.247 | `; thermal nominal -> nominal (worst nominal)` |
| C / 2 | 100 | 25 | 10,000 | 9.44 | 34.17 | 33.298 | `; thermal nominal -> nominal (worst nominal)` |
| D / 1 | 71 | 100 | 5,041 | 4.83 | 37.50 | 36.523 | `; thermal nominal -> nominal (worst nominal)` |
| D / 2 | 71 | 100 | 5,041 | 6.26 | 37.55 | 36.792 | `; thermal nominal -> nominal (worst nominal)` |
| E / 1 | 71 | 0 | 5,041 | 3.74 | 23.38 | 22.587 | `; thermal nominal -> nominal (worst nominal)` |
| E / 2 | 71 | 0 | 5,041 | 5.32 | 23.23 | 22.520 | `; thermal nominal -> nominal (worst nominal)` |
| A end / 1 | 100 | 100 | 10,000 | 11.01 | 44.12 | 43.081 | `; thermal nominal -> nominal (worst nominal)` |
| A end / 2 | 100 | 100 | 10,000 | 11.35 | 44.10 | 43.166 | `; thermal nominal -> nominal (worst nominal)` |

## Means and differences

Means in ms; parentheses give numeric-run counts for each cell. A pools the two initial and two final runs; the other variants have two runs each. Differences below use these pooled means. Displayed rounding can slightly change a subtraction.

| Variant | Forward | Cascade 2 | Cascade 3 | Depth | GPU total | Interval |
|---|---:|---:|---:|---:|---:|---:|
| A | 25.795 (4) | 2.114 (4) | 6.271 (4) | 3.575 (4) | 43.208 (4) | 44.150 (4) |
| B | 11.128 (2) | 2.111 (2) | 6.282 (2) | 3.580 (2) | 28.498 (2) | 29.385 (2) |
| C | 15.870 (2) | 2.111 (2) | 6.278 (2) | 3.561 (2) | 33.273 (2) | 34.220 (2) |
| D | 23.447 (2) | 1.987 (2) | 3.305 (2) | 2.548 (2) | 36.657 (2) | 37.525 (2) |
| E | 9.352 (2) | 2.010 (2) | 3.320 (2) | 2.575 (2) | 22.553 (2) | 23.305 (2) |

Forward differences are higher-count minus lower-count. The object reduction is 4,959 objects (10,000 to 5,041). These are finite differences for this scene, not fitted scaling laws.

| Comparison | Run counts | Difference of forward means, ms | Difference divided by count change |
|---|---|---:|---:|
| A − B | 4 vs 2 | 14.6675 | 0.146675 ms per light |
| A − C | 4 vs 2 | 9.9255 | 0.132340 ms per light |
| C − B | 2 vs 2 | 4.7420 | 0.189680 ms per light |
| D − E | 2 vs 2 | 14.0950 | 0.140950 ms per light |
| A − D | 4 vs 2 | 2.3475 | 0.473382 ms per thousand objects |
| B − E | 2 vs 2 | 1.7750 | 0.357935 ms per thousand objects |

## Interpretation

Forward follows both lights and objects in these comparisons, with the much larger effect coming from lights. Removing 100 lights saves 14.6675 ms at 10,000 objects (A versus B, four versus two runs), 56.9% of this session's 25.795 ms baseline forward pass. At 5,041 objects it saves 14.0950 ms (D versus E, two versus two), 54.6% of that baseline. C lies between A and B, but the 0–25 light difference is 0.189680 ms per light while the 25–100 difference is 0.132340 ms per light; light count alone does not establish a constant cost per light.

Reducing objects saves 2.3475 ms with 100 lights (A versus D, four versus two runs), or 9.1% of baseline forward, and 1.7750 ms with zero lights (B versus E, two versus two), or 6.9%. This nearly halves the created object count but reduces forward by only 9.1% and 16.0% of the respective larger-grid pass. The light-related difference remains large in the smaller grid. These comparisons support a dominant light-sensitive contribution and a smaller geometry/coverage-sensitive contribution; they do not isolate shader arithmetic from bandwidth, occupancy or visibility.

Initial A forward averaged 25.8775 ms (two runs), final A 25.7125 ms (two): a −0.1650 ms change, or −0.64%. GPU total changed from 43.2925 to 43.1235 ms, and interval from 44.19 to 44.11 ms. The two forward ranges overlap (25.723–26.032 initially, 25.669–25.756 finally); the observed drift is small relative to either the light or object effect, without establishing a statistical confidence interval.

The shadow and depth comparisons follow geometry rather than point-light count. A versus B (four versus two runs) differs by +0.0025 ms in cascade 2, −0.01075 ms in cascade 3 and −0.0050 ms in depth; D versus E (two versus two) differs by −0.0220, −0.0145 and −0.0275 ms respectively. Cascade 3 does not show a meaningful positive light-count effect. Reducing objects cuts cascade 3 by 2.96575 ms with lights (A versus D) and 2.9620 ms without (B versus E), about 47% in either comparison. Depth falls by 1.0270 and 1.0045 ms, about 29% and 28%; cascade 2 falls less, by 0.1260 and 0.1015 ms, about 6% and 5%. Run counts are four versus two for A–D and two versus two for B–E. GPU total and interval decline alongside these pass changes, as shown in the means table.

## Limits and context

GPU timestamps are not independently validated; no Xcode per-encoder capture was taken. Lights retain the script's fixed range and positions, and changing `LIGHTS` truncates that ordered spatial distribution, so these results describe this scene rather than lights in general. Reducing `SIDE` changes grid extent and coverage as well as object count; no independently verified surviving draw count was collected. The 1920×1080 target is scaled into a portrait window, and these short captures do not establish sustained thermal behavior or general performance laws. No conclusion extends to the iPhone.

Context: [stress pass costs and TAA check](ipad-stress-passes.md), [blank-scene floor](ipad-floor.md), and [M14 stress measurement](ipad-m14.md).

Validation: `python3 tools/check_docs.py`; the pre-commit privacy scan found no identity or device identifier in this report. No engine code changes or tuning are included.

<details>
<summary>Complete final capture lines, in run order</summary>

A start / 1:

```text
capture frame times over the last 100 frames: CPU 11.00 ms a frame, 44.17 ms apart; GPU 43.151 ms: cull 0.188 ms, shadow cascade 0 0.170 ms, shadow cascade 1 0.332 ms, shadow cascade 2 2.116 ms, shadow cascade 3 6.303 ms, depth 3.533 ms, depth pyramid 0.883 ms, occlusion cull 0.029 ms, depth late 0.023 ms, light clustering 0.063 ms, forward 25.723 ms, taa 2.586 ms, bloom down 0 0.288 ms, bloom down 1 0.048 ms, bloom down 2 0.041 ms, bloom down 3 0.029 ms, bloom down 4 0.024 ms, bloom up 3 0.022 ms, bloom up 2 0.027 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.200 ms, present 0.492 ms; thermal nominal -> nominal (worst nominal)
```

A start / 2:

```text
capture frame times over the last 100 frames: CPU 11.56 ms a frame, 44.21 ms apart; GPU 43.434 ms: cull 0.186 ms, shadow cascade 0 0.166 ms, shadow cascade 1 0.340 ms, shadow cascade 2 2.106 ms, shadow cascade 3 6.238 ms, depth 3.576 ms, depth pyramid 0.854 ms, occlusion cull 0.035 ms, depth late 0.023 ms, light clustering 0.067 ms, forward 26.032 ms, taa 2.588 ms, bloom down 0 0.307 ms, bloom down 1 0.044 ms, bloom down 2 0.036 ms, bloom down 3 0.033 ms, bloom down 4 0.025 ms, bloom up 3 0.022 ms, bloom up 2 0.020 ms, bloom up 1 0.016 ms, bloom up 0 0.015 ms, tonemap 0.215 ms, present 0.488 ms; thermal nominal -> nominal (worst nominal)
```

B / 1:

```text
capture frame times over the last 100 frames: CPU 6.87 ms a frame, 29.45 ms apart; GPU 28.491 ms: cull 0.183 ms, shadow cascade 0 0.169 ms, shadow cascade 1 0.349 ms, shadow cascade 2 2.114 ms, shadow cascade 3 6.270 ms, depth 3.571 ms, depth pyramid 0.850 ms, occlusion cull 0.036 ms, depth late 0.021 ms, light clustering 0.013 ms, forward 11.140 ms, taa 2.571 ms, bloom down 0 0.304 ms, bloom down 1 0.046 ms, bloom down 2 0.036 ms, bloom down 3 0.035 ms, bloom down 4 0.023 ms, bloom up 3 0.024 ms, bloom up 2 0.023 ms, bloom up 1 0.016 ms, bloom up 0 0.017 ms, tonemap 0.206 ms, present 0.474 ms; thermal nominal -> nominal (worst nominal)
```

B / 2:

```text
capture frame times over the last 100 frames: CPU 8.48 ms a frame, 29.32 ms apart; GPU 28.504 ms: cull 0.189 ms, shadow cascade 0 0.185 ms, shadow cascade 1 0.319 ms, shadow cascade 2 2.108 ms, shadow cascade 3 6.294 ms, depth 3.589 ms, depth pyramid 0.872 ms, occlusion cull 0.033 ms, depth late 0.022 ms, light clustering 0.014 ms, forward 11.115 ms, taa 2.559 ms, bloom down 0 0.302 ms, bloom down 1 0.048 ms, bloom down 2 0.035 ms, bloom down 3 0.034 ms, bloom down 4 0.024 ms, bloom up 3 0.023 ms, bloom up 2 0.021 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.206 ms, present 0.479 ms; thermal nominal -> nominal (worst nominal)
```

C / 1:

```text
capture frame times over the last 100 frames: CPU 8.34 ms a frame, 34.27 ms apart; GPU 33.247 ms: cull 0.182 ms, shadow cascade 0 0.172 ms, shadow cascade 1 0.310 ms, shadow cascade 2 2.115 ms, shadow cascade 3 6.290 ms, depth 3.560 ms, depth pyramid 0.885 ms, occlusion cull 0.036 ms, depth late 0.026 ms, light clustering 0.022 ms, forward 15.872 ms, taa 2.575 ms, bloom down 0 0.292 ms, bloom down 1 0.047 ms, bloom down 2 0.043 ms, bloom down 3 0.032 ms, bloom down 4 0.024 ms, bloom up 3 0.023 ms, bloom up 2 0.026 ms, bloom up 1 0.017 ms, bloom up 0 0.016 ms, tonemap 0.192 ms, present 0.492 ms; thermal nominal -> nominal (worst nominal)
```

C / 2:

```text
capture frame times over the last 100 frames: CPU 9.44 ms a frame, 34.17 ms apart; GPU 33.298 ms: cull 0.211 ms, shadow cascade 0 0.144 ms, shadow cascade 1 0.371 ms, shadow cascade 2 2.106 ms, shadow cascade 3 6.266 ms, depth 3.562 ms, depth pyramid 0.864 ms, occlusion cull 0.034 ms, depth late 0.021 ms, light clustering 0.024 ms, forward 15.867 ms, taa 2.584 ms, bloom down 0 0.303 ms, bloom down 1 0.047 ms, bloom down 2 0.037 ms, bloom down 3 0.034 ms, bloom down 4 0.026 ms, bloom up 3 0.024 ms, bloom up 2 0.023 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.199 ms, present 0.519 ms; thermal nominal -> nominal (worst nominal)
```

D / 1:

```text
capture frame times over the last 100 frames: CPU 4.83 ms a frame, 37.50 ms apart; GPU 36.523 ms: cull 0.161 ms, shadow cascade 0 0.163 ms, shadow cascade 1 0.341 ms, shadow cascade 2 1.974 ms, shadow cascade 3 3.303 ms, depth 2.489 ms, depth pyramid 0.840 ms, occlusion cull 0.035 ms, depth late 0.021 ms, light clustering 0.050 ms, forward 23.403 ms, taa 2.585 ms, bloom down 0 0.307 ms, bloom down 1 0.040 ms, bloom down 2 0.036 ms, bloom down 3 0.031 ms, bloom down 4 0.024 ms, bloom up 3 0.022 ms, bloom up 2 0.021 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.191 ms, present 0.451 ms; thermal nominal -> nominal (worst nominal)
```

D / 2:

```text
capture frame times over the last 100 frames: CPU 6.26 ms a frame, 37.55 ms apart; GPU 36.792 ms: cull 0.150 ms, shadow cascade 0 0.165 ms, shadow cascade 1 0.300 ms, shadow cascade 2 2.001 ms, shadow cascade 3 3.308 ms, depth 2.607 ms, depth pyramid 0.856 ms, occlusion cull 0.041 ms, depth late 0.023 ms, light clustering 0.066 ms, forward 23.492 ms, taa 2.601 ms, bloom down 0 0.275 ms, bloom down 1 0.047 ms, bloom down 2 0.039 ms, bloom down 3 0.031 ms, bloom down 4 0.025 ms, bloom up 3 0.024 ms, bloom up 2 0.024 ms, bloom up 1 0.016 ms, bloom up 0 0.017 ms, tonemap 0.204 ms, present 0.481 ms; thermal nominal -> nominal (worst nominal)
```

E / 1:

```text
capture frame times over the last 100 frames: CPU 3.74 ms a frame, 23.38 ms apart; GPU 22.587 ms: cull 0.152 ms, shadow cascade 0 0.172 ms, shadow cascade 1 0.254 ms, shadow cascade 2 2.011 ms, shadow cascade 3 3.331 ms, depth 2.575 ms, depth pyramid 0.886 ms, occlusion cull 0.039 ms, depth late 0.023 ms, light clustering 0.014 ms, forward 9.357 ms, taa 2.577 ms, bloom down 0 0.301 ms, bloom down 1 0.046 ms, bloom down 2 0.042 ms, bloom down 3 0.028 ms, bloom down 4 0.024 ms, bloom up 3 0.023 ms, bloom up 2 0.022 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.199 ms, present 0.480 ms; thermal nominal -> nominal (worst nominal)
```

E / 2:

```text
capture frame times over the last 100 frames: CPU 5.32 ms a frame, 23.23 ms apart; GPU 22.520 ms: cull 0.143 ms, shadow cascade 0 0.170 ms, shadow cascade 1 0.256 ms, shadow cascade 2 2.008 ms, shadow cascade 3 3.309 ms, depth 2.576 ms, depth pyramid 0.869 ms, occlusion cull 0.038 ms, depth late 0.023 ms, light clustering 0.015 ms, forward 9.348 ms, taa 2.569 ms, bloom down 0 0.297 ms, bloom down 1 0.047 ms, bloom down 2 0.040 ms, bloom down 3 0.030 ms, bloom down 4 0.025 ms, bloom up 3 0.024 ms, bloom up 2 0.022 ms, bloom up 1 0.017 ms, bloom up 0 0.016 ms, tonemap 0.199 ms, present 0.482 ms; thermal nominal -> nominal (worst nominal)
```

A end / 1:

```text
capture frame times over the last 100 frames: CPU 11.01 ms a frame, 44.12 ms apart; GPU 43.081 ms: cull 0.183 ms, shadow cascade 0 0.172 ms, shadow cascade 1 0.287 ms, shadow cascade 2 2.120 ms, shadow cascade 3 6.256 ms, depth 3.568 ms, depth pyramid 0.839 ms, occlusion cull 0.033 ms, depth late 0.022 ms, light clustering 0.050 ms, forward 25.756 ms, taa 2.597 ms, bloom down 0 0.289 ms, bloom down 1 0.048 ms, bloom down 2 0.044 ms, bloom down 3 0.028 ms, bloom down 4 0.023 ms, bloom up 3 0.023 ms, bloom up 2 0.024 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.201 ms, present 0.485 ms; thermal nominal -> nominal (worst nominal)
```

A end / 2:

```text
capture frame times over the last 100 frames: CPU 11.35 ms a frame, 44.10 ms apart; GPU 43.166 ms: cull 0.189 ms, shadow cascade 0 0.171 ms, shadow cascade 1 0.334 ms, shadow cascade 2 2.112 ms, shadow cascade 3 6.288 ms, depth 3.623 ms, depth pyramid 0.850 ms, occlusion cull 0.033 ms, depth late 0.023 ms, light clustering 0.060 ms, forward 25.669 ms, taa 2.589 ms, bloom down 0 0.295 ms, bloom down 1 0.046 ms, bloom down 2 0.040 ms, bloom down 3 0.035 ms, bloom down 4 0.028 ms, bloom up 3 0.023 ms, bloom up 2 0.021 ms, bloom up 1 0.015 ms, bloom up 0 0.016 ms, tonemap 0.209 ms, present 0.497 ms; thermal nominal -> nominal (worst nominal)
```

</details>
