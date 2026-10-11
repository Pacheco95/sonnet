# iOS far shadow cascade: 512 versus 1024

[Issue 181](https://github.com/Pacheco95/sonnet/issues/181), measured 2026-10-11 on iPad, A16, iPadOS 27.0. Context: [the mechanism and Android 512 result](far-cascade-mechanism.md), [the earlier iOS 1024 result](ios-far-cascade.md) and [iPad stress-pass conditions](ipad-stress-passes.md).

## Setup

Engine commit `499c1ef0c28513faa83066cd12f76f7cfce45f3d`, `perf/far-map-512`; `ios-release`, RelWithDebInfo, Xcode Apple Clang 17. The player reports Apple A16 GPU, Vulkan 1.4.357 and MoltenVK 1.4.2. Fresh cooking for iOS produced 40 assets, six scenes and prefabs, zero warnings. All three signed builds and installs passed. A temporary, uncommitted `TARGETED_DEVICE_FAMILY=1,2` override enabled the iPad; the signed app's generated plist confirms both families.

The owner confirmed the iPad was unlocked, awake, on its stand, Auto-Lock off, connected for charging and unused during captures. Every launch brought Sonnet to the foreground, with the same connection throughout. Each set began nominal and stayed nominal according to the capture logs; charging state and GPU clocks were not independently instrumented.

Order: A (512), three final captures then `sf512.png`; B (1024), three final captures then `sf1024.png`; A again (512), three final captures. B changed only `desc.renderer.farShadowMapSize` locally in `apps/player/main.cpp`; the file was restored from Git before the last build. Only this report is committed, and the last installed app is the branch's 512 version. No sustained run or deliberate cooldown occurred between sets.

## Runs

Each fresh process used `--console --terminate-existing --scene scenes/stress.scene.json --play 3 --render-size 1920x1080 --screenshot <file>`. All eleven captures ended `exit ok` and logged `capture draws at 1920x1080, scaled to the window`. GPU totals and passes were numeric throughout; no timestamp-driven retries were made. The two extra captures used `--shading-term shadow-factor` and are excluded from the means.

Values below are exactly as logged, averaging the last 100 frames per run; set means are arithmetic, rounded to three decimals for CPU/GPU and two for intervals.

| Build / run | CPU ms | Interval ms | GPU total ms | Cascade 2 ms | Cascade 3 ms | Thermal part, as logged |
|---|---:|---:|---:|---:|---:|---|
| 512 / 1 | 11.07 | 41.83 | 40.791 | 2.110 | 4.094 | `; thermal nominal -> nominal (worst nominal)` |
| 512 / 2 | 11.14 | 41.83 | 40.935 | 2.139 | 4.053 | `; thermal nominal -> nominal (worst nominal)` |
| 512 / 3 | 10.96 | 42.04 | 41.218 | 2.106 | 4.124 | `; thermal nominal -> nominal (worst nominal)` |
| 512 mean | 11.057 | 41.90 | 40.981 | 2.118 | 4.090 | All nominal |
| 1024 / 1 | 11.12 | 42.62 | 41.695 | 2.102 | 4.658 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 / 2 | 11.19 | 42.38 | 41.613 | 2.102 | 4.644 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 / 3 | 11.04 | 42.33 | 41.450 | 2.114 | 4.659 | `; thermal nominal -> nominal (worst nominal)` |
| 1024 mean | 11.117 | 42.44 | 41.586 | 2.106 | 4.654 | All nominal |
| 512 again / 1 | 10.81 | 41.71 | 40.925 | 2.122 | 4.109 | `; thermal nominal -> nominal (worst nominal)` |
| 512 again / 2 | 11.24 | 41.89 | 40.983 | 2.108 | 4.138 | `; thermal nominal -> nominal (worst nominal)` |
| 512 again / 3 | 10.95 | 41.78 | 40.945 | 2.112 | 4.096 | `; thermal nominal -> nominal (worst nominal)` |
| 512 again mean | 11.000 | 41.79 | 40.951 | 2.114 | 4.114 | All nominal |

## Result and limits

Against the same-session 1024 set, the first and repeated 512 sets reduced cascade 3 by 0.563 ms (12.1%) and 0.539 ms (11.6%), and GPU total by 0.605 ms (1.5%) and 0.635 ms (1.5%). The two 512 sets agree closely: cascade 3 rose only 0.024 ms (0.6%) in the repeat while GPU total fell 0.030 ms, so the data show no coherent overall slowdown between the sets. Interval fell from 42.44 ms at 1024 to 41.90 and 41.79 ms at 512, gains of 0.54 and 0.65 ms; these remain above the 16.67 ms [60 Hz FIFO floor](ipad-framerate.md) and still miss 33 ms. Three fresh runs per set, with the repeated A bracketing B, support a consistent short-run gain on this iPad, but do not establish sustained performance, exact isolated GPU work, GPU clocks or results on other devices.

Both shadow-factor images were inspected: scene geometry and near shadows align, while distant rows at 512 lose some small shadow detail and appear softer. Final shadow-quality acceptance remains the owner's visual judgment. GPU timestamps were not independently validated with a Metal/Xcode capture, and the landscape render target is scaled into the device window. The iPhone was reachable, but the owner confirmed only the iPad was ready; iPhone installation, portrait measurements and timestamp availability were therefore not verified.

Uncommitted screenshots and logs are in `/tmp/sonnet-far-map-512-ios/build/far-cascade-captures/`. Device output names are `far512-1.png` through `far512-3.png`, `far1024-1.png` through `far1024-3.png`, `far512b-1.png` through `far512b-3.png`, `sf512.png` and `sf1024.png`; local copies have the prefix `ipad-`.
