# Ambient occlusion and contact shadows on the S25

Measured 2026-11-16 for [#540](https://github.com/Pacheco95/sonnet/issues/540), the phone measurement that [ADR-0039](../decisions/0039-screen-space-ambient-occlusion-and-contact-shadows.md#mobile-budget-and-measurement) requires before the Android player permits screen-space ambient occlusion ([#501](https://github.com/Pacheco95/sonnet/issues/501)) or sun contact shadows ([#509](https://github.com/Pacheco95/sonnet/issues/509)). The RTX 4090 figures are those issues' own ([ambient-occlusion-rtx4090](ambient-occlusion-rtx4090.md), [contact-shadows](contact-shadows.md)).

**Result: both effects stay off on Android.** At the Standard counts each effect fails its cold cost gate on both scenes measured, by a wide margin. The Reduced counts could not be measured: the phone dropped off USB during the session (below), so Reduced remains unmeasured, which by itself keeps an effect off.

## Setup

- Galaxy S25 Ultra (SM-S938B), Adreno 830, Android 17, display mode 1080×2340 at 120 Hz. Vulkan 1.3.284 device on [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md)'s 1.4-extensions path, driver build `0603ba43d2`. Charging over USB, screen on.
- `android-release` (RelWithDebInfo) APK of `perf/s25-ao-contact-shadows`: `main` at `3020429` plus the `reduced` value of the two capture flags, the code of `2758794` (its `--help` wording was shortened after the build). FIFO, 4 swapchain images of 1080×2340, the player's 60 fps request. The phone player's renderer settings (TAA, occlusion culling, four cascades with the far map at 512, shadow LOD cascade 3, bloom); only `--ambient-occlusion` and `--contact-shadows` differ between the runs of a scene.
- Render extent `--render-size 1920x1080`, scaled to the portrait window by the present pass, so the screenshot and every pass are 1920×1080.
- Scenes, cooked with `sonnet_cook --platform android`:
  - **Course, still**: `apps/samples/character`, `scenes/capture_contact_shadows.scene.json`, the character standing at the start under a stationary camera. Its camera already asks for AO and its sun for contact shadows.
  - **Showcase**: `apps/samples/showcase/scenes/main.scene.json` with its camera asking for AO and its sun for contact shadows (radius 0.5 m, strength 1; length 0.2 m, thickness 0.02 m, ADR-0039's defaults), a copy written for the cook and not committed. No showcase-with-AO variant exists on `main` yet ([#559](https://github.com/Pacheco95/sonnet/issues/559)).
- Each run is a fresh process: `--play 5` (300 fixed steps after the assets have loaded and ten settle frames), so asset and pipeline warm-up and more than 32 TAA frames come before the window. The window is the capture's last 100 frames (1.67 s at 60 fps, 1.6 to 2.3 s at the showcase's rate), not ADR-0039's three seconds: the player's frame-times window is fixed at 100 frames.
- GPU figures are the capture line's `GPU frames median / p95` over complete frames (every pass's timestamp available; all 100 frames were complete in every run), and the interval figures its `intervals median / p95`.
- Before every run the HAL's skin reading was at or below 31.0 °C and the thermal status 0 (`dumpsys thermalservice`, polled every 20 s; the run started on the first reading in range). Every run's thermal line reads `nominal -> nominal`.
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

## Against ADR-0039's gates

| Gate | Standard | Reduced |
|---|---|---|
| AO added ≤ 1.5 ms, cold median and p95 | **Fails**: +2.0 / +2.1 showcase, +3.1 corrected on the course (passes 1.9 and 2.6 ms) | Not measured |
| Contact added ≤ 0.5 ms, cold median and p95 | **Fails**: +1.2 / +1.3 showcase, +1.8 corrected on the course (pass 1.1 and 1.5 ms) | Not measured |
| Combined ≤ 2 ms, cold | **Fails**: +2.8 / +2.9 showcase, +5.0 corrected on the course | Not measured |
| Sustained cost, p95 interval < 33 ms, thermal no worse | Not run: the cold gate already fails | Not measured |

ADR-0039 keeps an effect off when Reduced fails or remains unmeasured. **The Android player keeps `RendererSettings::ambientOcclusion` and `contactShadows` off**, the gate named is the cold added GPU time: Standard misses all three budgets (AO, contact shadows, combined) on both scenes, and Reduced remains unmeasured. iOS stays off, unmeasured.

## Not measured

The session ran the Standard cold rounds on the showcase and the still course (24 runs, all kept: each log was checked to be its own process, with the settings, size and `exit ok` its label says). About 70 minutes into it, after the last course run, the phone disappeared from USB (`adb devices` and `lsusb` empty) and had not returned more than an hour later, so the rest of the matrix was not run:

- **The deterministic traversal** (`scenes/route.scene.json` of the character sample with its sun asking for contact shadows): no run.
- **The Reduced counts** (2 slices of 3 steps a side; 8 contact steps), the ADR's next candidate when Standard fails: no run. The flags for it are in place (`--ambient-occlusion reduced`, `--contact-shadows reduced`, [player.md](../player.md#capture-runs)). An estimate is not a measurement, but it says what to expect: only the search and the contact march scale with the counts. Halving the search and the march leaves AO near 1.5 ms on the showcase and about 2.0 ms on the course (temporal, blurs and upsample alone are 0.6 and 1.4 ms there), and contact shadows near 0.6 and 0.8 ms, over their 0.5 ms.
- **The native landscape extent** (2340×1080): no run.
- **Sustained 600 s pairs**, with temperature and status during them: none. With every Standard candidate failing cold, a sustained pair cannot change the decision; for Reduced they are still needed if its cold windows pass.
- **Clocks during the runs**: only the idle clock before each run and the clock at its end were read (`devfreq/cur_freq`), not sampled through the window; the course's clock shift is inferred from the unchanged passes.
- **Transient and persistent image memory**: not read on the phone. The images are the same as on the desktop at 1080p ([ambient-occlusion-rtx4090](ambient-occlusion-rtx4090.md#results): 13.8 MiB transient and 7.9 MiB of history for AO; one 2.0 MiB `R8Unorm` mask for contact shadows).
- **Three-second windows**: the capture line covers the last 100 frames, 1.6 to 2.3 s here.
- iOS: no device measured; the iOS player keeps both off.

To finish: reconnect the phone, push the two bundles, and run the Reduced rounds of the showcase, the still course and the route at 1920×1080 with the same recipe; for any candidate that passes cold, its 600 s pairs and the native extent. Then the Android defaults change only if every gate passes.
