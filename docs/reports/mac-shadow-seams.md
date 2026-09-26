# macOS shadow seam check

The fix revision is `93e08bd` (`fix/shadow-seams`); the comparison is `6cb36e5` (`main`). The fix was checked on a Mac through MoltenVK with validation enabled in Debug. Device names, device IDs and home paths are omitted here. Screenshot PNGs carry no metadata.

## Tests

The full Debug CTest run passed all 12 suites with the installed Vulkan loader selected explicitly. An earlier run without that loader also reported 12 passing suites, but its GPU cases skipped, so it is not the GPU result used here. The standalone `an unoccluded plane stays lit across every shadow cascade` case passed all 12 assertions at 256 and 1024 shadow-map sizes. The runtime Slang compiler tests passed 12 assertions in two cases, compiling `forward.slang`, `post.slang` and `cluster.slang`. The editor's GPU shader reload case passed four assertions and recompiled shaders from the checkout. These two runtime compilation paths ran on this Mac; neither was skipped.

## Captures and shape comparison

Each capture is the editor's 968 × 662 viewport PNG from the same static scene and default camera, with no play time. The two revisions use the same scene files. Interpret the shapes and boundaries, rather than absolute colour, because macOS colour encoding differs from captures on other platforms.

| Scene | `main` | `fix/shadow-seams` |
|---|---|---|
| Basic (`main.scene.json`), final | [PNG](mac-shadow-seams/main/main-final.png) | [PNG](mac-shadow-seams/fix/main-final.png) |
| Basic, shadow factor | [PNG](mac-shadow-seams/main/main-shadow-factor.png) | [PNG](mac-shadow-seams/fix/main-shadow-factor.png) |
| Basic, cascade | Unavailable on `main` | [PNG](mac-shadow-seams/fix/main-cascade.png) |
| Playground, final | [PNG](mac-shadow-seams/main/playground-final.png) | [PNG](mac-shadow-seams/fix/playground-final.png) |
| Playground, shadow factor | [PNG](mac-shadow-seams/main/playground-shadow-factor.png) | [PNG](mac-shadow-seams/fix/playground-shadow-factor.png) |
| Playground, cascade | Unavailable on `main` | [PNG](mac-shadow-seams/fix/playground-cascade.png) |

The fix's cascade diagnostic marks the basic ground's blue/green boundary around the objects and the green/red boundary across the foreground. The playground has a blue/green boundary across the far ground and a green/red boundary across the near ground. Neither fix shadow-factor image shows a ground-wide line at those boundaries. The visible shadow edges are continuous, including the basic scene's box shadows and the playground's long wall shadow; no dashed shadow edge is visible. The final views show no corresponding brightness stripe. The `main` shadow-factor views also show no clear cascade seam on this GPU, so this comparison establishes that the fix does not introduce one here. The shadow shapes differ around casters: on `main`, some shadows are narrower or separated from their caster, especially the playground's long wall shadow; the fix brings them closer. `main` rejects `--shading-term cascade` because that diagnostic was introduced by the fix, so no baseline cascade PNG is presented as if it came from `main`.

## Release benchmark

The benchmark is `renderer_tests '[benchmark]'`: 10,000 draws, 100 lights, 1,180,000 triangles at 1920 × 1080, 12 indirect calls over six scene passes, 30 rendered frames per invocation. Both revisions were built with Apple Clang using `CMAKE_BUILD_TYPE=Release`, `-O3 -DNDEBUG`. Source changes were not made for the benchmark. The same two `-Wno-error=unused-parameter` and `-Wno-error=unused-variable` overrides were needed on both sides because existing assertion-only variables become unused under `NDEBUG`. The runs alternated fix, main three times. GPU figures below are the benchmark's last completed frame's per-pass timestamps, and the total is their sum. CPU submission is the median of the last 25 frames, with its worst value also shown.

| Run | Revision | Forward GPU (ms) | Total GPU (ms) | CPU recording (ms) | CPU submit median (ms) | CPU submit worst (ms) |
|---|---|---:|---:|---:|---:|---:|
| 1 | Fix | 1.168 | 2.129 | 0.115 | 0.247 | 0.428 |
| 1 | Main | 1.134 | 2.093 | 0.134 | 0.246 | 0.418 |
| 2 | Fix | 1.155 | 2.128 | 0.122 | 0.222 | 0.377 |
| 2 | Main | 1.126 | 2.088 | 0.172 | 0.235 | 0.402 |
| 3 | Fix | 1.157 | 2.132 | 0.150 | 0.238 | 0.323 |
| 3 | Main | 1.135 | 2.088 | 0.154 | 0.224 | 0.372 |

The three-run mean forward pass is 1.132 ms on main and 1.160 ms on the fix, a 0.028 ms (+2.5%) increase. Mean total GPU time is 2.090 ms on main and 2.130 ms on the fix, a 0.040 ms (+1.9%) increase. This is a small, repeatable forward-pass cost on this Mac, consistent with sampling two cascades in overlap regions. The benchmark provides one GPU frame per invocation, so the three runs characterize observed variation rather than a full frame-time distribution.

Raw GPU pass timestamps, in milliseconds, in execution order:

| Pass | Main 1 | Fix 1 | Main 2 | Fix 2 | Main 3 | Fix 3 |
|---|---:|---:|---:|---:|---:|---:|
| Cull | 0.040 | 0.042 | 0.042 | 0.045 | 0.043 | 0.043 |
| Shadow cascade 0 | 0.015 | 0.015 | 0.015 | 0.015 | 0.015 | 0.015 |
| Shadow cascade 1 | 0.064 | 0.065 | 0.066 | 0.067 | 0.067 | 0.067 |
| Shadow cascade 2 | 0.134 | 0.137 | 0.136 | 0.137 | 0.134 | 0.137 |
| Shadow cascade 3 | 0.368 | 0.373 | 0.368 | 0.368 | 0.365 | 0.367 |
| Depth | 0.151 | 0.144 | 0.151 | 0.153 | 0.152 | 0.152 |
| Light clustering | 0.040 | 0.040 | 0.040 | 0.040 | 0.041 | 0.039 |
| Forward | 1.134 | 1.168 | 1.126 | 1.155 | 1.135 | 1.157 |
| Bloom down 0 | 0.036 | 0.035 | 0.034 | 0.034 | 0.032 | 0.040 |
| Bloom down 1 | 0.008 | 0.012 | 0.010 | 0.012 | 0.008 | 0.013 |
| Bloom down 2 | 0.012 | 0.012 | 0.012 | 0.011 | 0.012 | 0.012 |
| Bloom down 3 | 0.007 | 0.006 | 0.006 | 0.006 | 0.003 | 0.007 |
| Bloom down 4 | 0.006 | 0.006 | 0.006 | 0.006 | 0.006 | 0.006 |
| Bloom up 3 | 0.004 | 0.004 | 0.003 | 0.004 | 0.004 | 0.004 |
| Bloom up 2 | 0.006 | 0.005 | 0.006 | 0.006 | 0.006 | 0.006 |
| Bloom up 1 | 0.005 | 0.005 | 0.004 | 0.005 | 0.005 | 0.006 |
| Bloom up 0 | 0.017 | 0.015 | 0.015 | 0.016 | 0.013 | 0.017 |
| Tonemap | 0.018 | 0.018 | 0.018 | 0.019 | 0.018 | 0.019 |
| FXAA | 0.029 | 0.029 | 0.028 | 0.028 | 0.029 | 0.029 |
| Readback | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
