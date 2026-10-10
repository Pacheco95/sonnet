# iPhone sustained stress thermal measurement

One measurement for [issue 159](https://github.com/Pacheco95/sonnet/issues/159), 2026-10-10, following the [earlier iPhone M14 report](iphone-m14.md). No engine code changed.

## Setup

- Engine commit: `3212eaedfc8c99013c688393ed2135a322e714e2`, including PR 164.
- iOS 27.0; Apple A17 Pro GPU generation.
- `ios-release`, RelWithDebInfo, built with Xcode Apple Clang. The basic sample was freshly cooked with `sonnet_cook --platform ios`, then the player was built and installed.
- Charging through a USB-C cable connected to the Mac, as in the earlier run according to the owner.
- The player was stopped for 15 minutes 4 seconds between the short capture and sustained launch. The owner said the phone felt cool; this is a touch observation, not a temperature measurement.
- The owner confirmed the phone was unlocked, awake and on its stand, and agreed to keep Sonnet in front without other use. The stress grid was confirmed visible early and again after twenty minutes, with these conditions maintained throughout according to the owner.
- Both launches used `--terminate-existing`, `scenes/stress.scene.json` and an engine-written screenshot. The short run used `--play 3`; the sustained run used `--play 600` (600 seconds of simulation, about 36,000 fixed steps).
- Optional battery diagnostics were skipped because `pymobiledevice3` was not installed.

## Results

Times are the logged averages over the last 100 frames. CPU work excludes GPU/display waits; interval is time between frames. Thermal first, last and worst cover samples taken approximately once a second during each capture run.

| Run | CPU ms | Interval ms | GPU | Thermal part, exactly as logged |
|---|---:|---:|---|---|
| Short | 6.25 | 22.28 | n/a | `; thermal nominal -> nominal (worst nominal)` |
| Sustained | 9.66 | 43.16 | n/a | `; thermal nominal -> serious (worst serious)` |

Short capture line, omitting per-pass output:

```text
capture frame times over the last 100 frames: CPU 6.25 ms a frame, 22.28 ms apart; GPU n/a; thermal nominal -> nominal (worst nominal)
```

Sustained capture line, omitting per-pass output:

```text
capture frame times over the last 100 frames: CPU 9.66 ms a frame, 43.16 ms apart; GPU n/a; thermal nominal -> serious (worst serious)
```

Both runs logged `exit ok`. The sustained run took approximately 22 minutes 22 seconds from the initial player log to exit; its screenshot was written at 1290×2796, the retrieved engine-written PNG was inspected and shows the stress grid, and no engine error or post-launch background transition was logged.

The reported thermal state rose from nominal to serious during the sustained run, and its final 43.16 ms interval exceeded the 33 ms phone target while the state was serious. This documents a slowdown coinciding with elevated thermal state, consistent with thermal throttling, but one run cannot separate throttling from other causes or establish when the slowdown began; GPU cost, chip temperature and ambient temperature were not measured.
