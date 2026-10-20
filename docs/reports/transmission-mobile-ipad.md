# Transmission on and off on the iPad

Measured for [#344](https://github.com/Pacheco95/sonnet/issues/344) and [#342](https://github.com/Pacheco95/sonnet/issues/342), against [ADR-0033](../decisions/0033-transmission.md#off-and-mobile). This report records measurements; the owner decides the iOS default.

## Setup

- Base commit `18a92b56238ab78a8d945b3020dda1791fbab031`, from `feat/player-transmission-switch`. One signed `ios-release` / RelWithDebInfo build using Xcode Apple Clang served all rows below. Temporary, uncommitted player instrumentation added an info-level file sink and logged thermal state changes at the existing once-per-second sampling point. It changed no renderer settings or frame scheduling and was removed from the checkout after building.
- iPad, Apple A16 GPU, iPadOS 27.0.1. Native swapchain `1640x2360`, three `B8G8R8A8Unorm` images, FIFO, Identity surface transform; no render-size override. The build used the local command-line `TARGETED_DEVICE_FAMILY=1,2` override; the signed app device family was verified as `[1, 2]`.
- Freshly fetched TAA fixtures; the rebuilt cooker produced 169 assets, nine scenes and prefabs, zero warnings. Both portrait acceptance scenes came from `apps/samples/taa`. Renderer settings were the player defaults, including TAA, a 1024 far shadow map and a 1024 environment cube limit; only `--transmission on|off` varied.
- The operator confirmed unlocked, awake on its stand, Auto-Lock off, unused, with the same charging connection throughout. Every accepted sustained run followed at least 15 minutes idle and logged nominal at its first frame. Charging current, battery state and GPU clocks were not sampled independently.
- Device commands ran outside the sandbox. Raw logs and representative images are under `~/sonnet-measurements/transmission-mobile-ipad/`. All reported runs have persisted summaries, the intended transmission setting and `exit ok`. Original uninstrumented cold captures and an initial sustained capture whose console lost its summary are archived separately and excluded.
- The first reported sustained capture completed normally, but its console stayed attached after exit. Its persisted log was recovered before continuing. Remaining sustained captures launched detached; logs were retrieved after a fixed 20-minute completion wait, extended by ten minutes only if unfinished. This avoids dependence on the console connection and adds idle time after completion. Host sleep also stretched the initial cooldown before T / 2; a wall-clock cooldown timer and host sleep prevention were used from T / 2 onward. Initial nominal is a coarse thermal classification, not proof of identical starting temperatures.

## Cold (`--play 3`)

Four rounds per scene: off/on, on/off, off/on, on/off, each a fresh process. T means TransmissionTest; R means CommercialRefrigerator. Times are milliseconds over the final 100 frames. Transmission sums every `transmission ...` pass and `forward transparent`; clock reference sums every pass before `forward`, `taa`, all bloom passes, `tonemap` and `present`. An absent transmission path is 0; `n/a` preserves missing timestamps rather than treating them as zero. Reference sums use rounded logged pass values.

| Scene / round | Transmission | CPU | Interval | GPU total | Forward | Transmission passes | Clock reference | Thermal part, as logged |
|---|---|---:|---:|---:|---:|---:|---:|---|
| T / 1 | off | 0.71 | 20.51 | 18.453 | 9.356 | 0.000 | 9.100 | `; thermal nominal -> nominal (worst nominal)` |
| T / 1 | on | 0.76 | 23.94 | 22.401 | 6.881 | 5.228 | 10.294 | `; thermal nominal -> nominal (worst nominal)` |
| T / 2 | on | 0.81 | 23.66 | 22.942 | 6.864 | 5.160 | 10.918 | `; thermal nominal -> nominal (worst nominal)` |
| T / 2 | off | 0.74 | 20.33 | 20.112 | 9.317 | 0.000 | 10.796 | `; thermal nominal -> nominal (worst nominal)` |
| T / 3 | off | 0.72 | 20.34 | 20.236 | 9.368 | 0.000 | 10.867 | `; thermal nominal -> nominal (worst nominal)` |
| T / 3 | on | 0.78 | 23.95 | 23.004 | 6.911 | 5.226 | 10.867 | `; thermal nominal -> nominal (worst nominal)` |
| T / 4 | on | 0.77 | 23.83 | 23.026 | 6.919 | 5.237 | 10.871 | `; thermal nominal -> nominal (worst nominal)` |
| T / 4 | off | 0.64 | 20.47 | 12.894 | 9.530 | 0.000 | 3.365 | `; thermal nominal -> nominal (worst nominal)` |
| R / 1 | off | 0.58 | 19.17 | 17.511 | 7.354 | 0.000 | 10.158 | `; thermal nominal -> nominal (worst nominal)` |
| R / 1 | on | 0.55 | 22.50 | 21.633 | 6.158 | 3.992 | 11.485 | `; thermal nominal -> nominal (worst nominal)` |
| R / 2 | on | 0.59 | 22.50 | 22.442 | 6.254 | 4.804 | 11.383 | `; thermal nominal -> nominal (worst nominal)` |
| R / 2 | off | 0.59 | 19.04 | 16.598 | 7.340 | 0.000 | 9.258 | `; thermal nominal -> nominal (worst nominal)` |
| R / 3 | off | 0.58 | 18.99 | 14.797 | 7.304 | 0.000 | 7.491 | `; thermal nominal -> nominal (worst nominal)` |
| R / 3 | on | 0.56 | 22.49 | 21.385 | 6.071 | 4.034 | 11.277 | `; thermal nominal -> nominal (worst nominal)` |
| R / 4 | on | 0.65 | 22.49 | 21.389 | 6.211 | 4.145 | 11.033 | `; thermal nominal -> nominal (worst nominal)` |
| R / 4 | off | 0.58 | 19.17 | 17.384 | 7.264 | 0.000 | 10.120 | `; thermal nominal -> nominal (worst nominal)` |

GPU timestamp sample counts vary by pass and run, as in [#152](https://github.com/Pacheco95/sonnet/issues/152). Cold captures had between 1 and 22 samples per pass out of 100 frames. Sustained captures had 0–15; the two sustained off rows with GPU `n/a` also lack complete clock references. Runs were not repeated to improve timestamp availability.

Representative cold screenshots were retrieved and inspected: the transmission grid is sideways within the portrait image, with all three material rows visible, and the refrigerator fills the portrait frame. On restores the grid’s tinted and rough glass and the refrigerator’s frosted door; off shows the blend fallback. The sustained refrigerator image has its animated door open at the 600-second simulation endpoint, whereas the cold image has it closed; cold-to-sustained differences therefore do not isolate heat or clocks. These are image sanity checks, not a repeat of the desktop TAA acceptance tests.

## Sustained (`--play 600`)

Each scene used off/on/on/off. Start timestamps below are UTC launch times; thermal transition times are elapsed seconds since the first frame, sampled about once per second. The first nominal entry establishes the thermal state at the start. No intermediate fair state is inferred when the API moved directly from nominal to serious. Capture duration counts 36,000 fixed simulation steps, so wall time exceeds ten minutes.

| Scene / run | Start UTC | Transmission | CPU | Interval | GPU total | Forward | Transmission passes | Clock reference | Thermal part, as logged | Observed transitions |
|---|---|---|---:|---:|---:|---:|---:|---:|---|---|
| T / 1 | 2026-10-20 00:04:54 | off | 0.58 | 22.50 | n/a | n/a | 0.000 | n/a | `; thermal nominal -> serious (worst serious)` | nominal at 0.0 s; serious at 758.5 s |
| T / 2 | 2026-10-20 01:35:56 | on | 0.73 | 25.71 | 25.194 | 7.894 | 5.476 | 11.823 | `; thermal nominal -> serious (worst serious)` | nominal at 0.0 s; serious at 487.6 s |
| T / 3 | 2026-10-20 02:10:56 | on | 0.67 | 25.83 | 25.173 | 7.845 | 5.529 | 11.800 | `; thermal nominal -> serious (worst serious)` | nominal at 0.0 s; serious at 402.2 s |
| T / 4 | 2026-10-20 02:45:57 | off | 0.65 | 22.50 | 21.444 | 10.393 | 0.000 | 11.050 | `; thermal nominal -> nominal (worst nominal)` | nominal at 0.0 s |
| R / 1 | 2026-10-20 03:20:58 | off | 0.53 | 20.66 | 17.886 | 8.139 | 0.000 | 9.747 | `; thermal nominal -> nominal (worst nominal)` | nominal at 0.0 s |
| R / 2 | 2026-10-20 03:55:59 | on | 0.58 | 24.00 | 22.732 | 6.744 | 4.219 | 11.769 | `; thermal nominal -> nominal (worst nominal)` | nominal at 0.0 s |
| R / 3 | 2026-10-20 04:31:00 | on | 0.60 | 23.99 | 23.532 | 6.928 | 4.184 | 12.422 | `; thermal nominal -> nominal (worst nominal)` | nominal at 0.0 s |
| R / 4 | 2026-10-20 05:06:01 | off | 0.61 | 20.49 | n/a | n/a | 0.000 | n/a | `; thermal nominal -> nominal (worst nominal)` | nominal at 0.0 s |

## Reading the results

**TransmissionTest.** Cold GPU means are 17.924 ms off and 22.843 ms on (+4.920 ms, shading and split included), exceeding the ADR’s 2 ms cold allowance; mean interval rises from 20.413 to 23.845 ms. Sustained on totals are 25.194 and 25.173 ms, under 33 ms, versus off totals of `n/a` and 21.444 ms; mean interval rises from 22.500 to 25.770 ms. Both on runs reached serious (487.6 and 402.2 s), whereas off reached serious once (758.5 s) and stayed nominal once, so the second pair does not meet the “thermal state no worse than off” condition. The opposite-order pairs reproduce the interval increase but do not establish a clock/thermal plateau or a full GPU delta for the first pair, whose off timestamps are missing.

**CommercialRefrigerator.** Cold GPU means are 16.572 ms off and 21.712 ms on (+5.140 ms, shading and split included), exceeding the 2 ms allowance; mean interval rises from 19.093 to 22.495 ms. Sustained on totals are 22.732 and 23.532 ms, under 33 ms, versus off totals of 17.886 ms and `n/a`; mean interval rises from 20.575 to 23.995 ms. All four sustained runs stayed nominal, so neither pair showed a worse thermal state with transmission on. The pairs support a sustained interval cost and nominal thermal behavior in these conditions, but missing off timestamps limit the second GPU comparison and they do not prove a steady-state plateau.

Clock-reference scaling, if used, is `GPU total × first off reference / this run reference` within the same scene. All per-run reference sums are retained for the owner; clocks were not measured, and unchanged-pass attribution can move with waiting, so this is a sensitivity calculation rather than a verified clock correction. Missing components leave the reference and any such normalization unavailable.

Unverified: GPU clocks, independent timestamp accuracy, charging current and battery state, and a thermal/clock plateau beyond these captures. This report makes no iOS default decision and includes no S25 measurement or committed engine changes.
