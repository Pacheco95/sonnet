# iPad blank-scene frame floor

Measured 2026-10-10 at engine commit `5ce911a017fa6bff0432aef7487ab538b7e4db5b` (current main including PR 170). Only this report is committed.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang. Charging over the Mac connection: a temporary battery diagnostic reported charging (state 2) in every run; the display maximum was 60 Hz.
- Unlocked, awake, full screen, on a stand, Auto-Lock disabled and unused during runs, as specified by the owner. Every capture remained thermally nominal, so no cooldown was required.
- Local `TARGETED_DEVICE_FAMILY=1,2` override; the generated plist’s `UIDeviceFamily` array gained this one uncommitted line after its existing phone entry:

```diff
+    <integer>2</integer>
```

The local version-3 blank scene copied exactly two entities from `main.scene.json`: Camera (`Transform`, `Camera`, with its audio listener removed) and Sun (`Transform`, `DirectionalLight`). It contained no other entities, meshes, local lights, environment, scripts or physics bodies; no extra entity was needed. The iOS cook reported 40 assets, seven scenes and prefabs, zero warnings. The player initially started the project’s default scene, then logged `playing "Basic": 2 entities` for every blank capture; no scene-load warning or error followed, and the retrieved native blank screenshot showed a uniform background. Each process nevertheless emitted four startup MoltenVK warnings about blending on `VK_FORMAT_R32_UINT`; the same warnings occurred with main, so these were not warning-free launches.

Temporarily promoted the swapchain log to info and added a few Objective-C++ lines beside the thermal query to log display maximum and charging state. Both source files were restored with `git checkout`; the blank scene was deleted and the normal bundle re-cooked (six scenes and prefabs, zero warnings) before committing. An Xcode GPU frame capture was skipped because device capture setup and per-encoder extraction were not already available as a quick command; these GPU timings remain independently unvalidated.

## Runs

Fresh processes used `--console --terminate-existing --play 5`, `--scene scenes/blank.scene.json` or `--scene scenes/main.scene.json`, and a distinct screenshot for each run. The first pass ran blank native, blank scaled, main native, main scaled; the second repeated that order. Native was 1640×2360; scaled used `--render-size 1080x1920`, a 1.867-fold pixel reduction with a changed aspect ratio. All four scaled captures logged `capture draws at 1080x1920, scaled to the window`; every run logged `exit ok` and the same swapchain line:

```text
swapchain 1640x2360, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
```

Times average the last 100 frames, with CPU excluding GPU/display waits. Values below are as logged; no run was repeated to replace missing timestamps, and none reported `n/a`.

| Configuration / pass | CPU ms | Interval ms | GPU total ms | Thermal part, as logged |
|---|---:|---:|---:|---|
| blank native / 1 | 0.30 | 16.68 | 13.698 | `; thermal nominal -> nominal (worst nominal)` |
| blank scaled / 1 | 0.40 | 16.67 | 12.655 | `; thermal nominal -> nominal (worst nominal)` |
| main native / 1 | 0.84 | 31.66 | 30.804 | `; thermal nominal -> nominal (worst nominal)` |
| main scaled / 1 | 0.89 | 18.00 | 16.713 | `; thermal nominal -> nominal (worst nominal)` |
| blank native / 2 | 0.38 | 16.66 | 13.971 | `; thermal nominal -> nominal (worst nominal)` |
| blank scaled / 2 | 0.36 | 16.67 | 13.674 | `; thermal nominal -> nominal (worst nominal)` |
| main native / 2 | 0.80 | 30.00 | 29.106 | `; thermal nominal -> nominal (worst nominal)` |
| main scaled / 2 | 0.93 | 17.89 | 16.830 | `; thermal nominal -> nominal (worst nominal)` |

## Per-pass GPU times

Cells are means in ms with numeric-run count in parentheses. `— (0)` means the pass was absent, not a measured zero. These are all passes exceeding 1 ms in any run; “rest” is the mean of each run’s sum of all other logged passes. Rounding means the displayed sums can differ slightly from the logged total.

| Pass | Blank native | Blank 1080×1920 | Main native | Main 1080×1920 |
|---|---:|---:|---:|---:|
| forward | 0.767 (2) | 0.645 (2) | 19.834 (2) | 10.398 (2) |
| taa | 8.785 (2) | 8.556 (2) | 4.712 (2) | 2.537 (2) |
| depth pyramid | — (0) | — (0) | 1.674 (2) | 0.961 (2) |
| bloom down 0 | 1.136 (2) | 1.094 (2) | 0.556 (2) | 0.294 (2) |
| Rest, summed | 3.147 (2) | 2.870 (2) | 3.178 (2) | 2.579 (2) |

