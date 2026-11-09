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

**The RTX 4090 meets the budget with about a twelfth of it: +0.12 ms of 1.5 ms**, median and p95, cold and sustained, at the standard counts; the reduced mode is not needed on this GPU, and `RendererSettings` keeps the desktop default on. Thermal: the GPU stayed at 2745 to 2760 MHz and at most 66 °C; no throttling was seen.

## Not measured

- The showcase stress scene: its camera does not ask for ambient occlusion, and `--ambient-occlusion on` only permits a request, so it would need a scene edited for the run. Every pass is full-screen, so its cost follows the pixels rather than the draws.
- A sustained player run: the sustained pair is the offscreen benchmark's.
- The phones: [#540](https://github.com/Pacheco95/sonnet/issues/540) for the Galaxy S25 Ultra, and an iOS device of its own. Both players keep the effect off until then.
- Frame intervals: the benchmark presents nothing, so it has none.
