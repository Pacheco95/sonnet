# Why the far shadow cascade dominates the stress scene on phones

[Issue 173](https://github.com/Pacheco95/sonnet/issues/173). Status: the workload comes from a geometry model and the cost was measured on the Galaxy S25 Ultra on 2026-10-11 (cold, `--play 3`, `android-release`, commit 50c5f4f plus a temporary probe that is not in the tree). The iPad was not re-measured.

## What the workload is

The stress scene is a 100×100 grid at 1.5 m spacing (half boxes, 12 triangles, half icospheres, 320 triangles, each about 1 m across), seen from the scene's camera (y 12, z 40, fovY 1.0, 16:9) with the default `shadowDistance` 80 m, `shadowMapSize` 2048 and the sun from the scene file. `Renderer::computeCascades` was replayed for that camera and each cascade's orthographic footprint was intersected with the grid, which is what the cull shader's frustum test keeps (the depth range is extended 80 m towards the light, so only the footprint decides here).

| Cascade | Split (m) | Radius (m) | Texel (cm) | Casters kept | Triangles drawn | Texels per sphere | Texels per triangle |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0 | 5.4 | 6.7 | 0.65 | 143 | 0.02 M | 18 581 | 58 |
| 1 | 12.1 | 14.1 | 1.37 | 494 | 0.08 M | 4 161 | 13 |
| 2 | 26.3 | 30.4 | 2.97 | 2 214 | 0.37 M | 891 | 2.8 |
| 3 | 80.0 | 93.8 | 9.16 | 10 000 | 1.66 M | 94 | **0.29** |

## Measurement on the S25

A temporary probe (a file in the app's `files/` read by `computeCascades` and the cascade passes) set `shadowMapSize` and skipped a cascade's draws. Cascade 3, cold, one run each, the GPU total in brackets; the 2048 baseline was run three times (8.39, 8.48, 8.40 ms) and the spread is about 0.1 ms.

| Cascade 3 map | Cascade 3 (ms) | Cascade 2 (ms) | GPU total (ms) |
|---|---:|---:|---:|
| 2048 (default) | 8.4 | 1.05 | 19.6 |
| 1024 | 6.33 | 0.86 | 17.2 |
| 512 | 3.70 | 0.75 | 14.5 |
| 256 | 3.21 | 0.47 | 13.8 |
| 128 | 3.36 | 0.37 | 13.9 |
| 2048, cascade 3 draws skipped | 0.01 | 1.35 | 14.7 |

(The skipped run's cascade 2 and forward times are higher than the baseline's with the same work, which is the GPU clock settling lower when the frame is lighter; read the totals of that row with care.)

## Finding

Cascade 3 draws every caster in the scene (its footprint, radius 94 m, covers the whole 150 m grid): 10 000 draws, about 1.66 M triangles, where cascade 2 draws 0.37 M. At 9 cm a texel each icosphere covers about 94 texels with 320 triangles, three triangles per texel.

The cost has two parts, split by the map-size sweep:

- **A floor of about 3.3 ms** that does not move below a 512 map, down to a 128 map where every triangle is far under a texel. This is the cost of submitting 1.66 M triangles (vertex work, setup and binning) with almost nothing rasterised. It is the triangle count, and only fewer triangles remove it.
- **About 5 ms more at 2048** that falls with the map size (6.33 ms at 1024, 3.70 ms at 512). The same triangles cost less when they are smaller than a sample, because those are rejected before they are rasterised and binned into tiles. So the far cascade is expensive exactly where its triangles are about a texel each.

That matches the other observations in the issue: the pass does not change with the render size (its own image is its only target), it is small on an immediate-mode desktop GPU, cascade 2 is cheap with 4.5 times fewer triangles of 2.8 texels each, and 10 000 boxes (0.12 M triangles) cost 1.07 ms.

Of the issue's candidates: the first (every caster survives, each covering well under a texel per triangle) is the mechanism. The second (the extended depth box draws extra casters) does not apply here, since the footprint holds the whole scene. The third is the same effect seen from cascade 2.

An earlier version of this report predicted a smaller map would not help. The measurement says it does help, because it shrinks the rasterisation part, though not the floor.

## What each fix would do

- **Skip casters of only a few texels:** removes nothing in this scene; each sphere is about 11 texels across, only its triangles are small.
- **A smaller map for cascade 3:** saves about 2.1 ms at 1024 and 4.7 ms at 512, at a cost in sharpness (a texel of 18 cm and 37 cm at 80 m). The cheapest change, one setting; needs the shadow-factor view and the cascade regression to judge.
- **A lower level of detail for shadow draws beyond a distance:** the only fix that lowers the 3.3 ms floor, since it lowers the triangle count. The engine has no mesh levels of detail.
- **Drawing cascade 3 less often:** removes the whole 8.4 ms on the frames it is skipped, but needs the cached cascade's matrix and texel snapping to stay valid while the camera moves.

## Fix: a smaller map for the last cascade

`RendererSettings::farShadowMapSize` (unset by default, meaning the same as `shadowMapSize`; capped at `shadowMapSize`) sizes the last cascade's map; the other cascades keep `shadowMapSize`. The player sets it to 1024 on Android and iOS (`__ANDROID__` and `TARGET_OS_IPHONE`; `hostPlatform()` is the cooking host and says Linux on a phone), so desktop output is unchanged: the stress scene's final and shadow-factor captures are byte-identical to the 2048 ones. The texel size and the texel snapping follow the cascade's own size, and the shader already reads each image's dimensions.

- S25, cold stress run, two runs, with the far map set to 1024 for every platform (before the setting became mobile-only; the final build's mobile path was built but not re-run on the phone): cascade 3 6.31 and 6.31 ms (from 8.4), the GPU total 17.5 ms (from 19.6). That is 25 % off the cascade, not the "well below" the issue hopes for: the 3.3 ms floor and the part that follows the map size remain, and 512 would give 3.7 ms at a 37 cm texel.
- Basic sample, desktop, 1280×720: the screenshot with a 1024 far map differs from the 2048 one by at most 1 in 255 and by more than 2 in no pixel.
- The cascade regression (`renderer_tests "[shadow]"`) now runs the far map at 256 and 1024 against the others at 256 and 1024: 207 assertions pass on Lavapipe.
- S25, sustained, `--play 600` at the window's 1080×2340, the final build with the 1024 far map, started cool (status 0, skin 37.8 °C): GPU 27.79 ms, 27.82 ms apart, CPU 15.6 ms, cascade 3 10.25 ms (37 % of the frame), forward 10.05 ms. The earlier run with the 2048 map (`s25-m14.md`) was 31.85 ms and 13.96 ms (44 %). The clock fell the same way, 1200 MHz to 734 MHz by 420 s, but the thermal status stopped at 2 (skin 42.0 °C) and the run lasted about 900 s; the earlier run reached status 3. Part of the 4.1 ms gain may therefore be a milder throttle, but the cascade's own gain at the same 734 MHz, 3.7 ms or 27 %, matches the 25 % of the cold run. The target (33 ms) is met with 5.2 ms to spare, from 1.1 ms.
- Not measured: the iPad ([the follow-up issue](https://github.com/Pacheco95/sonnet/issues)), a second sustained run, the desktop frame time (the pass only got smaller).

## Reproduce

The map-size sweep is not reproducible from the tree: the probe was temporary (set `farShadowMapSize` to repeat it). The geometry table came from replaying `computeCascades` for the stress scene's camera and sun against its 100×100 grid.
