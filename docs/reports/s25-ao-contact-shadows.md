# Ambient occlusion and contact shadows on the S25

Measured 2026-11-16 (Standard, then Reduced after the phone was reconnected) for [#540](https://github.com/Pacheco95/sonnet/issues/540), the phone measurement that [ADR-0039](../decisions/0039-screen-space-ambient-occlusion-and-contact-shadows.md#mobile-budget-and-measurement) requires before the Android player permits screen-space ambient occlusion ([#501](https://github.com/Pacheco95/sonnet/issues/501)) or sun contact shadows ([#509](https://github.com/Pacheco95/sonnet/issues/509)). The RTX 4090 figures are those issues' own ([ambient-occlusion-rtx4090](ambient-occlusion-rtx4090.md), [contact-shadows](contact-shadows.md)).

**Result: both effects stay off on Android.** At the Standard counts each effect fails its cold cost gate on both scenes, by a wide margin. At the Reduced counts AO and the combination still fail cold. Reduced contact shadows pass the cold gate only as raw totals, which the Adreno's rising clock hides. They cannot pass the sustained frame-interval gate on the showcase, because the showcase misses 33 ms sustained with both effects off. Their sustained pair confirms both failures: once the phone throttles they add +1.5 ms (median) and +1.9 ms (p95).

## Setup

- Galaxy S25 Ultra (SM-S938B), Adreno 830, Android 17, display mode 1080×2340 at 120 Hz. Vulkan 1.3.284 device on [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s 1.4-extensions path, driver build `0603ba43d2`. Charging over USB, screen on.
- `android-release` (RelWithDebInfo) APK of `perf/s25-ao-contact-shadows`: `main` at `3020429` plus the `reduced` value of the two capture flags, the code of `2758794` (its `--help` wording was shortened after the build). FIFO, 4 swapchain images of 1080×2340, the player's 60 fps request. The phone player's renderer settings (TAA, occlusion culling, four cascades with the far map at 512, shadow LOD cascade 3, bloom); only `--ambient-occlusion` and `--contact-shadows` differ between the runs of a scene.
- Render extent `--render-size 1920x1080`, scaled to the portrait window by the present pass, so the screenshot and every pass are 1920×1080. The phone stayed in portrait, so the image on its screen is stretched; only `present` (about 0.1 ms) scales into the window, the same in every run.
- Scenes, cooked with `sonnet_cook --platform android`:
  - **Course, still**: `apps/samples/character`, `scenes/capture_contact_shadows.scene.json`, the character standing at the start under a stationary camera. Its camera already asks for AO and its sun for contact shadows.
  - **Showcase**: `apps/samples/showcase/scenes/main.scene.json` with its camera asking for AO and its sun for contact shadows (radius 0.5 m, strength 1; length 0.2 m, thickness 0.02 m, ADR-0039's defaults), a copy written for the cook and not committed. No showcase-with-AO variant exists on `main` yet ([#559](https://github.com/Pacheco95/sonnet/issues/559)).
- Each run is a fresh process: `--play 5` (300 fixed steps after the assets have loaded and ten settle frames), so asset and pipeline warm-up and more than 32 TAA frames come before the window. The window is the capture's last 100 frames (1.67 s at 60 fps, 1.6 to 2.3 s at the showcase's rate), not ADR-0039's three seconds: the player's frame-times window is fixed at 100 frames.
- GPU figures are the capture line's `GPU frames median / p95` over complete frames (every pass's timestamp available; all 100 frames were complete in every run), and the interval figures its `intervals median / p95`.
- Before every run the HAL's skin reading was at or below 31.0 °C and the thermal status 0 (`dumpsys thermalservice`, polled every 20 s; the run started on the first reading in range). Every cold run's thermal line reads `nominal -> nominal`.
- Cold order: rounds of four runs, both off, AO alone, contact shadows alone, both on, the order reversed every other round (off first in rounds 1 and 3, last in round 2). Each effect run is paired with its round's off run.
- Raw logs, scripts and the scene edit are outside the repository.

## Cold, Standard counts, 1920×1080

Milliseconds. "Added" is the on run minus its round's off run, median and p95 of GPU frames. "Passes" is the sum of the effect's own pass means (`ao search`, `ao temporal`, `ao blur x`, `ao blur y`, `ao upsample`; `contact shadows`).

**Showcase** (GPU-bound below the 60 fps cap, so the clock stays where the load puts it):

| Run | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Off, median / p95 | 19.845 / 19.951 | 19.890 / 20.014 | 19.885 / 19.999 |
| AO, median / p95 | 21.904 / 22.020 | 21.500 / 22.062 | 21.918 / 22.504 |
| Contact, median / p95 | 20.649 / 20.850 | 21.087 / 21.273 | 21.087 / 21.370 |
| Both, median / p95 | 22.662 / 22.814 | 22.650 / 22.746 | 22.985 / 23.265 |

| Added | Round 1 | Round 2 | Round 3 | Passes | Budget |
|---|---|---|---|---|---|
| AO | +2.06 / +2.07 | +1.61 / +2.05 | +2.03 / +2.51 | 1.94 to 1.97 | 1.5 |
| Contact | +0.80 / +0.90 | +1.20 / +1.26 | +1.20 / +1.37 | 1.14 to 1.15 | 0.5 |
| Both | +2.82 / +2.86 | +2.76 / +2.73 | +3.10 / +3.27 | 3.08 to 3.11 | 2.0 |

**Course, still** (the off run is under the 60 fps cap, 14.2 ms of GPU work in 16.64 ms):

| Run | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Off, median / p95 | 14.181 / 14.376 | 14.173 / 14.232 | 14.206 / 14.279 |
| AO, median / p95 | 15.475 / 15.554 | 15.472 / 15.547 | 15.452 / 15.539 |
| Contact, median / p95 | 14.304 / 14.337 | 14.307 / 14.393 | 14.303 / 14.374 |
| Both, median / p95 | 17.110 / 17.198 | 17.119 / 17.242 | 17.144 / 17.219 |

| Added | Round 1 | Round 2 | Round 3 | Passes | Corrected | Budget |
|---|---|---|---|---|---|---|
| AO | +1.29 / +1.18 | +1.30 / +1.32 | +1.25 / +1.26 | 2.62 to 2.66 | +3.09 | 1.5 |
| Contact | +0.12 / −0.04 | +0.13 / +0.16 | +0.10 / +0.10 | 1.52 | +1.81 | 0.5 |
| Both | +2.93 / +2.82 | +2.95 / +3.01 | +2.94 / +2.94 | 4.14 to 4.18 | +5.00 | 2.0 |

On the course the raw added time understates the cost. The Adreno's clock follows its load, and with an effect on the frame does more work in the same 16.7 ms, so the clock rises: every pass the effects do not touch (the cascades, depth, TAA, bloom, tonemap, present) takes 0.74 to 0.83 ms less with an effect on than in the off run, and `forward` 0.38 to 0.64 ms less. The contact-shadow pass alone is 1.52 ms while the frame grows by 0.1 ms. "Corrected" scales each on run's total by the ratio of those unchanged passes, off over on, as the [transmission report](transmission-mobile.md) did, and gives the median over the three rounds. The showcase, which runs below the cap, shows no such shift (the unchanged passes move by −0.35 to +0.02 ms), and there the raw added time is close to the passes' own.

Where the time goes: `ao search` is 0.85 ms on the showcase and 1.25 ms on the course, `ao upsample` 0.60 and 0.78 ms, `ao temporal` 0.28 and 0.32 ms, the two blurs 0.11 to 0.13 ms each. On the RTX 4090 the five passes are 0.11 ms; on the phone they are 17 to 23 times that.

## Cold, Reduced counts, 1920×1080

The same recipe with `--ambient-occlusion reduced` and `--contact-shadows reduced`: 2 slices of 3 steps a side, and 8 contact steps. These are 24 more runs, all kept after the same checks.

| Showcase | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Off, median / p95 | 19.917 / 20.043 | 19.886 / 20.837 | 19.944 / 20.041 |
| AO, median / p95 | 21.163 / 21.267 | 21.594 / 22.594 | 21.538 / 21.741 |
| Contact, median / p95 | 20.249 / 20.395 | 20.253 / 20.974 | 20.241 / 20.327 |
| Both, median / p95 | 22.277 / 22.476 | 22.050 / 22.582 | 22.278 / 23.094 |

| Course, still | Round 1 | Round 2 | Round 3 |
|---|---|---|---|
| Off, median / p95 | 14.178 / 14.273 | 14.187 / 14.780 | 14.179 / 14.239 |
| AO, median / p95 | 14.929 / 14.989 | 14.958 / 15.342 | 14.936 / 15.021 |
| Contact, median / p95 | 13.775 / 13.814 | 13.764 / 13.798 | 13.774 / 13.823 |
| Both, median / p95 | 16.092 / 16.181 | 16.079 / 16.474 | 16.062 / 16.128 |

| Added, median over the rounds (worst round) | Showcase raw | Showcase passes | Course raw | Course passes | Course corrected | Budget |
|---|---|---|---|---|---|---|
| AO | +1.59 / +1.70 (+1.71 / +1.76) | 1.63 to 1.67 | +0.76 / +0.72 | 2.11 to 2.14 | +2.47 | 1.5 |
| Contact | +0.33 / +0.29 (+0.37 / +0.35) | 0.74 | −0.41 / −0.46 | 0.98 | +1.22 | 0.5 |
| Both | +2.33 / +2.43 (+2.36 / +3.05) | 2.37 to 2.41 | +1.89 / +1.89 | 3.09 to 3.12 | +3.78 | 2.0 |

The counts cut only the search and the march. `ao search` falls to 0.53 ms on the showcase and 0.75 ms on the course, from 0.85 and 1.25. The contact pass falls to 0.74 and 0.98 ms, from 1.14 and 1.52. `ao temporal`, the blurs and `ao upsample` stay at 1.1 and 1.4 ms, which alone is most of AO's 1.5 ms.

The rising clock now shows on the showcase too. With reduced contact shadows, the unchanged passes are 0.32 to 0.38 ms faster than off, so the frame grows by 0.33 ms while the pass takes 0.74 ms. On the course the frame even shrinks by 0.41 ms. Read as raw totals, reduced contact shadows meet the 0.5 ms cold gate on both scenes. At a fixed clock they would not: the pass alone is 0.74 and 0.98 ms, and the corrected figures are +1.1 and +1.2 ms.

## Sustained, Reduced contact shadows, showcase

Reduced contact shadows were the only candidate whose raw totals met a cold gate, so they got the paired sustained runs. The scene is the showcase at 1920×1080 with `--play 600`: 36 000 fixed steps, which at its frame rate is more than 600 s of wall-clock. The off run went first; both runs started from skin 31.0 °C and status 0. `thermal.sh` sampled the HAL skin and AP temperatures, Android's thermal status and the GPU clock every 30 s. The frame figures are the last 100 frames.

| Run | Wall-clock | Status 1 at | Status 2 at | Peak status, skin | Clock in the last minutes | GPU frames median / p95 | Interval median / p95 | Contact pass |
|---|---|---|---|---|---|---|---|---|
| Off (first) | about 1100 s | 91 s | 271 s | 2, 42.3 °C | 734 to 832 MHz | 35.636 / 35.997 | 35.67 / 36.05 | |
| Contact, reduced (second) | about 1160 s | 91 s | 301 s | 2, 42.3 °C | 734 MHz | 37.130 / 37.876 | 37.13 / 37.85 | 1.49 ms |

Sustained, reduced contact shadows add **+1.49 ms** to the median and **+1.88 ms** to the p95 GPU frame, three times their 0.5 ms. Throttled, the clock no longer has room to absorb the pass, which takes 1.49 ms here against 0.74 cold.

The thermal course is the same in both runs: status 1 at 91 s, status 2 at 271 and 301 s, a peak of 2 at 42 °C skin. So the thermal gate alone is met.

Neither run meets 33 ms. The showcase at 1080p is already GPU-bound at 19.9 ms cold, and settles at 35.7 ms per frame once the phone has throttled, with both effects off. The ADR asks for a single pair; there is one order here, off first. Both runs start from skin 31.0 °C and status 0, about 50 minutes apart.

## Against ADR-0039's gates

| Gate | Standard | Reduced |
|---|---|---|
| AO added ≤ 1.5 ms, cold median and p95 | **Fails**: +2.0 / +2.1 showcase, +3.1 corrected on the course (passes 1.9 and 2.6 ms) | **Fails**: +1.59 / +1.70 showcase (passes 1.64 ms), +2.5 corrected on the course (passes 2.1 ms) |
| Contact added ≤ 0.5 ms, cold median and p95 | **Fails**: +1.2 / +1.3 showcase, +1.8 corrected on the course (pass 1.1 and 1.5 ms) | Raw totals **meet** it (+0.33 / +0.29 showcase, −0.41 / −0.46 course), but only through the clock: the pass is 0.74 and 0.98 ms, +1.1 and +1.2 corrected |
| Combined ≤ 2 ms, cold | **Fails**: +2.8 / +2.9 showcase, +5.0 corrected on the course | **Fails**: +2.33 / +2.43 showcase, +3.8 corrected on the course |
| Sustained p95 interval < 33 ms | Not run: the cold gate already fails | **Fails** on the showcase, where off alone settles at 36.05 ms (below); reduced contact shadows reach 37.85 ms. Their added time, sustained, is +1.49 / +1.88 ms: **fails** the 0.5 ms |
| Thermal no worse than off | Not run | Met for reduced contact shadows: peak status 2 and status 1 at 91 s in both runs, status 2 at 301 s against 271 s off. Not run for AO or the combination, which fail cold |

ADR-0039 keeps an effect off when Reduced fails. **The Android player keeps `RendererSettings::ambientOcclusion` and `contactShadows` off.**

- **AO:** fails the cold added GPU time at both counts.
- **Contact shadows:** fail it at Standard. At Reduced they fail the sustained 33 ms frame-interval gate on the showcase, which the showcase misses even with both effects off. Their raw cold pass at Reduced is an artefact of the clock rising, not a cost under 0.5 ms.

iOS stays off, unmeasured.

The showcase failing 33 ms sustained at 1080p with both effects off is a finding of its own. Throttled to 734 to 900 MHz at status 2, its frame takes 35.6 ms. That is the existing scene and renderer, not these effects.

## Not measured

The first session ran the Standard cold rounds (24 runs). Then the phone dropped off USB for about three hours. After it was reconnected, the Reduced cold rounds (24 runs) and the one sustained pair ran with the same APK, bundles and recipe. Every log was checked to be its own process, with the settings, size and `exit ok` its label names.

- **The deterministic traversal** (`scenes/route.scene.json` of the character sample with its sun asking for contact shadows): not run. No candidate passed, and ADR-0039 asks for the traversal to confirm a passing one.
- **The native landscape extent** (2340×1080): not run, for the same reason. It has 1.22 times the pixels of 1080p, so it can only cost more.
- **Sustained pairs** of AO, of the combination and of the Standard counts: not run. Each already fails cold, and a sustained window cannot rescue that. The one sustained pair (reduced contact shadows) has a single order.
- **Clocks during the cold windows**: the sustained runs sampled the clock every 30 s, but the cold runs read it only before and after. The clock shift in the cold runs is inferred from the passes the effects do not change.
- **Transient and persistent image memory**: not read on the phone. The images are the desktop's at 1080p ([ambient-occlusion-rtx4090](ambient-occlusion-rtx4090.md#results): 13.8 MiB transient and 7.9 MiB of history for AO; one 2.0 MiB `R8Unorm` mask for contact shadows).
- **Three-second windows**: the capture line covers the last 100 frames, 1.6 to 2.3 s cold.
- **iOS**: no device was measured, and the iOS player keeps both off.

What would change the answer is a cheaper pass, not another run of these. AO's fixed passes (temporal, two blurs and upsample) take 1.1 to 1.4 ms at 1080p on this GPU before any search. The contact march takes 0.74 ms or more at 8 steps. A lighter scene, or a mobile frame budget with headroom under 33 ms sustained, would also be needed: the showcase has none.
