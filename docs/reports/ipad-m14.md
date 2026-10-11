# M14 stress-scene measurement on iPad

Measured 2026-10-10 at engine commit `3e7b3b01906b0c27335534160db1ddade8210f85` (main including PR 167). No engine code changed.

## Setup

- iPad (A16), iPadOS 27.0; MoltenVK 1.4.2, Vulkan 1.4.357. No tool printed the GPU core count.
- `ios-release`, RelWithDebInfo, Xcode Apple Clang. Fresh `sonnet_cook --platform ios`: 40 assets, six scenes and prefabs, zero warnings.
- Charging over USB-C connected to the Mac; battery 58% at session start, 65% reported near the valid sustained launch. Case removed before all three measured runs. The owner confirmed unlocked, awake, full screen, on a stand, Auto-Lock disabled, unchanged portrait orientation and no other use; the sustained grid and these conditions were confirmed again after twenty minutes.
- Cooldown: **2 minutes 34.46 seconds** with the player stopped. The owner said the iPad felt cool and asked to shorten the original 15-minute requirement. This is a protocol deviation; touch is an observation, not a temperature measurement.
- With owner approval, the local build override `TARGETED_DEVICE_FAMILY=1,2` produced the following generated plist change, allowing iPad support. It is **uncommitted**; tracked source files remain unchanged:

```diff
 <key>UIDeviceFamily</key>
 <array>
     <integer>1</integer>
+    <integer>2</integer>
 </array>
```

Earlier windowed short captures and a sustained attempt stopped after 47 seconds are excluded. After the owner selected Full Screen in iPadOS, the fresh captures below confirmed a 1640×2360 window. The 1920×1080 target changes the scene aspect ratio and is scaled into that portrait window; the owner observed squashed shapes on screen.

## Results

Fresh processes used `--console --terminate-existing`, `--scene scenes/stress.scene.json`, `--play 3` or `--play 600`, and screenshots. Both 1080p runs logged `capture draws at 1920x1080, scaled to the window`. Values average the last 100 frames; CPU excludes GPU/display waits. Thermal first, last and worst span each run. GPU values are retained as logged, without independent timestamp validation.

| Run | CPU ms | Interval ms | Logged GPU ms | Thermal part, exactly as logged |
|---|---:|---:|---:|---|
| Native short, 1640×2360 | 11.64 | 49.78 | 48.623 | `; thermal nominal -> nominal (worst nominal)` |
| 1080p short, 1920×1080 | 11.19 | 43.87 | 43.188 | `; thermal nominal -> nominal (worst nominal)` |
| 1080p sustained, 1920×1080 | 11.49 | 45.45 | 44.533 | `; thermal nominal -> serious (worst serious)` |

The sustained run took **27 minutes 13.15 seconds** wall time for 600 seconds of simulation (about 36,000 fixed steps). Final line, without per-pass output:

```text
capture frame times over the last 100 frames: CPU 11.49 ms a frame, 45.45 ms apart; GPU 44.533 ms; thermal nominal -> serious (worst serious)
```

All three runs logged `exit ok`; retrieved PNGs were inspected and show the stress grid. No engine error or background transition after capture started was logged; two launch-time foreground messages preceded capture setup. Optional battery diagnostics were skipped because `pymobiledevice3` was not installed; battery, chip and ambient temperatures were not measured.

## Interpretation

The sustained final interval is **45.45 ms**, exceeding 33 ms by 12.45 ms; this iPad does not demonstrate the target, and its 1080p short interval already exceeds it. Thermal state rose from nominal to serious while the final interval was 3.6% longer than the short run, documenting a modest slowdown coinciding with elevated thermal state without establishing its timing or cause. This establishes one charging, case-free iPad stress-scene result under the shortened cooldown, with neither independently verified GPU timing nor a verified count of 10,000 visible surviving draws. An iPad has a larger chassis and cools better than a handset, so a pass here would be optimistic for a handset, and the iPad's A16 may have a different GPU core count than the 2022 flagship handset's A16.

Previous measurements: [earlier Apple measurement](iphone-m14.md) and [Galaxy S25 Ultra measurement](s25-m14.md).
