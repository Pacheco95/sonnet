# iPad player frame-rate measurement

Measured 2026-10-10 at engine commit `40eed0d2a81cd24dc36b1de77a065f488229276a` (current main, including PR 169). No committed code changes.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang. Fresh basic-sample `sonnet_cook --platform ios`: 40 assets, six scenes and prefabs, zero warnings.
- Charging over the Mac connection: the temporary diagnostic reported `UIDeviceBatteryStateCharging` (2) in every run. Unlocked, awake, full screen, on a stand, Auto-Lock disabled, and unused during the runs, as specified by the owner.
- Native swapchain size 1640×2360. `UIScreen.maximumFramesPerSecond` reported 60 Hz; this is the display maximum, not a measurement of its active scanout cadence.
- Local, uncommitted `TARGETED_DEVICE_FAMILY=1,2` build override added this one line inside the generated plist’s `UIDeviceFamily` array after the existing phone entry:

```diff
+    <integer>2</integer>
```

Temporarily promoted the swapchain debug log to info and added a small Objective-C++ diagnostic beside the thermal query to log the display maximum and battery state once; both source files were restored with `git checkout` before committing.

## Results

Every run used a fresh process with `--console --terminate-existing`, the basic sample cooked bundle, and no render-size override. Light runs used `--scene scenes/main.scene.json --play 5 --screenshot light.png`; the reference used `--scene scenes/stress.scene.json --play 3 --screenshot stress.png`. Frame times average the last 100 frames; CPU excludes GPU/display waits. GPU values are as logged, without independent timestamp validation. All three runs logged `exit ok` and stayed nominal, so no thermal cooldown was required.

| Run | Present mode | Images | CPU ms | Interval ms | GPU ms | Thermal part, as logged |
|---|---|---:|---:|---:|---:|---|
| Light 1 | Fifo | 3 | 0.91 | 29.71 | 28.780 | `; thermal nominal -> nominal (worst nominal)` |
| Light 2 | Fifo | 3 | 0.82 | 29.83 | n/a | `; thermal nominal -> nominal (worst nominal)` |
| Stress reference | Fifo | 3 | 11.96 | 49.00 | 48.176 | `; thermal nominal -> nominal (worst nominal)` |

Each run logged the same swapchain line:

```text
swapchain 1640x2360, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
```

Final capture lines, with per-pass output removed:

```text
capture frame times over the last 100 frames: CPU 0.91 ms a frame, 29.71 ms apart; GPU 28.780 ms; thermal nominal -> nominal (worst nominal)
capture frame times over the last 100 frames: CPU 0.82 ms a frame, 29.83 ms apart; GPU n/a; thermal nominal -> nominal (worst nominal)
capture frame times over the last 100 frames: CPU 11.96 ms a frame, 49.00 ms apart; GPU 48.176 ms; thermal nominal -> nominal (worst nominal)
```

The forced-FIFO comparison was skipped because MoltenVK already selected FIFO despite the iOS Mailbox request.

## Interpretation

MoltenVK gave FIFO with three images on a display reporting a 60 Hz maximum, but the light scene averaged 29.71–29.83 ms, neither approximately 16.7 ms nor 8.3 ms, and not an integer multiple of 16.7 ms. These averages do not establish a refresh-multiple lock or uncapped rendering: even this light scene exceeded a 60 Hz frame budget, with the first run’s logged GPU time close to its interval, so the cap for a sufficiently inexpensive scene and the actual scanout cadence remain unverified. The stress and sustained intervals in [the earlier iPad measurement](ipad-m14.md) exceed 16.7 ms, so a refresh lock can only quantise them, not cause them; these short runs do not explain the sustained slowdown. This result applies only to this iPad, not the untested iPhone, whose different panel may support 120 Hz ProMotion.
