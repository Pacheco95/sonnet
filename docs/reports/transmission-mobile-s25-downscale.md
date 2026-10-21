# A smaller transmission copy on the S25

Measured 2026-10-20 for [#347](https://github.com/Pacheco95/sonnet/issues/347), the follow-up to [transmission-mobile](transmission-mobile.md) ([#342](https://github.com/Pacheco95/sonnet/issues/342)). That report kept transmission off on Android because TransmissionTest reached a worse thermal status with it on. This one measures `RendererSettings::transmissionDownscale`, a copy of the opaque scene built at 1/n of the view's size on each side ([rendering.md](../rendering.md#transmission)), against the same rules: cold added GPU time at most 2 ms, sustained frame under 33 ms, and a thermal status no worse than with transmission off ([ADR-0033](../decisions/0033-transmission.md), *Phone player*).

**Result: Android keeps transmission off.** A half-size copy is the cheapest cold (+0.8 and +1.1 ms) and holds the frame, but the sustained thermal rule is not shown to be met: the phone reaches status 2 within 20 minutes with transmission off as well as on, and in the controlled round the copy reaches each status 1.5 to 4 minutes sooner.

## Setup

- Galaxy S25 Ultra, Adreno 830, Android 16, release APK of this branch at `0debc7c` (the later commits only reformat and fix a test), FIFO, a 1080×2340 swapchain, no `--render-size`, the phone player's other settings unchanged. Only `--transmission on|off` and `--transmission-downscale N` differ between runs.
- Scenes, cooking, charging over USB, the 15-minute cooldown and the skin check are the recipe of [transmission-mobile](transmission-mobile.md): every sustained run started at thermal status 0 with the skin at 37.6 to 37.9 °C.
- Cold: `--play 3`, four rounds per scene and setting (off, full size `on1`, half `on2`, quarter `on4`), the order rotated each round. GPU times are corrected for the clock the way that report does (the passes transmission does not change, scaled to the first off run of the scene).
- Sustained: `--play 1200`, one run per scene and setting. The thermal status and GPU clock are sampled every 30 s, so a time below has a 30 s resolution.
- Round 1 was run in a room whose temperature was not controlled. Round 2 was run at a controlled, stable 24 °C, in the opposite order within each scene. A phone call of about 5 seconds came during round 1; no clock file has a gap over 35 s.
- Raw logs and scripts are outside the repository.

## Cold (`--play 3`)

| Scene | Full size (1) | Half (2) | Quarter (4) |
|---|---|---|---|
| TransmissionTest | +1.34 ms | **+0.78 ms** | +1.76 ms |
| CommercialRefrigerator | +1.64 ms | **+1.08 ms** | +2.76 ms |

Added GPU time against off (13.33 and 13.64 ms), the mean of four corrected runs. Half size is 0.55 ms below the full-size copy in both scenes. Quarter size is worse than full size: its copy reads 16 texels per output texel and the correction does not hold as well where the clock differs (the added passes alone are 5.5 ms against 4.9 ms at full size). Half and full size meet the 2 ms rule in both scenes; quarter size does not on CommercialRefrigerator.

## Sustained (`--play 1200`)

Minutes are from the start of the run to the first sample at that status.

| Round | Scene, setting | GPU | Interval | Status 1 | Status 2 | End skin, status |
|---|---|---|---|---|---|---|
| 1 | TransmissionTest, half | 13.98 ms | 16.65 ms | 12.5 | never | 40.0 °C, 1 |
| 1 | TransmissionTest, off | 13.36 ms | 16.65 ms | 10.5 | 20.0 | 42.0 °C, 2 |
| 1 | CommercialRefrigerator, half | 15.73 ms | 16.64 ms | 5.5 | 12.0 | 42.0 °C, 2 |
| 1 | CommercialRefrigerator, off | 14.02 ms | 16.64 ms | 9.0 | never | 40.1 °C, 1 |
| 2 (24 °C) | TransmissionTest, off | 13.34 ms | 16.65 ms | 10.0 | 18.5 | 42.0 °C, 2 |
| 2 (24 °C) | TransmissionTest, half | 14.23 ms | 16.65 ms | 7.5 | 15.0 | 42.0 °C, 2 |
| 2 (24 °C) | CommercialRefrigerator, off | 13.95 ms | 16.62 ms | 9.5 | 18.0 | 42.0 °C, 2 |
| 2 (24 °C) | CommercialRefrigerator, half | 14.13 ms | 16.64 ms | 8.0 | 14.0 | 42.0 °C, 2 |

The frame stays far under 33 ms and the interval stays at the 60 fps cap in every run. The GPU times are the last 100 frames and are raw: the clock moves between 734 and 900 MHz, so the +0.2 to +1.7 ms between pairs is not a clean cost.

## Against ADR-0033's phone thresholds

- Cold, the added GPU time is at most 2 ms: **met** at half size, +0.78 and +1.08 ms (and at full size; not at quarter size on the refrigerator).
- Sustained, the frame stays under 33 ms: **met**, at most 15.7 ms.
- Sustained, the thermal status reached is no worse than with transmission off: **not shown.**
  - In the controlled round every run ends at status 2, with off as well, so the final status ties. The half-size copy reaches status 1 1.5 to 2.5 minutes sooner and status 2 3.5 to 4 minutes sooner.
  - In round 1 the two scenes went opposite ways: TransmissionTest ended cooler with the copy (status 1 against 2), CommercialRefrigerator hotter (2 against 1). One of four pairs ended worse with the copy on, which the rule does not allow.
  - Round 1 and round 2 disagree on the off runs themselves (CommercialRefrigerator off ended at status 1 and then at status 2), so one run per setting cannot separate a 0.2 to 0.9 ms difference from the phone's own variation.

## Decision

**Android keeps transmission off.** A half-size copy cuts the cold cost by about 40 % (+0.8 and +1.1 ms against +1.3 and +1.6 ms) and passes the cold and frame rules, but the thermal rule is not met: the copy brings both status steps forward in the controlled round, and ended hotter in one of the four round-1 pairs. The default stays off in `apps/player/main.cpp`; `--transmission on --transmission-downscale 2` runs the half-size copy for a capture.

What this establishes and what it does not: one S25, one build, one run per scene and setting in each round, and the full-size copy was not rerun at 24 °C, so the table above compares the half-size copy with off and not with the full-size copy sustained. The phone reaches status 2 on both scenes after 14 to 20 minutes with the copy off, so the rule "no worse than off" is close to a tie by the end of 20 minutes and mostly a question of how soon each step arrives; a later change that cuts the sustained GPU load further (a shorter chain, no pass for a view where glass covers little of the screen) would be measured with the same recipe, in the controlled room. [#348](https://github.com/Pacheco95/sonnet/issues/348) (the default per device) can use these figures, and [#349](https://github.com/Pacheco95/sonnet/issues/349) measures the iPad with the smaller copy.
