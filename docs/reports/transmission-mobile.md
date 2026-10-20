# Transmission on the S25

Measured 2026-10-19 and 2026-10-20 for [#342](https://github.com/Pacheco95/sonnet/issues/342), the phone part of [#254](https://github.com/Pacheco95/sonnet/issues/254) ([ADR-0033](../decisions/0033-transmission.md), *Phone player*). This runs the finished implementation, with the transmission shading, on the ADR's two acceptance scenes at the phone's native resolution, with the setting on and off. The pass structure alone was measured earlier on the [S25](transmission-cost-phone.md) and the [iPad](transmission-cost-ipad.md). The iPad's measurements of these scenes are in [transmission-mobile-ipad](transmission-mobile-ipad.md) ([#344](https://github.com/Pacheco95/sonnet/issues/344)) and decide iOS below.

## Setup

- Galaxy S25 Ultra, Adreno 830, Android 16, Vulkan 1.4 device on the 1.3 path ([ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)). Release APK of the tree at `18a92b5`, FIFO, a 1080×2340 swapchain, no `--render-size`. The phone player's renderer settings (far shadow map 512, shadow LOD cascade 3, environment cube at most 1024, TAA and bloom on); only `--transmission on|off` differs between runs.
- Scenes: `scenes/transmission_test_phone.scene.json` and `scenes/commercial_refrigerator_phone.scene.json` of `apps/samples/taa`, cooked for `android` and run from the app's files directory. Both are still: TransmissionTest stands on its end, CommercialRefrigerator is seen from the front with its door open. The camera is the same on both devices (docs/player.md, "Transmission on a phone").
- Charging over USB for every run. Cold runs are `--play 3`, sustained `--play 600`, and one pair `--play 1200`. The thermal status is Android's (0 none, 1 light, 2 moderate), sampled with the skin temperature and the GPU clock every 30 s. Before each sustained run: a fixed 15-minute cooldown and a skin reading within 1 °C of the first run's (37.5 to 37.6 °C), so every one started at status 0.
- The GPU time is the player's `capture frame times` line, the last 100 frames, with the interval between frames. The 60 fps request makes the interval 16.65 ms when the GPU keeps up.
- The Adreno clock moves between 734 and 900 MHz and cannot be pinned, and transmission raises it. As in the pass-structure report, each run's total is scaled by the passes transmission does not change (everything before `forward`, and `taa`, the bloom chain, `tonemap` and `present`), relative to the first off run of its scene. The correction holds only while the clocks are close: where they are far apart, below, the raw totals are given.
- Raw logs and the scripts are outside the repository.

## Cold (`--play 3`)

Four rounds per scene, alternating the order each round, thermal status 0 throughout. The correction is to the first off run of the scene.

| Scene | Off | On | Added |
|---|---|---|---|
| TransmissionTest | 13.26 ms | 14.50 ms | **+1.24 ms** |
| CommercialRefrigerator | 14.56 ms | 15.95 ms | **+1.39 ms** |

TransmissionTest's four pairs add 0.6 to 1.7 ms, and CommercialRefrigerator's 0.3 to 1.9 ms: the refrigerator's raw totals show no difference, because the clock was higher with transmission on (the reference passes took 4.0 to 4.7 ms, against 4.8 with it off). Of the added time, the transmission passes and `forward transparent` are 4.4 to 5.0 ms for TransmissionTest and 3.0 to 3.5 ms for the refrigerator, and TransmissionTest's `forward` falls from 9.3 to 9.9 ms to 5.4 to 6.3 ms (the split moves the blended draws out of it), so the shading and the split together are the added time above.

Cold, the added GPU time is at most 2 ms: **met in both scenes**, with the refrigerator's worst pair 0.1 ms under.

## Sustained (`--play 600`)

Two pairs per scene in opposite orders. The corrected column is to the scene's first off run.

| Scene, run | Raw GPU | Corrected | Interval | Status 1 at | Final status, clock |
|---|---|---|---|---|---|
| TransmissionTest, off first | 13.34 ms | 13.34 ms | 16.64 ms | never | 0, 832 MHz |
| TransmissionTest, on second | 14.34 ms | 14.73 ms | 16.64 ms | 7.5 min | 1, 900 MHz |
| TransmissionTest, on first | 14.19 ms | 14.76 ms | 16.65 ms | 7.0 min | 1, 832 MHz |
| TransmissionTest, off second | 13.32 ms | 13.42 ms | 16.65 ms | 9.5 min | 1, 832 MHz |
| CommercialRefrigerator, off first | 14.26 ms | 14.26 ms | 16.65 ms | 10.0 min | 1, 734 MHz |
| CommercialRefrigerator, on second | 13.49 ms | 15.93 ms | 16.65 ms | 6.0 min | 1, 443 MHz |
| CommercialRefrigerator, on first | 13.74 ms | 16.07 ms | 16.64 ms | 6.5 min | 1, 900 MHz |
| CommercialRefrigerator, off second | 13.92 ms | 14.28 ms | 16.64 ms | 10.0 min | 1, 660 MHz |

Means of the corrected totals: TransmissionTest 13.38 ms off and 14.75 ms on (**+1.37 ms**); CommercialRefrigerator 14.27 ms off and 16.00 ms on (**+1.73 ms**). Every frame stays far under 33 ms and the interval stays at the 60 fps cap.

At 600 s the thermal state had not settled: the off runs reached status 1 at 9.5 min or at the very end, or not at all. The on runs reached it after 6 to 7.5 min in both orders and both scenes, so transmission brings the first thermal step forward by 3 to 4 minutes. Whether the state it reaches is worse is what 600 s cannot say, so one TransmissionTest pair was run for 1200 s (off first, then on, both from status 0 and 37.5 to 37.8 °C):

| Run | Raw GPU | Interval | Status 1 at | Status 2 at | Final status, skin, clock |
|---|---|---|---|---|---|
| off | 13.33 ms | 16.66 ms | 18 min | never | 1, 40.0 °C, 525 MHz |
| on | 16.79 ms | 16.82 ms | 8 min | 16 min | **2**, 42.0 °C, 734 MHz |

With transmission on, the phone throttled to 734 MHz at status 2 and the frame took 16.8 ms, 3.5 ms more than off, and the interval slipped past the 60 fps cap (59.5 frames a second). The corrected figure for this run (12.8 ms) is not used: the clocks of the two runs differ by a third, which the correction cannot hold. The pair has one order only.

## Against ADR-0033's phone thresholds

- Cold, the added GPU frame time is at most 2 ms: **met**, +1.24 and +1.39 ms.
- Sustained, the frame stays under 33 ms: **met**, at most 16.8 ms.
- Sustained, the thermal status reached is no worse than with transmission off: **not met in TransmissionTest.** At 1200 s on reaches status 2 and off stops at 1, and in the first 600 s pair on reaches 1 where off stays at 0. CommercialRefrigerator ends at status 1 either way at 600 s, but it was not run for longer, and its off runs reach status 1 only at the end of the window.

## Decision: Android

**The Android player keeps transmission off.** One of the two acceptance scenes fails the sustained thermal rule, and the rule is the ADR's; a failed budget leaves the platform on the fallback and is a valid completion of #342. The cold cost is within budget (+1.2 to +1.4 ms), so what a later change would have to bring down is the sustained heat: at the 60 fps cap the GPU is 13 to 14 ms busy of 16.7 off and transmission adds 1.4 to 1.7 ms to that. The editor and the desktop player leave the setting on.

What this does and does not establish: one S25, one build, and for the 1200 s pair one order. The two 600 s pairs, in both orders, agree that transmission reaches status 1 sooner; the 1200 s pair is the only one that shows the state reached, and the refrigerator has no run past 600 s. A change that cuts the heat (a smaller copy, a cheaper chain, no transmission pass for a view where glass covers little of the screen) would be measured with the same recipe.

## Decision: iOS

**The iOS player keeps transmission off.** The iPad's report ([transmission-mobile-ipad](transmission-mobile-ipad.md)) measured the same two scenes at its native 1640×2360:

- Cold, the added GPU time is at most 2 ms: **not met.** TransmissionTest adds 4.9 ms (17.9 off, 22.8 on) and CommercialRefrigerator 5.1 ms (16.6 off, 21.7 on), raw. The transmission passes and `forward transparent` alone are 4.0 to 5.2 ms. The per-pass timestamps are sparse on MoltenVK ([#152](https://github.com/Pacheco95/sonnet/issues/152)) and the off totals scatter (12.9 to 20.2 ms for TransmissionTest), so the unchanged-pass scaling used for the S25 would move the figures a lot; the report keeps it as a sensitivity calculation and not as a correction, and the figures here are the raw ones. The pass structure alone had added 3.4 ms on this iPad ([transmission-cost-ipad](transmission-cost-ipad.md)), and the shading comes on top of that, as ADR-0033 expected.
- Sustained, the frame stays under 33 ms: met, at most 25.2 ms in the totals that have timestamps, and the interval grows from 20.5 or 22.5 ms to 24 or 25.8 ms.
- Sustained, the thermal state reached is no worse than with transmission off: **not met in TransmissionTest.** Both on runs reached serious (after 402 and 488 s), and off reached it once (759 s) and stayed nominal once. CommercialRefrigerator stayed nominal in all four runs.

One failed budget is enough for the fallback, and a platform on the fallback is a valid completion of #342; here two fail. The iPad is the weakest of the two devices on this cost (the cold figure is over twice the S25's), so the case for turning it on is weaker than on Android. What a later change would measure is the same as on Android: a cheaper chain or a smaller copy, with the same recipe.

Limits: one iPad, one build, GPU clocks not sampled, and a quarter of the sustained rows lack GPU timestamps, so the sustained GPU delta of the first TransmissionTest pair and the second refrigerator pair is not known. The interval and the thermal rows do not depend on them.