At native size, bloom down 2 and bloom up 1 cost about the same in blank and main, but most shared passes did not form an additive constant: blank’s TAA, bloom down 0/1/3/4, bloom up 0/2/3, light clustering, tonemap and present were higher, while main’s depth and forward were higher. Within main, forward, TAA, depth pyramid, depth, bloom down 0/1/2, bloom up 0/1 and tonemap fell with pixel count; bloom down 3/4, bloom up 2/3 and present changed little. Within blank, depth, light clustering, TAA, bloom down 0/2/3/4, bloom up 1/2/3 and present showed weak changes relative to the pixel reduction, while bloom down 1, bloom up 0 and tonemap declined more clearly. Skinning, cull, shadow cascades 0–3, shadow point 0 faces 0–5, shadow spot 1, occlusion cull, depth late and particles appeared only in main; the shadow passes and depth late changed little with resolution, while the smaller compute-pass differences are too small and variable to assign a reliable scaling law from two samples.

## Interpretation

Blank native establishes a scene-free rendering floor of 13.835 ms mean logged GPU work, with 16.68/16.66 ms intervals under 60 Hz FIFO, dominated by TAA at 8.785 ms (about 64% of that GPU total). Reducing pixels 1.867-fold left blank at 13.165 ms mean GPU work, whereas main fell from 29.955 to 16.772 ms, with forward and TAA approximately following pixel count in main. Main added about 16.121 ms net GPU work at native size and 3.607 ms at 1080×1920, chiefly through forward (19.834/10.398 ms), although lower TAA and post-processing costs in main show that subtracting blank is not an exact additive decomposition. TAA dominates blank and forward dominates main, but FIFO hides the blank floor below 16.7 ms in the observed intervals, and unvalidated engine timestamps, changed aspect ratio and no independent per-encoder capture prevent an exact cost or active scanout determination. This short iPad measurement does not explain the sustained slowdown in [the stress report](ipad-m14.md), complements [the presentation measurement](ipad-framerate.md), and does not extend to the untested iPhone.

<details>
<summary>Complete final capture lines, in run order</summary>

blank native / 1:

```text
capture frame times over the last 100 frames: CPU 0.30 ms a frame, 16.68 ms apart; GPU 13.698 ms: depth 0.141 ms, light clustering 0.795 ms, forward 0.850 ms, taa 8.432 ms, bloom down 0 1.132 ms, bloom down 1 0.258 ms, bloom down 2 0.045 ms, bloom down 3 0.045 ms, bloom down 4 0.038 ms, bloom up 3 0.043 ms, bloom up 2 0.046 ms, bloom up 1 0.048 ms, bloom up 0 0.289 ms, tonemap 0.857 ms, present 0.679 ms; thermal nominal -> nominal (worst nominal)
```

blank scaled / 1:

```text
capture frame times over the last 100 frames: CPU 0.40 ms a frame, 16.67 ms apart; GPU 12.655 ms: depth 0.173 ms, light clustering 0.773 ms, forward 0.866 ms, taa 7.799 ms, bloom down 0 1.036 ms, bloom down 1 0.163 ms, bloom down 2 0.049 ms, bloom down 3 0.058 ms, bloom down 4 0.046 ms, bloom up 3 0.039 ms, bloom up 2 0.040 ms, bloom up 1 0.041 ms, bloom up 0 0.170 ms, tonemap 0.660 ms, present 0.742 ms; thermal nominal -> nominal (worst nominal)
```

main native / 1:

```text
capture frame times over the last 100 frames: CPU 0.84 ms a frame, 31.66 ms apart; GPU 30.804 ms: skinning 0.047 ms, cull 0.075 ms, shadow cascade 0 0.250 ms, shadow cascade 1 0.052 ms, shadow cascade 2 0.158 ms, shadow cascade 3 0.142 ms, shadow point 0 face 0 0.056 ms, shadow point 0 face 1 0.085 ms, shadow point 0 face 2 0.042 ms, shadow point 0 face 3 0.091 ms, shadow point 0 face 4 0.060 ms, shadow point 0 face 5 0.078 ms, shadow spot 1 0.130 ms, depth 0.327 ms, depth pyramid 1.674 ms, occlusion cull 0.031 ms, depth late 0.023 ms, light clustering 0.012 ms, particles 0.031 ms, forward 20.768 ms, taa 4.689 ms, bloom down 0 0.566 ms, bloom down 1 0.139 ms, bloom down 2 0.052 ms, bloom down 3 0.027 ms, bloom down 4 0.021 ms, bloom up 3 0.023 ms, bloom up 2 0.023 ms, bloom up 1 0.024 ms, bloom up 0 0.072 ms, tonemap 0.524 ms, present 0.510 ms; thermal nominal -> nominal (worst nominal)
```

main scaled / 1:

