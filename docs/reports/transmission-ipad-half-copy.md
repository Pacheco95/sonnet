# A half-size transmission copy on the iPad

Measured for [#367](https://github.com/Pacheco95/sonnet/issues/367), part of [#349](https://github.com/Pacheco95/sonnet/issues/349), against [ADR-0033](../decisions/0033-transmission.md#off-and-mobile). This report records the measurements for the owner to decide the iOS default.

## Setup

- Base commit `59630d843536ed15d21f3359aaa08a3e319a7dfc`, current main at the start of this task, including the settled TAA implementation. One signed `ios-release` / RelWithDebInfo build, using Xcode Apple Clang, served every run.
- iPad, Apple A16 GPU, iPadOS 27.0.1. Every capture logged native `1640x2360`; no render-size override. The signed app's device family was verified as `[1, 2]`, with the local build override `TARGETED_DEVICE_FAMILY=1,2`; current main already targets both device families.
- Freshly fetched TAA fixtures, a rebuilt desktop cooker and a fresh iOS cook: 169 assets, nine scenes and prefabs, zero warnings. The packaged bundle's hash matched the fresh cook. The two portrait acceptance scenes are `transmission_test_phone` and `commercial_refrigerator_phone` in `apps/samples/taa`.
- The operator confirmed unlocked, awake on its stand, Auto-Lock off, unused during the runs, and the same charging connection throughout. Charging current, battery state, room temperature and GPU clocks were not independently sampled.
- Temporary, uncommitted player diagnostics persisted the log beside the captures, logged swapchain size and recorded thermal transitions at the existing once-per-second sampling point. The player source was restored after building. No renderer setting or frame scheduling was changed by the diagnostics. Only the launch's transmission and downscale options varied; iOS defaults remain unchanged.
- The Mac slept during the first half-size sustained capture and the following cooldown despite the initial sleep inhibitor. The device-side persisted log confirms that capture completed normally; the next run was delayed until morning. The runner resumed with wall-clock cooldowns and a stronger sleep inhibitor. The first two TransmissionTest sustained rows therefore belong to the evening session, and the remaining six to the morning session; room temperature was not controlled.
- Device commands ran outside the sandbox. Raw logs, captures and analysis are under `~/sonnet-measurements/transmission-ipad-half-copy/`. A host sleep inhibitor covered the capture batches. Each accepted capture used a fresh process with `--console --terminate-existing`, the intended transmission setting, a persisted frame-time summary and `exit ok`.

## Cold (`--play 3`)

T is TransmissionTest, R is CommercialRefrigerator. Each scene has four rounds of off, full-size on and half-size on, rotated in the execution order below. All rows started and ended nominal. Times are milliseconds over the final 100 frames. Transmission sums all `transmission ...` passes and `forward transparent`; clock reference sums every pass before `forward`, `taa`, every bloom pass, `tonemap` and `present`. Missing timestamps remain `n/a`; absent transmission passes are zero. Sums use rounded logged values.

| Scene / round | Setting | CPU | Interval | GPU total | Forward | Transmission passes | Clock reference | Thermal part, as logged |
|---|---|---:|---:|---:|---:|---:|---:|---|
| T / 1 | off | 0.77 | 20.66 | 19.963 | 9.394 | 0.000 | 10.568 | `; thermal nominal -> nominal (worst nominal)` |
| T / 1 | full (1) | 0.64 | 24.00 | 20.597 | 6.875 | 5.138 | 8.584 | `; thermal nominal -> nominal (worst nominal)` |
| T / 1 | half (2) | 0.57 | 26.67 | 21.793 | 6.897 | 3.865 | 11.033 | `; thermal nominal -> nominal (worst nominal)` |
| T / 2 | full (1) | 0.48 | 24.17 | 21.200 | 6.806 | 5.053 | 9.340 | `; thermal nominal -> nominal (worst nominal)` |
| T / 2 | half (2) | 0.42 | 22.50 | 18.413 | 6.869 | 3.937 | 7.605 | `; thermal nominal -> nominal (worst nominal)` |
| T / 2 | off | 0.36 | 20.66 | n/a | n/a | 0.000 | n/a | `; thermal nominal -> nominal (worst nominal)` |
| T / 3 | half (2) | 0.40 | 22.33 | 22.026 | 6.920 | 3.821 | 11.284 | `; thermal nominal -> nominal (worst nominal)` |
| T / 3 | off | 0.83 | 20.66 | 17.790 | 9.381 | 0.000 | 8.410 | `; thermal nominal -> nominal (worst nominal)` |
| T / 3 | full (1) | 0.48 | 24.12 | 23.295 | 6.807 | 5.207 | 11.282 | `; thermal nominal -> nominal (worst nominal)` |
| T / 4 | off | 0.41 | 20.66 | 12.725 | 9.476 | 0.000 | 3.248 | `; thermal nominal -> nominal (worst nominal)` |
| T / 4 | half (2) | 0.52 | 22.33 | 20.997 | 6.887 | 3.847 | 10.264 | `; thermal nominal -> nominal (worst nominal)` |
| T / 4 | full (1) | 0.65 | 24.16 | 23.341 | 6.807 | 5.245 | 11.290 | `; thermal nominal -> nominal (worst nominal)` |
| R / 1 | off | 0.55 | 19.51 | 16.411 | 7.391 | 0.000 | 9.023 | `; thermal nominal -> nominal (worst nominal)` |
| R / 1 | full (1) | 0.70 | 22.83 | 21.404 | 6.196 | 4.295 | 10.913 | `; thermal nominal -> nominal (worst nominal)` |
| R / 1 | half (2) | 0.64 | 21.16 | 20.374 | 6.122 | 2.640 | 11.613 | `; thermal nominal -> nominal (worst nominal)` |
| R / 2 | full (1) | 0.69 | 22.79 | 21.695 | 6.111 | 4.277 | 11.307 | `; thermal nominal -> nominal (worst nominal)` |
| R / 2 | half (2) | 0.44 | 21.49 | 21.027 | 6.340 | 2.836 | 11.851 | `; thermal nominal -> nominal (worst nominal)` |
| R / 2 | off | 0.56 | 19.50 | 16.919 | 7.391 | 0.000 | 9.526 | `; thermal nominal -> nominal (worst nominal)` |
| R / 3 | half (2) | 0.64 | 21.16 | 20.395 | 6.155 | 2.739 | 11.503 | `; thermal nominal -> nominal (worst nominal)` |
| R / 3 | off | 0.56 | 19.50 | 16.022 | 7.539 | 0.000 | 8.483 | `; thermal nominal -> nominal (worst nominal)` |
| R / 3 | full (1) | 0.41 | 22.94 | 22.987 | 6.229 | 4.979 | 11.779 | `; thermal nominal -> nominal (worst nominal)` |
| R / 4 | off | 0.37 | 19.33 | 15.667 | 7.256 | 0.000 | 8.411 | `; thermal nominal -> nominal (worst nominal)` |
| R / 4 | half (2) | 0.41 | 21.17 | 20.260 | 6.249 | 3.256 | 10.756 | `; thermal nominal -> nominal (worst nominal)` |
| R / 4 | full (1) | 0.58 | 22.83 | 22.034 | 6.183 | 4.165 | 11.687 | `; thermal nominal -> nominal (worst nominal)` |

GPU totals are sums of per-pass means that can use different subsets of frames, rather than means of complete GPU frames ([#152](https://github.com/Pacheco95/sonnet/issues/152)). Cold sample coverage ranges from 0–78 per pass out of 100 frames for TransmissionTest and 3–29 for CommercialRefrigerator. TransmissionTest off / 2 has unavailable timestamps. No capture was repeated to improve timestamp availability. Representative cold images were retrieved and inspected: the transmission grid is sideways within the portrait frame, and the refrigerator fills it with its frosted door visible when transmission is on.

| Scene | Setting | Mean GPU total | Numeric totals / runs | Mean interval |
|---|---|---:|---:|---:|
| T | off | 16.826 | 3 / 4 | 20.660 |
| T | full (1) | 22.108 | 4 / 4 | 24.113 |
| T | half (2) | 20.807 | 4 / 4 | 23.457 |
| R | off | 16.255 | 4 / 4 | 19.460 |
| R | full (1) | 22.030 | 4 / 4 | 22.848 |
| R | half (2) | 20.514 | 4 / 4 | 21.245 |

## Sustained (`--play 600`)

Per scene the order is off, half, half, off. Every fresh process followed at least 15 minutes idle and logged nominal at its first thermal sample; the overnight interruption extended the idle before T / 3 by several hours. Dates and launch times are UTC on 2026-10-22. Thermal transitions are sampled about once a second and times below are seconds since the first iteration; no fair sample occurred in either transition to serious. All eight persisted logs include the requested transmission setting, a frame-time summary and `exit ok`; each screenshot was retrieved. Full size was not rerun because the raw cold GPU differences do not pass the 2 ms rule. Sustained timestamp coverage ranges from 0–12 samples per pass out of 100; R / 4 off has no numeric GPU total or complete clock reference.

| Scene / run | Setting | Start UTC | CPU | Interval | GPU total | Forward | Transmission passes | Clock reference | Thermal part, as logged | First non-nominal |
|---|---|---|---:|---:|---:|---:|---:|---:|---|---|
| T / 1 | off | 00:49:31 | 0.64 | 23.51 | 23.253 | 10.790 | 0.000 | 12.463 | `; thermal nominal -> serious (worst serious)` | serious at 349.7 s |
| T / 2 | half (2) | 01:18:39 | 0.82 | 25.68 | 24.082 | 8.125 | 4.352 | 11.605 | `; thermal nominal -> serious (worst serious)` | serious at 612.2 s |
| T / 3 | half (2) | 08:33:23 | 0.77 | 24.16 | 23.919 | 7.588 | 4.030 | 12.299 | `; thermal nominal -> nominal (worst nominal)` | never |
| T / 4 | off | 09:03:45 | 0.63 | 22.50 | 21.767 | 10.074 | 0.000 | 11.693 | `; thermal nominal -> nominal (worst nominal)` | never |
| R / 1 | off | 09:32:31 | 0.55 | 20.83 | 15.115 | 8.147 | 0.000 | 6.969 | `; thermal nominal -> nominal (worst nominal)` | never |
| R / 2 | half (2) | 10:00:46 | 0.61 | 22.66 | 22.228 | 6.695 | 2.781 | 12.750 | `; thermal nominal -> nominal (worst nominal)` | never |
| R / 3 | half (2) | 10:30:04 | 0.58 | 22.67 | 22.307 | 6.913 | 2.792 | 12.602 | `; thermal nominal -> nominal (worst nominal)` | never |
| R / 4 | off | 10:59:22 | 0.54 | 21.00 | n/a | n/a | 0.000 | n/a | `; thermal nominal -> nominal (worst nominal)` | never |

## Clock-reference sensitivity

The [phone spike](transmission-cost-phone.md) scales each run as `GPU total × first off reference / this run's reference`. The per-run references above retain the inputs. These are the resulting means where both total and reference are numeric; unavailable rows are excluded, never replaced by zero.

| Scene | Cold off | Cold full | Cold half | Sustained off | Sustained half |
|---|---:|---:|---:|---:|---:|
| T | 27.907 | 23.253 | 22.177 | 19.695 | 21.241 |
| R | 16.571 | 17.407 | 16.208 | 19.570 | 15.851 |

This is a sensitivity calculation, not a validated clock correction on the iPad: the unchanged passes can change through waiting, timing attribution and unequal available samples. For example, T off / 4 cold has a 3.248 ms reference versus 10.568 ms in the first off run, while its forward pass is nearly unchanged (9.476 versus 9.394 ms); scaling its entire frame produces an implausibly large baseline and reverses the sign of the added cost. The refrigerator's corrected cold half result similarly falls below off despite its consistently longer interval. These reversals do not establish free transmission or a cold pass. Raw totals, sample availability, changed-pass costs and intervals remain necessary context.

## Results against ADR-0033

**TransmissionTest.** Cold raw added GPU time is +5.282 ms at full size and +3.981 ms at half size, both above 2 ms; intervals increase by 3.453 and 2.797 ms, while the sum of forward and transmission passes increases by 2.568 and 1.344 ms. Half-size sustained averages 24.001 ms versus 22.510 ms off (+1.491 ms), with an interval increase of 1.915 ms; full-size sustained was not measured in this build. Both settings reached serious in the evening pair (off at 349.7 s, half at 612.2 s), and both stayed nominal in the morning reverse-order pair, so neither pair ended hotter with half but the session change prevents isolating a thermal benefit. The pairs establish final-window intervals and available GPU totals below 33 ms and the recorded thermal outcomes, but sparse GPU samples, unmeasured clocks and room temperature do not establish an exact isolated GPU cost or a longer-term equilibrium.

**CommercialRefrigerator.** Cold raw added GPU time is +5.775 ms at full size and +4.259 ms at half size, both above 2 ms; intervals increase by 3.387 and 1.785 ms, while the sum of forward and transmission passes increases by 3.215 and 1.690 ms. Half-size sustained averages 22.267 ms (two numeric totals) versus 15.115 ms off (only one of two totals is numeric, +7.152 ms), with an interval increase of 1.750 ms; full-size sustained was not measured in this build. All four sustained runs stayed nominal, so the half-size copy reached no worse thermal state than off in either order. The pairs establish final-window intervals and available GPU totals below 33 ms and the recorded thermal outcomes, but sparse GPU samples, unmeasured clocks and room temperature do not establish an exact isolated GPU cost or a longer-term equilibrium.

| Half-size rule | TransmissionTest | CommercialRefrigerator |
|---|---|---|
| Cold added total GPU time at most 2 ms | Not demonstrated: raw +3.981 ms, one off total unavailable, correction unreliable | Not demonstrated: raw +4.259 ms, correction unreliable |
| Sustained frame under 33 ms | Met in the final 100-frame summaries; GPU and interval below 33 ms | Met by both final-window intervals and the available GPU totals; one off total is unavailable |
| Sustained thermal state no worse than off | Met in the two observed pairs: serious/serious, nominal/nominal; session-limited | Met in the two observed pairs: nominal throughout |

**The half-size copy does not establish an overall pass of ADR-0033.** It cuts the measured transmission-pass work and improves intervals relative to full size, but these data cannot demonstrate the cold total-GPU rule. The owner has the per-run data to decide the default; this report changes no iOS default.

## Limits and verification

GPU timestamps were not independently validated with an Xcode encoder capture, and actual GPU clocks, charging current, battery state and room temperature were not measured. The 600-second play duration advances a fixed simulation; it is not a guarantee of exactly ten wall-clock minutes, nor do the last-100-frame summaries prove every frame throughout a sustained run stayed under 33 ms. Longer thermal equilibrium and other iPads or iPhones remain unverified. The overnight host-sleep interruption is a session confound, although device-side logs and screenshots preserve the completed results.

Validation: fixture fetch, current-main desktop cooker build, zero-warning iOS cook, signed iOS build/install, 24 cold and eight sustained captures with persisted summaries and `exit ok`, retrieved sustained screenshots and inspected representative images, report privacy scan, and `python3 tools/check_docs.py`. Only this report is committed.
