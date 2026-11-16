# Ambient occlusion on the RTX 4090

The desktop cost of the screen-space ambient occlusion of [#501](https://github.com/Pacheco95/sonnet/issues/501) against [ADR-0039](../decisions/0039-screen-space-ambient-occlusion-and-contact-shadows.md)'s budget: at most 1.5 ms of added total GPU frame time, median and p95, cold and sustained. The phone measurement that may enable it on Android is [#540](https://github.com/Pacheco95/sonnet/issues/540); nothing here applies to a phone or to iOS.

## Setup

- Commit 8f691c1 (branch `feat/ambient-occlusion`), `linux-release` (Clang 22, `-O2`), validation off.
- NVIDIA GeForce RTX 4090, driver 595.71.05, Vulkan 1.4.329, loader 1.4.357; Linux. Clocks before the cold runs 210 MHz (idle), 2745 to 2760 MHz graphics and 10501 MHz memory at the end of every run, 52 to 66 °C, power state P0.
- `renderer_tests "the added GPU frame time of ambient occlusion at 1080p"` (hidden, `[.][benchmark][ao][gpu]`), each run under the shared GPU timing lock. A 1920×1080 offscreen target, `RendererSettings{}` (TAA, shadows with four 2048 cascades, occlusion culling, bloom; ambient occlusion permitted at three slices and four steps a side), a sun, a flat-colour environment, an 80 m ground and 400 boxes 0.3 to 2.3 m tall in a 20×20 grid, from a camera 3 m up looking over them. The camera's request is the only difference between on and off.
- Every sample is one complete GPU frame: the sum of all of that frame's pass timestamps, kept only when every pass's timestamp is available (all were). 120 warm-up frames before each window, after a cut, so the pipelines, the transient pool and both histories settle (more than ADR-0039's 32). Cold: three pairs of three-second windows, alternating which goes first. Sustained: one pair of 600 s of wall-clock time each, off first.
- No presentation: the desktop session was locked during the benchmark, and a locked GNOME session paced the player's window to one frame a second (a trial `sonnet_player` run logged frames 1009 ms apart at a 210 MHz GPU clock), which makes its `capture frame times` useless for a cost. The session was unlocked again for the player pairs below, which ran at 2.78 ms a frame.
- Player pairs: the Release `sonnet_player` of the same commit on the character course, `--render-size 1920x1080 --ambient-occlusion on|off`, every scene camera asking for it, three pairs alternating the order on `capture_idle` (`--play 3`, a still camera) and on `route` (`--play 20`, the camera following the character's traversal). Their GPU figure is the player's `capture frame times` line: the sum of each pass's mean over the last 100 frames, all samples available, so a mean and not ADR-0039's median and p95 of complete frames.

Logs: `~/sonnet-measurements/501-ambient-occlusion/benchmark-1080p-{cold,sustained,final}.log` (outside the repository).

## Results

Final commit, cold (milliseconds of total GPU frame time):

| Pair | Order | Off median / p95 | On median / p95 | Added median / p95 |
|---|---|---|---|---|
| 1 | on, off | 0.328 / 0.333 | 0.446 / 0.475 | +0.118 / +0.143 |
| 2 | off, on | 0.328 / 0.333 | 0.446 / 0.451 | +0.118 / +0.119 |
| 3 | on, off | 0.328 / 0.333 | 0.446 / 0.451 | +0.118 / +0.119 |

Sustained, 600 s each, off first (one commit earlier, 4c18021 plus the uncommitted change: the same passes and shaders, with the occlusion mask's index in the transmission's spare frame-constant word instead of a word of its own): off 1,598,898 frames, median 0.330 / p95 0.334 ms; on 1,229,365 frames, median 0.448 / p95 0.453 ms; **added +0.119 ms median and p95**. The cold pairs run before it on that build gave +0.117 to +0.120 ms median and +0.119 to +0.146 ms p95.

Player, character course (means of the last 100 frames, milliseconds of GPU time):

| Pair | Order | Still camera off / on | Added | Route off / on | Added |
|---|---|---|---|---|---|
| 1 | on, off | 0.363 / 0.475 | +0.112 | 0.522 / 0.571 | +0.049 |
| 2 | off, on | 0.365 / 0.465 | +0.100 | 0.514 / 0.569 | +0.055 |
| 3 | on, off | 0.332 / 0.433 | +0.101 | 0.477 / 0.623 | +0.146 |

On the course the five passes took 0.11 to 0.14 ms (search 0.063 to 0.076, temporal 0.011 to 0.015, the two blurs 0.006 to 0.008 each, upsample 0.025 to 0.033 ms); the route's spread is its moving view, whose last 100 frames are not the same pixels in each run.

The ambient occlusion passes alone (`ao search`, `ao temporal`, `ao blur x`, `ao blur y`, `ao upsample`) take 0.114 ms median and 0.116 ms p95 of the frame, so nearly all of the added time is theirs; the forward pass's extra sample is lost in the noise. The search is about half of them and the upsample a quarter.

Memory: the transient pool grows from 98.3 to 112.1 MiB (+13.8 MiB: the half-resolution search and two blur images at 4.0 MiB each, the 2.0 MiB mask, rounded by the pool), and the history adds two persistent 960×540 `R16G16B16A16Sfloat` images, 7.9 MiB.

## The stress scene

[#559](https://github.com/Pacheco95/sonnet/issues/559) timed the showcase stress scene of ADR-0039's measurement plan: the basic sample's `scenes/stress.scene.json`, the README's performance target, whose `scripts/stress.lua` builds ten thousand boxes and icospheres and a hundred point lights when it plays. Its camera does not ask for ambient occlusion, so the run uses `scenes/stress_ao.scene.json`, written beside it by `tools/generate_stress_scene.py`: the same entities and identities with the camera's `ambientOcclusion` on. The player's existing `--ambient-occlusion on|off` then makes the pair, with the scene unchanged between its halves.

- Commit 650ab42 (branch `perf/ao-showcase-timing`, the scene variant on top of 3020429, so #560's normalisation and edge continuation and #573's history are in), `linux-release` (Clang 22, `-O2`), validation off. NVIDIA GeForce RTX 4090, driver 595.99.02, Vulkan 1.4.329; Linux, power profile `performance`.
- The Release `sonnet_player` with `--headless on --render-size 1920x1080 --scene scenes/stress_ao.scene.json --ambient-occlusion on|off`, the path of [#555](https://github.com/Pacheco95/sonnet/issues/555) ([player.md](../player.md#headless-runs)): no window, an offscreen swapchain, so no display paces the frames and the screen's lock does not matter. Desktop `RendererSettings`: TAA, four 2048 shadow cascades, occlusion culling, bloom, ambient occlusion at three slices and four steps a side. Every run under the shared GPU timing lock, each a fresh process, with no compiler running at its start (`pgrep` for `cc1plus` and clang's `-cc1`, none at any sample of the sustained runs either) and the load average under 3.
- The figures are the player's `capture frame times` line: the median and p95 of the complete GPU frames of the last 100 frames (all 100 complete in every run, every pass 100/100 samples), and the per-pass means.
- Cold: three pairs alternating the order, each run `--play 30` (1,800 fixed steps, about 4 s of wall-clock time at 1.8 to 2.0 ms a frame), so the window is the last 100 of 1,800 frames, long after the 32 TAA settling frames. The GPU was at 2760 MHz and P0 at the end of each run, 59 to 60 °C.
- Sustained: one pair of `--play 5400` (324,000 fixed steps), off first: 598 s and 602 s of wall-clock time, nearly two hours after the cold pairs and with another agent's runs in between, so each started from its own state (off at 2760 MHz and 58 °C, on from idle at 210 MHz and 51 °C). The GPU clock, temperature and power were sampled every 30 s: 2745 MHz and P0 at every sample of both, at most 72 °C (off) and 74 °C (on), 305 to 327 W.
- A control: the plain `stress.scene.json` with `--ambient-occlusion on`, which its camera does not ask for, drew no ambient-occlusion pass and gave 0.805 / 0.829 ms, the variant's off runs.

Milliseconds of total GPU frame time:

| Run | Order | Off median / p95 | On median / p95 | Added median / p95 | AO passes |
|---|---|---|---|---|---|
| Cold 1 | on, off | 0.804 / 0.829 | 0.902 / 0.926 | +0.098 / +0.097 | 0.095 |
| Cold 2 | off, on | 0.806 / 0.828 | 0.904 / 0.924 | +0.098 / +0.096 | 0.095 |
| Cold 3 | on, off | 0.807 / 0.827 | 0.903 / 0.926 | +0.096 / +0.099 | 0.095 |
| Sustained, 600 s | off, on | 0.806 / 0.831 | 0.904 / 0.925 | +0.098 / +0.094 | 0.095 |

The five passes are the same in every run with it on: search 0.041, temporal 0.013, the blurs 0.007 and 0.006, upsample 0.028 ms. The forward pass takes 0.267 to 0.269 ms with or without the mask, so the added time is the passes'. That is less than the benchmark's 0.114 ms, a different view at the same pixel count (the stress camera sees rows of small shapes running into the distance); why the search is cheaper there was not looked into. Ten thousand draws did not make it dearer: the passes read the depth, not the draws. The frame intervals are the CPU's (1.82 to 2.03 ms median, at most 2.10 ms p95, a frame's CPU time is 1.8 to 2.0 ms), headless and so not a display's; they are far under ADR-0039's 33 ms. The headless player reports no thermal state on Linux (`thermal n/a`), so the clocks above stand for it: no throttling in either sustained run. Memory is the benchmark's, the same 1080p images.

Logs, scripts and screenshots: `~/sonnet-measurements/559-ao-showcase/` (outside the repository).

**The RTX 4090 meets the budget with about a twelfth of it: +0.12 ms of 1.5 ms**, median and p95, cold and sustained, at the standard counts, and +0.10 ms on the stress scene in the player, cold and over 600 s; the reduced mode is not needed on this GPU, and `RendererSettings` keeps the desktop default on. Thermal: the GPU stayed at 2745 to 2760 MHz, at most 66 °C in #501's runs and 74 °C in the stress scene's; no throttling was seen.

## Not measured

- A windowed player run of the stress scene: [#559](https://github.com/Pacheco95/sonnet/issues/559)'s are headless, so they say nothing of presentation or of the display's intervals.
- A sustained player run on the character course: the player's sustained pair is the stress scene's.
- The phones: the Galaxy S25 Ultra is measured in [s25-ao-contact-shadows](s25-ao-contact-shadows.md) ([#540](https://github.com/Pacheco95/sonnet/issues/540)), where it misses the budget and the Android player keeps it off; an iOS device needs a run of its own, and the iOS player keeps it off until then.
- Frame intervals: the benchmark presents nothing, so it has none.
