# Transmission cost spike on the S25

What the passes [ADR-0033](../decisions/0033-transmission.md) ([#254](https://github.com/Pacheco95/sonnet/issues/254)) adds cost on the Galaxy S25 Ultra (Adreno 830): the opaque-scene copy, its mip chain and the split of the forward pass. The spike is on the branch `spike/transmission-cost` (commit `3c31842`), behind the player flag `--transmission-spike`. It is not the ADR's acceptance measurement, which runs TransmissionTest and CommercialRefrigerator with the transmission shading; this one sets the pass structure's cost and the baseline that measurement adds to.

## What the spike does

With the flag, the forward pass draws opaque geometry and the sky only. A compute pass copies the HDR target into a mipmapped image, eleven compute passes build its chain with the bloom chain's 13 taps, and a `forward transparent` pass loads the target and depth and draws the blended draws and particles, declaring the copy as sampled. Nothing reads the copy, so the image is unchanged; on the RTX 4090 (1280×720, Debug, validation on) the captures with and without the flag match and validation is clean.

It leaves out the transmission shading (one material texture and one copy sample per glass pixel) and the material changes. The stress scene has no blended draws (`forward transparent` measures 0.003 ms), so the second pass here costs only its load of the target and depth.

## Setup

- Release APK from `3c31842`, 1080×2340 swapchain, FIFO, the phone player's default renderer settings.
- `scenes/stress.scene.json` from `apps/samples/basic`, launched with `am start -S -W ... --es args '--scene scenes/stress.scene.json --play N --screenshot ...'`, with `--transmission-spike` for the runs marked on.
- Charging over USB for every run. Thermal status is Android's (0 none, 1 light, 2 moderate), the skin temperature `dumpsys thermalservice` reports and the GPU clock `/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq`, sampled every 30 s.
- Times are the player's `capture frame times` line, the last 100 frames.

## Correcting for the clock

The Adreno clock moves between 734 and 1200 MHz and cannot be pinned without root, and added work raises it: raw cold totals made the spike look faster than no spike in some pairs. Each run is scaled by the passes the spike does not change (everything before `forward`, and `taa`, the bloom chain, `tonemap` and `present`), relative to the first run without the spike in its table. The corrected column is that scaled total.

## Cold (`--play 3`)

Alternating runs, thermal status 0 throughout.

| Run | Spike | GPU total | Corrected | Spike passes |
|---|---|---|---|---|
| 1 | off | 13.746 ms | 13.746 ms | – |
| 1 | on | 14.285 ms | 14.737 ms | 0.887 ms |
| 2 | off | 14.469 ms | 13.900 ms | – |
| 2 | on | 14.299 ms | 14.798 ms | 0.928 ms |
| 3 | off | 14.200 ms | 13.872 ms | – |
| 3 | on | 14.401 ms | 14.854 ms | 0.894 ms |

Means: 13.84 ms off, 14.80 ms on, **+0.96 ms**, of which the spike's passes are 0.90 ms. An earlier set from the same build, whose logs were lost, measured +0.94 ms. The corrected `forward` is the same with and without the spike (8.20–8.42 ms).

The per-pass timestamps attribute 0.003 ms to the copy and 0.39 and 0.33 ms to the first two mip levels. A full-resolution copy costs more than a quarter-resolution downsample, so the copy's time likely lands in the next pass's timestamps; the sum is what this report uses.

## Sustained (`--play 600`)

Every run starts at a skin reading of 37.9 °C and thermal status 0, after a fixed 15-minute cooldown. The skin reading moves in steps of about 2 °C, and a first pair that cooled only until the reading returned started its second run warmer than it read; it is left out. The two pairs below run in opposite orders.

| Order | Spike | GPU total | Corrected | Spike passes | Status 2 reached | Final status, clock |
|---|---|---|---|---|---|---|
| off first | off | 19.795 ms | 19.795 ms | – | ~5.5 min | 2, 734 MHz |
| off first | on | 20.988 ms | 21.216 ms | 1.212 ms | ~2.5 min | 2, 734 MHz |
| on first | on | 21.059 ms | 20.981 ms | 1.191 ms | ~2.5 min | 2, 734 MHz |
| on first | off | 19.768 ms | 19.740 ms | – | within 3.6 min | 2, 734 MHz |

Corrected to the first off run. The last run lost its clock samples between 0.5 and 3.6 min when the host restarted; the phone kept running and its log was collected afterwards, so its frame time is complete but the time it reached status 2 is only bounded.

- The spike adds **+1.2 to +1.4 ms** at the settled 734 MHz clock, whichever run goes first. Frames stay far under the 33 ms target: 21.0–21.2 ms against 19.7–19.8 ms.
- Every run, with and without the spike, ends at thermal status 2 at 734 MHz.
- With the spike the phone reaches status 2 in about 2.5 minutes in both orders. Without it, 5.5 minutes when it runs first and within 3.6 minutes when it runs second. Order explains part of the difference; the spike reaches status 2 sooner in both orders. Two pairs do not separate the two causes further.

## Against ADR-0033's phone thresholds

- Cold, the added GPU frame time is at most 2 ms: **+0.96 ms**, met for the pass structure. The transmission shading still has to fit in the remaining 1 ms.
- Sustained, the frame stays under 33 ms: **21.2 ms at most**, met.
- Sustained, the thermal status reached is no worse than with transmission off: **2 in both**, met as written. The rule compares the status reached, not how fast; the spike reaching it sooner is a cost the rule does not capture.

The iPad is measured in [transmission-cost-ipad](transmission-cost-ipad.md): +3.4 ms cold, over the 2 ms limit.
