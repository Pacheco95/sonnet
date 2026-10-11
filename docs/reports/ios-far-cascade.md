# iOS far shadow cascade: 1024 versus 2048

[Issue 178](https://github.com/Pacheco95/sonnet/issues/178), measured 2026-10-11 on iPad, A16, iPadOS 27.0. Context: [the far-cascade mechanism](far-cascade-mechanism.md) and [the earlier iPad stress passes](ipad-stress-passes.md).

## Setup

Engine commit `259d32a9e8de9aae4bba894b200a3131d5d8751e`, the final revision of `perf/far-cascade-map`, including `c676c40` (`perf(renderer): shrink the far shadow cascade's map on phones (#173)`). The branch has since merged and been deleted; this report branch starts from its exact final commit. `ios-release`, RelWithDebInfo, Xcode Apple Clang 17; the player logs Apple A16 GPU, MoltenVK 1.4.2 and Vulkan 1.4.357. Fresh cooking for iOS produced 40 assets, six scenes and prefabs, zero warnings; both signed builds and installs passed.

The owner confirmed the iPad was unlocked, awake, on its stand, Auto-Lock off, charging through its Mac connection and unused during the runs. Every launch brought Sonnet to the foreground. Both sets began nominal and remained nominal, as reported by the capture line; battery state and GPU clocks were not independently instrumented. A temporary `TARGETED_DEVICE_FAMILY=1,2` build override and the extra `<integer>2</integer>` in the generated plist enabled the iPad, as in the earlier report. Neither override is committed.

A ran the branch's 1024 far map first, three fresh final captures and then `sf1024.png`. B deleted only the three-line mobile `farShadowMapSize` override in `apps/player/main.cpp`, rebuilt and installed the same player with the default 2048 far map, and ran three fresh final captures followed by `sf2048.png`. The source file was restored afterward; only this report is committed. The last installed app is the 2048 baseline. Both sets ran in the same session with the same charging connection; there was no sustained run or cooldown between sets.

## Runs

Each process used `--console --terminate-existing`, `--scene scenes/stress.scene.json --play 3 --render-size 1920x1080 --screenshot <file>`. All eight captures ended `exit ok` and logged `capture draws at 1920x1080, scaled to the window`. All GPU timings were numeric; no timestamp-driven retries were made. Each launch emitted the four previously observed MoltenVK warnings about blending on `VK_FORMAT_R32_UINT`; no engine errors were logged.

The six final captures below average the last 100 frames. Run values and thermal text are exactly as logged; arithmetic means are rounded to three decimals for CPU and GPU times and two for intervals. Shadow-factor runs are excluded from these means because they use a different shading term.

| Build / run | CPU ms | Interval ms | GPU total ms | Cascade 2 ms | Cascade 3 ms | Thermal part, as logged |
|---|---:|---:|---:|---:|---:|---|
| 1024 / 1 | 10.62 | 42.04 | 41.062 | 2.075 | 4.621 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 / 2 | 11.35 | 42.28 | 41.511 | 2.119 | 4.643 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 / 3 | 11.02 | 42.28 | 41.412 | 2.111 | 4.667 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 mean | 10.997 | 42.20 | 41.328 | 2.102 | 4.644 | All nominal |
| 2048 / 1 | 11.52 | 44.16 | 43.173 | 2.133 | 6.256 | `; thermal nominal -> nominal (worst nominal)` |
| 2048 / 2 | 11.48 | 44.00 | 43.191 | 2.157 | 6.322 | `; thermal nominal -> nominal (worst nominal)` |
| 2048 / 3 | 11.54 | 43.99 | 43.115 | 2.115 | 6.274 | `; thermal nominal -> nominal (worst nominal)` |
| 2048 mean | 11.513 | 44.05 | 43.160 | 2.135 | 6.284 | All nominal |

## Result and limits

Against the same-session 2048 baseline, the 1024 far map reduced mean cascade 3 by 1.640 ms (26.1%) and mean GPU total by 1.831 ms (4.2%), with cascade 2 nearly unchanged (2.135 to 2.102 ms). The interval also fell by 1.85 ms (4.2%), from 44.05 to 42.20 ms: these stress runs remain above the 16.67 ms 60 Hz FIFO floor, so the presentation cap did not hide this gain ([presentation measurements](ipad-framerate.md)), and the 33 ms target is still missed. Three fresh runs per build show a consistent short-run improvement on this iPad (cascade 3 ranges 4.621–4.667 versus 6.256–6.322 ms), but the fixed A-then-B order, unmeasured clocks and coarse thermal state do not establish an exact isolated cost or sustained performance. GPU timestamps were not independently validated with a Metal/Xcode capture, and the landscape render target is scaled into the device window; these measurements do not establish results on other devices.

The shadow-factor images were inspected: the scene and near shadows align, while distant objects have softer, slightly darker shadow coverage at 1024. Final shadow-quality acceptance remains the owner's visual judgment. Uncommitted screenshots and local logs are in `/tmp/sonnet-far-cascade-ios/build/far-cascade-captures/`: `far1024-1.png` through `far1024-3.png`, `far2048-1.png` through `far2048-3.png`, `sf1024.png` and `sf2048.png`. The optional iPhone was unavailable in an outside-sandbox device listing before and after the runs, so its build installation, portrait measurements and timestamp availability were not verified.
