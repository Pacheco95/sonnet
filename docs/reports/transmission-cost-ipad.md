# Transmission cost spike on the iPad

Measured 2026-10-18 for [#336](https://github.com/Pacheco95/sonnet/issues/336), using the same pass-structure spike as the [S25 report](transmission-cost-phone.md). This measures the opaque-scene copy, its mip chain and the split forward pass; transmission shading and the ADR's TransmissionTest and CommercialRefrigerator acceptance scenes are outside this measurement.

## Setup

- Engine commit `a87b878674db9442f2ab902e459388b06c5198f1`, from `spike/transmission-cost`; one signed `ios-release` / RelWithDebInfo build, Xcode Apple Clang, for every accepted run. The desktop cooker was rebuilt from this commit and cooked `apps/samples/basic --platform ios` into bundle version 18: 48 assets, 10 scenes and prefabs, zero warnings.
- iPad, Apple A16 GPU, iPadOS 27.0; MoltenVK 1.4.2, Vulkan 1.4.357. Every cold run logged native `1640x2360`, three `B8G8R8A8Unorm` swapchain images, FIFO, surface transform Identity. No `--render-size` was used.
- Local build override `TARGETED_DEVICE_FAMILY=1,2`; the signed app's `UIDeviceFamily` was verified as `[1, 2]`. No engine source changes are included in this report.
- The operator confirmed unlocked, awake, on its stand, Auto-Lock off, unused during measurements, with the USB charging connection unchanged. Captures launched Sonnet in the foreground. Charging current and battery state were not separately sampled.
- Each launch used `xcrun devicectl device process launch --console --terminate-existing --device <device-id> io.github.pacheco95.sonnet --scene scenes/stress.scene.json --play N --screenshot <run>.png`, adding `--transmission-spike` only for on runs. Device commands ran outside the sandbox. Failed trust/profile launches and an initial stale version-7 bundle were corrected before the first accepted capture.

## Cold (`--play 3`)

Six fresh processes in the order below. Each logged the intended `transmission cost spike: off` or `on`, a capture summary and `exit ok`. All six retrieved 1640×2360 screenshots show the expected stress grid and are pixel-identical.

Times below are milliseconds from the final capture summary (last 100 frames). **Spike passes** sums every `transmission ...` pass plus `forward transparent`; **clock reference** sums everything before `forward`, `taa`, every `bloom` pass, `tonemap` and `present`. These sums use the logged three-decimal values, so rounding can differ from the logged GPU total. Off has no spike passes. The sample count shown applies to every pass in that row.

| Run | Spike | CPU | Interval | GPU total | Forward | Spike passes | Clock reference | GPU samples | Thermal part, as logged |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| 1 | off | 12.05 | 47.07 | 46.487 | 32.354 | 0.000 | 14.132 | 7/100 | `; thermal nominal -> nominal (worst nominal)` |
| 1 | on | 14.01 | 51.16 | 49.978 | 33.322 | 2.942 | 13.715 | 49/100 | `; thermal nominal -> nominal (worst nominal)` |
| 2 | off | 11.87 | 47.33 | 46.510 | 32.399 | 0.000 | 14.111 | 9/100 | `; thermal nominal -> nominal (worst nominal)` |
| 2 | on | 13.69 | 51.21 | 49.979 | 33.435 | 2.941 | 13.604 | 53/100 | `; thermal nominal -> nominal (worst nominal)` |
| 3 | off | 11.90 | 47.33 | 46.408 | 32.386 | 0.000 | 14.023 | 7/100 | `; thermal nominal -> nominal (worst nominal)` |
| 3 | on | 13.79 | 50.99 | 49.781 | 33.167 | 2.956 | 13.656 | 51/100 | `; thermal nominal -> nominal (worst nominal)` |
| Mean | off | 11.94 | 47.24 | 46.468 | 32.380 | 0.000 | 14.089 | — | all nominal |
| Mean | on | 13.83 | 51.12 | 49.913 | 33.308 | 2.946 | 13.658 | — | all nominal |

GPU timestamps were available for only 7–9 of the last 100 off frames and 49–53 on frames. No pass was `n/a`, and no run was repeated to improve sample availability; the sparse, unequal samples limit the comparison ([#152](https://github.com/Pacheco95/sonnet/issues/152)).

The S25 report's normalization is `GPU total × first off clock reference / this run's clock reference`. Applied to this cold set, it gives means of 46.612 ms off and 51.644 ms on (+5.033 ms). Treat that as a sensitivity calculation, not a verified clock correction on this GPU: clocks were not measured, and unchanged-pass timings can shift through attribution or waiting as well as clock changes. Raw totals and all reference sums are retained above.

## Sustained (`--play 600`)

Both 600-second captures completed and produced screenshots that visually confirm the expected stress grid. The sustained-on launch's `devicectl` console disconnected and its wrapper returned 1, as in the earlier attempts, but the app's persisted summary says `exit ok`; this confirms the app finished the capture normally and the console loss did not interrupt it. This run used a temporary player build that wrote its final summary beside the screenshot in the app data directory. That instrumentation was removed from the working tree after retrieval.

Baseline launch: `2026-10-18T20:48:54.230491+00:00`; completion: `2026-10-18T21:18:53.351343+00:00` (UTC). It followed the final cold capture by at least 15 minutes idle. Screenshot retrieved and visually checked: the expected stress grid is visible at native resolution.

| Run | Spike | CPU ms | Interval ms | GPU total ms | Forward ms | Spike passes ms | Clock reference ms | GPU samples | Thermal part, as logged | Transition time |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| sustained-off, 17:48:54–18:18:53 | off | 11.80 | 50.28 | 49.459 | 34.732 | — | 14.728 | 8/100 | `; thermal nominal -> serious (worst serious)` | Not logged |
| sustained-on, 20:32:30–20:56:30 | on | 14.82 | 54.33 | 52.722 | 35.259 | 2.959 | 14.504 | 44/100 | `; thermal nominal -> serious (worst serious)` | Not logged |

Final baseline capture summary:

```text
over the last 100 frames: CPU 11.80 ms a frame, 50.28 ms apart; GPU 49.459 ms: cull 0.132 ms (8/100 samples), shadow cascade 0 0.093 ms (8/100 samples), shadow cascade 1 0.049 ms (8/100 samples), shadow cascade 2 1.274 ms (8/100 samples), shadow cascade 3 1.383 ms (8/100 samples), depth 2.491 ms (8/100 samples), depth pyramid 1.765 ms (8/100 samples), occlusion cull 0.031 ms (8/100 samples), depth late 0.021 ms (8/100 samples), light clustering 0.053 ms (8/100 samples), forward 34.732 ms (8/100 samples), taa 5.268 ms (8/100 samples), bloom down 0 0.636 ms (8/100 samples), bloom down 1 0.143 ms (8/100 samples), bloom down 2 0.033 ms (8/100 samples), bloom down 3 0.033 ms (8/100 samples), bloom down 4 0.020 ms (8/100 samples), bloom up 3 0.028 ms (8/100 samples), bloom up 2 0.026 ms (8/100 samples), bloom up 1 0.026 ms (8/100 samples), bloom up 0 0.108 ms (8/100 samples), tonemap 0.606 ms (8/100 samples), present 0.509 ms (8/100 samples); thermal nominal -> serious (worst serious)
```

Final sustained-on capture summary:

```text
over the last 100 frames: CPU 14.82 ms a frame, 54.33 ms apart; GPU 52.722 ms: cull 0.140 ms (44/100 samples), shadow cascade 0 0.137 ms (44/100 samples), shadow cascade 1 0.274 ms (44/100 samples), shadow cascade 2 1.270 ms (44/100 samples), shadow cascade 3 1.410 ms (44/100 samples), depth 2.608 ms (44/100 samples), depth pyramid 1.754 ms (44/100 samples), occlusion cull 0.030 ms (44/100 samples), depth late 0.020 ms (44/100 samples), light clustering 0.146 ms (44/100 samples), forward 35.259 ms (44/100 samples), transmission copy 2.253 ms (44/100 samples), transmission mip 1 0.011 ms (44/100 samples), transmission mip 2 0.320 ms (44/100 samples), transmission mip 3 0.044 ms (44/100 samples), transmission mip 4 0.028 ms (44/100 samples), transmission mip 5 0.011 ms (44/100 samples), transmission mip 6 0.026 ms (44/100 samples), transmission mip 7 0.029 ms (44/100 samples), transmission mip 8 0.035 ms (44/100 samples), transmission mip 9 0.057 ms (44/100 samples), transmission mip 10 0.060 ms (44/100 samples), transmission mip 11 0.051 ms (44/100 samples), forward transparent 0.034 ms (44/100 samples), taa 4.555 ms (44/100 samples), bloom down 0 0.626 ms (44/100 samples), bloom down 1 0.132 ms (44/100 samples), bloom down 2 0.035 ms (44/100 samples), bloom down 3 0.032 ms (44/100 samples), bloom down 4 0.024 ms (44/100 samples), bloom up 3 0.025 ms (44/100 samples), bloom up 2 0.025 ms (44/100 samples), bloom up 1 0.025 ms (44/100 samples), bloom up 0 0.091 ms (44/100 samples), tonemap 0.628 ms (44/100 samples), present 0.517 ms (44/100 samples); thermal nominal -> serious (worst serious)
```

The sustained-on run was 3.263 ms higher in raw GPU total and 4.05 ms slower in frame interval than the single sustained-off baseline; the spike passes account for 2.959 ms. Both runs reached serious thermal state. Their clock-reference sums are within 2% (14.728 and 14.504 ms), so scaling changes little: 53.54 ms on, +4.08 ms. The comparison is indicative rather than conclusive: the captures have only 8/100 versus 44/100 GPU timestamp samples, the on run used the temporary build and a later session, and thermal transition times were not logged. The cold spike adds 3.444 ms raw on average, exceeding ADR-0033’s 2 ms limit; the interval also rises by 3.88 ms, so FIFO did not hide the cold cost. On this GPU the copy itself is most of it (2.253 ms sustained), and `forward` is 0.93 ms slower cold with the split. The stress scene at native resolution misses the 33 ms target without the spike too (46.5 ms cold, 49.5 ms sustained), so that part of ADR-0033's rule cannot separate on from off here. Transmission shading and the ADR acceptance scenes remain unmeasured.

The sustained-on clock reference was corrected from 7.789 ms, which left out the passes after `forward`, when the report was brought to `main`.

