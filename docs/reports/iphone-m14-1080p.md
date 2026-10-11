# iPhone M14 target at 1080p

Measurement for [issue 175](https://github.com/Pacheco95/sonnet/issues/175), 2026-10-11. No engine code changed.

## Setup

- Engine commit: `99c725ce1786302e2e6a3fc219131717aead8b7e`, current `origin/main` at worktree creation, including the render-size and thermal capture features.
- iPhone, Apple A17 Pro GPU generation, iOS 27.0.
- `ios-release`, RelWithDebInfo, built with Xcode 26.2 Apple Clang; basic sample freshly cooked with `sonnet_cook --platform ios`, then the player built, signed and installed from an isolated worktree.
- Charging through a USB-C cable to the Mac throughout both runs, matching the [earlier sustained thermal run](iphone-m14-thermal.md).
- Player stopped for **15 minutes 18 seconds** between the short capture and sustained launch. During cooldown the owner said it felt slightly warm and suggested charging as a possible cause; this is a touch observation, not a temperature measurement or an established cause.
- Owner confirmed the phone was unlocked, awake and on its stand, with Sonnet in front and no other use; the stress grid was confirmed during the sustained run and these conditions were confirmed maintained through completion.
- Both fresh-process launches used `--terminate-existing --scene scenes/stress.scene.json --render-size 1080x1920`; short `--play 3`, one sustained `--play 600` (600 seconds of simulation, about 36,000 fixed steps). Both logs confirm `capture draws at 1080x1920, scaled to the window` and `exit ok`.
- The sustained process took **16 minutes 41 seconds** wall time. Both engine-written PNGs were retrieved and inspected: 1080×1920, showing the alternating box-and-icosphere stress grid. No engine error was logged during the sustained run.
- Optional battery diagnostics skipped: `pymobiledevice3` was not installed.

## Results

Times are milliseconds, averaged over the last 100 frames before capture. CPU work excludes GPU/display waits; interval is time between frames. Thermal first, last and worst cover the capture run's sampled states.

| Run | CPU ms | Interval ms | Logged GPU ms | Thermal part, exactly as logged |
|---|---:|---:|---|---|
| Short | 5.09 | 17.96 | 17.286 | `; thermal nominal -> nominal (worst nominal)` |
| Sustained | 7.68 | 29.03 | n/a | `; thermal nominal -> serious (worst serious)` |

Capture lines, omitting per-pass output:

```text
capture frame times over the last 100 frames: CPU 5.09 ms a frame, 17.96 ms apart; GPU 17.286 ms; thermal nominal -> nominal (worst nominal)
capture frame times over the last 100 frames: CPU 7.68 ms a frame, 29.03 ms apart; GPU n/a; thermal nominal -> serious (worst serious)
```

<details>
<summary>Short run: complete logged timing line with available per-pass values</summary>

```text
capture frame times over the last 100 frames: CPU 5.09 ms a frame, 17.96 ms apart; GPU 17.286 ms: cull 0.094 ms, shadow cascade 0 0.025 ms, shadow cascade 1 0.182 ms, shadow cascade 2 0.778 ms, shadow cascade 3 5.010 ms, depth 1.344 ms, depth pyramid 0.526 ms, occlusion cull 0.033 ms, depth late 0.037 ms, light clustering 0.011 ms, forward 6.653 ms, taa 1.820 ms, bloom down 0 0.184 ms, bloom down 1 0.034 ms, bloom down 2 0.009 ms, bloom down 3 0.009 ms, bloom down 4 0.003 ms, bloom up 3 0.019 ms, bloom up 2 0.012 ms, bloom up 1 0.006 ms, bloom up 0 0.044 ms, tonemap 0.105 ms, present 0.348 ms; thermal nominal -> nominal (worst nominal)
```

</details>

The sustained final average interval meets the 33 ms target at 1080p by 3.97 ms, though this last-100-frame average does not prove every frame stayed under 33 ms or establish a 2022 flagship result; native-size comparisons are in [iphone-m14.md](iphone-m14.md) and [iphone-m14-thermal.md](iphone-m14-thermal.md). Thermal state rose from nominal to serious while the final interval increased from the short run, a slowdown coinciding with elevated thermal state, but one run cannot separate throttling from other causes or establish when the slowdown began. Sustained GPU cost and headroom remain unverified because every sustained pass reports `n/a`; the short nonzero readings are preserved as logged, with the platform timestamp limitation documented in the [timestamp investigation](iphone-m14-timestamps.md), and chip and ambient temperatures were not measured. A 60 Hz FIFO cap may hide GPU headroom below 16.7 ms, as the [iPad frame-rate measurement](ipad-framerate.md) illustrates, but that measurement was on an iPad: the iPhone panel may be 120 Hz ProMotion and its present mode was not logged here.
