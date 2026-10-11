# iOS far-cascade shadow LOD measurement

Measured 2026-10-11 for [issue 185](https://github.com/Pacheco95/sonnet/issues/185), from `feat/shadow-lod` at commit `3ddaa163965245cc73bb922b48111e77b1138dd1`. PR 184 was still open when the measurement started; this report is on `mac-validation/shadow-lod-ios`.

## Setup

- iPad, A16, iPadOS 27.0; `ios-release`, RelWithDebInfo, Xcode Apple Clang, MoltenVK. The owner confirmed unlocked, awake, on its stand, Auto-Lock off, charging over the same Mac connection and unused during the runs. Charging was held constant by that setup; no independent battery diagnostic was added.
- A fresh host `sonnet_cook --platform ios` built the version-4 bundle from this branch. All sets used that same bundle, the stress scene, `--play 3`, `--render-size 1920x1080`, and a 1024 far shadow map. The local `TARGETED_DEVICE_FAMILY=1,2` override produced both device-family entries in the app manifest; it is not committed.
- A: the branch's `shadowLodCascade = 3`; B: only that line removed from the iOS branch of `gameDesc`; A again: the source restored from Git. Each change was rebuilt and installed, with no other renderer setting changed. No engine code is committed.
- Each capture was a fresh `--console --terminate-existing` process. The first run of each set began nominal, and every run ended nominal with worst nominal. All eleven captures logged `exit ok` and `capture draws at 1920x1080, scaled to the window`; all nine timing runs had numeric GPU samples. Means cover the last 100 frames per run as logged. One shadow-factor capture followed each of the first two sets and is excluded from timing means.

## Runs

Times in milliseconds; per-run values and thermal text are exactly as logged, means rounded to three decimals. Execution order is range on, range off, range on again.

| Build / run | CPU | Interval | GPU total | Cascade 2 | Cascade 3 | Thermal part, as logged |
|---|---:|---:|---:|---:|---:|---|
| Range on / 1 | 11.15 | 39.00 | 38.032 | 2.108 | 1.245 | `; thermal nominal -> nominal (worst nominal)` |
| Range on / 2 | 11.18 | 38.95 | 37.918 | 2.090 | 1.209 | `; thermal nominal -> nominal (worst nominal)` |
| Range on / 3 | 11.23 | 39.12 | 38.124 | 2.092 | 1.233 | `; thermal nominal -> nominal (worst nominal)` |
| **Range on mean** | 11.187 | 39.023 | 38.025 | 2.097 | 1.229 | All nominal |
| Range off / 1 | 11.48 | 42.33 | 41.252 | 2.095 | 4.648 | `; thermal nominal -> nominal (worst nominal)` |
| Range off / 2 | 10.69 | 42.33 | 41.519 | 2.112 | 4.667 | `; thermal nominal -> nominal (worst nominal)` |
| Range off / 3 | 11.37 | 42.45 | 41.427 | 2.126 | 4.641 | `; thermal nominal -> nominal (worst nominal)` |
| **Range off mean** | 11.180 | 42.370 | 41.399 | 2.111 | 4.652 | All nominal |
| Range on again / 1 | 9.84 | 39.12 | 38.099 | 2.127 | 1.176 | `; thermal nominal -> nominal (worst nominal)` |
| Range on again / 2 | 9.97 | 38.95 | 37.852 | 2.146 | 1.174 | `; thermal nominal -> nominal (worst nominal)` |
| Range on again / 3 | 10.96 | 38.71 | 37.921 | 2.096 | 1.198 | `; thermal nominal -> nominal (worst nominal)` |
| **Range on again mean** | 10.257 | 38.927 | 37.957 | 2.123 | 1.183 | All nominal |

## Result

Against range off, the first range-on set reduced cascade 3 from 4.652 to 1.229 ms (73.6%) and GPU total from 41.399 to 38.025 ms (8.2%); the second range-on set reached 1.183 and 37.957 ms. The two range-on means differ by -0.046 ms in cascade 3 and -0.067 ms in GPU total, with no thermal-state change; this brackets short-session drift but does not measure clocks. Interval fell from 42.370 ms off to 39.023 and 38.927 ms on, so the improvement was visible above the 60 Hz FIFO cap rather than hidden below it ([presentation context](ipad-framerate.md)). Three short runs per set establish repeatability for this device and scene in this session, not sustained performance, independent timestamp accuracy, or a general result for other scenes and devices; the GPU frame still exceeds 33 ms.

## Pictures and limits

Uncommitted captures and sanitized logs are in `build/shadow-lod-ios/`: `iPad-lod-1.png` through `iPad-lod-3.png`, `iPad-nolod-1.png` through `iPad-nolod-3.png`, `iPad-lod2-1.png` through `iPad-lod2-3.png`, and the shadow-factor pair `iPad-sf-lod.png` / `iPad-sf-nolod.png`. The shadow-factor pair differs by 0.0924 levels of 255 on average across RGB channels, maximum 44, with 0.1145% of pixels exceeding 8 in any channel; the images remain available for the owner to judge the far shadows.

Far-cascade shadow triangle count: **not printed** in the info-level player logs; no diagnostic code was added. The optional iPhone comparison was skipped at the owner’s request; this session does not verify its timestamp availability or LOD performance. No independent GPU capture, clock measurement, sustained run or battery diagnostic was taken.