```text
capture frame times over the last 100 frames: CPU 0.89 ms a frame, 18.00 ms apart; GPU 16.713 ms: skinning 0.069 ms, cull 0.177 ms, shadow cascade 0 0.255 ms, shadow cascade 1 0.085 ms, shadow cascade 2 0.145 ms, shadow cascade 3 0.144 ms, shadow point 0 face 0 0.051 ms, shadow point 0 face 1 0.087 ms, shadow point 0 face 2 0.053 ms, shadow point 0 face 3 0.085 ms, shadow point 0 face 4 0.053 ms, shadow point 0 face 5 0.071 ms, shadow spot 1 0.118 ms, depth 0.172 ms, depth pyramid 0.961 ms, occlusion cull 0.044 ms, depth late 0.027 ms, light clustering 0.025 ms, particles 0.041 ms, forward 10.362 ms, taa 2.554 ms, bloom down 0 0.282 ms, bloom down 1 0.042 ms, bloom down 2 0.037 ms, bloom down 3 0.026 ms, bloom down 4 0.024 ms, bloom up 3 0.024 ms, bloom up 2 0.024 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.177 ms, present 0.463 ms; thermal nominal -> nominal (worst nominal)
```

blank native / 2:

```text
capture frame times over the last 100 frames: CPU 0.38 ms a frame, 16.66 ms apart; GPU 13.971 ms: depth 0.197 ms, light clustering 0.396 ms, forward 0.684 ms, taa 9.137 ms, bloom down 0 1.140 ms, bloom down 1 0.260 ms, bloom down 2 0.041 ms, bloom down 3 0.042 ms, bloom down 4 0.041 ms, bloom up 3 0.041 ms, bloom up 2 0.044 ms, bloom up 1 0.047 ms, bloom up 0 0.309 ms, tonemap 0.867 ms, present 0.725 ms; thermal nominal -> nominal (worst nominal)
```

blank scaled / 2:

```text
capture frame times over the last 100 frames: CPU 0.36 ms a frame, 16.67 ms apart; GPU 13.674 ms: depth 0.126 ms, light clustering 0.393 ms, forward 0.424 ms, taa 9.313 ms, bloom down 0 1.152 ms, bloom down 1 0.185 ms, bloom down 2 0.050 ms, bloom down 3 0.059 ms, bloom down 4 0.049 ms, bloom up 3 0.033 ms, bloom up 2 0.040 ms, bloom up 1 0.041 ms, bloom up 0 0.229 ms, tonemap 0.771 ms, present 0.810 ms; thermal nominal -> nominal (worst nominal)
```

main native / 2:

```text
capture frame times over the last 100 frames: CPU 0.80 ms a frame, 30.00 ms apart; GPU 29.106 ms: skinning 0.047 ms, cull 0.074 ms, shadow cascade 0 0.250 ms, shadow cascade 1 0.080 ms, shadow cascade 2 0.193 ms, shadow cascade 3 0.151 ms, shadow point 0 face 0 0.056 ms, shadow point 0 face 1 0.106 ms, shadow point 0 face 2 0.042 ms, shadow point 0 face 3 0.092 ms, shadow point 0 face 4 0.054 ms, shadow point 0 face 5 0.082 ms, shadow spot 1 0.147 ms, depth 0.373 ms, depth pyramid 1.674 ms, occlusion cull 0.032 ms, depth late 0.029 ms, light clustering 0.011 ms, particles 0.033 ms, forward 18.900 ms, taa 4.735 ms, bloom down 0 0.546 ms, bloom down 1 0.128 ms, bloom down 2 0.049 ms, bloom down 3 0.029 ms, bloom down 4 0.027 ms, bloom up 3 0.024 ms, bloom up 2 0.025 ms, bloom up 1 0.025 ms, bloom up 0 0.056 ms, tonemap 0.531 ms, present 0.505 ms; thermal nominal -> nominal (worst nominal)
```

main scaled / 2:

```text
capture frame times over the last 100 frames: CPU 0.93 ms a frame, 17.89 ms apart; GPU 16.830 ms: skinning 0.071 ms, cull 0.140 ms, shadow cascade 0 0.241 ms, shadow cascade 1 0.089 ms, shadow cascade 2 0.167 ms, shadow cascade 3 0.141 ms, shadow point 0 face 0 0.055 ms, shadow point 0 face 1 0.092 ms, shadow point 0 face 2 0.048 ms, shadow point 0 face 3 0.089 ms, shadow point 0 face 4 0.065 ms, shadow point 0 face 5 0.076 ms, shadow spot 1 0.118 ms, depth 0.183 ms, depth pyramid 0.961 ms, occlusion cull 0.051 ms, depth late 0.027 ms, light clustering 0.020 ms, particles 0.065 ms, forward 10.433 ms, taa 2.520 ms, bloom down 0 0.307 ms, bloom down 1 0.042 ms, bloom down 2 0.038 ms, bloom down 3 0.027 ms, bloom down 4 0.025 ms, bloom up 3 0.024 ms, bloom up 2 0.024 ms, bloom up 1 0.016 ms, bloom up 0 0.016 ms, tonemap 0.181 ms, present 0.476 ms; thermal nominal -> nominal (worst nominal)
```

</details>
