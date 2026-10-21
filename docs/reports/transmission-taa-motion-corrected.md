# TAA glass motion with the previous rendered pose

The `RG16F` glass-motion experiment from `5db1a8dbe7ffe2cb7d0809aff9757ce20f5b7e74` helps the rotating TransmissionTest once the harness supplies a full frame of object motion: worst mean error falls **31.8%, from 12.369 to 8.440**, and worst p99 falls from **68 to 57**. It also worsens the watch and masked pane. This corrects the earlier conclusion that glass motion did not help rotating detail. [ADR-0034](../decisions/0034-taa-glass-gate.md) has since replaced ADR-0033's acceptance criteria; the full corrected gate results are reported below.

The renderer, shaders, fixtures, reference and evaluation region are unchanged from that revision. This work fixes harness bookkeeping, adds regression tests, replaces the misleading “temporal floor” terminology, and reruns all seven cases against both the original ADR-0033 error metrics and the current ADR-0034 gate. The [original report and measurements](transmission-taa.md#glass-motion-352) remain as historical data.

## Motion interval and setup

The harness advances the simulation four times before rendering. `PreviousWorldTransform` records the previous **update**, so using it directly supplies only the final substep's object motion to a history from the previous **render**. For uniform translation this is one quarter of the intended displacement. Both the background path and the optional glass path were affected.

The TAA run now keeps a separate snapshot of rendered draws and calls `runtime::taa::usePreviousFrameTransforms` before each render. Matching uses nonzero entity id, the complete mesh handle including generation, and submesh. New or unmatched draws use their current transform. The `SceneView` stays at a stable address, which is the renderer's history key. Previous camera matrices and skinned buffers already advance per render; this fix does not change the world or the production renderer's transform contract. `summary.json` records `motionInterval: rendered-frame`.

Measurement setup on 2026-10-21:

- Base: `5db1a8d`, with these local harness and diagnostic changes; GCC 14, CMake `Release`, RTX 4090, NVIDIA 595.71.05, Vulkan 1.4.329, loader 1.4.357. These captures use the Release validation default, off.
- 1280×720, 1/60 s per frame, four simulation updates per frame, 32 warm-up frames, eight TAA jitters, history age capped at 16.
- Frames 32–47 are measured, extended through 55 for both masked-pane cases to include their stopped tail.
- Reference: the same 16 Halton samples, averaged in linear HDR and tone mapped once. Neither treatment changes that reference or its mip bias.
- Comparison region and error: current and recent transmissive coverage, swept substep coverage, and two-pixel dilation, exactly as [the harness defines it](../rendering.md#taa-checks); largest absolute RGB-channel difference per pixel in display levels. For comparison with historical ADR-0033 results, the archived metrics also count frames meeting mean ≤2, p99 ≤8 and pixel recovery within 8 levels. The harness exit status now follows ADR-0034.

[`tools/analyze_taa_motion.py`](../../tools/analyze_taa_motion.py) checks all 128 paired measured reference frames for pixel identity, matching frame metadata, coverage counts and reference residuals. **All pairs are pixel-identical.** [`comparison.json`](transmission-taa-metrics/glass-motion-corrected/comparison.json) contains their SHA-256 hashes and the aggregate results. The per-case `metrics.csv` and `summary.json` files alongside it preserve both treatments. The four required cases determine the harness exit status; the three stress/still diagnostics are reported against the same budgets without changing that status.

## Corrected acceptance results

Each error cell is **worst mean / worst p99 / most unrecovered pixels** across the measured frames; these maxima need not occur in the same frame. “Passing” counts frames meeting all three budgets, not successful cases. A required case passes only if every measured frame does.

| Case | Background motion | Glass motion | Frames meeting all legacy ADR-0033 limits, background → glass |
|---|---|---|---|
| `rough-pane` | 0.211 / 2 / 0 | 0.217 / 2 / 0 | 16/16 → 16/16 |
| `masked-pane` | 1.873 / 19 / 7 | 2.642 / 21 / 2 | 14/24 → 14/24 |
| `watch` | 1.507 / 23 / 0 | 1.883 / 32 / 0 | 0/16 → 0/16 |
| `transmission-test` | 12.369 / 68 / 32 | 8.440 / 57 / 32 | 0/16 → 0/16 |
| `masked-pane-fast` (diagnostic) | 2.405 / 21 / 257 | 3.154 / 21 / 356 | 13/24 → 13/24 |
| `watch-fast` (diagnostic) | 1.618 / 26 / 0 | 1.802 / 28 / 0 | 0/16 → 0/16 |
| `transmission-test-still` (diagnostic) | 2.928 / 28 / 0 | 2.928 / 28 / 0 | 0/16 → 0/16 |

Under the legacy ADR-0033 limits, only `rough-pane` passes every frame. Both watch cases meet the old mean and recovery thresholds in all frames but miss p99 in all frames. Rotating TransmissionTest misses all three old limits in every frame; the still variant misses mean and p99 in every frame. These counts do not determine the harness exit status after ADR-0034.

The motion fix changes the comparison, not just a label. The original rotating measurements were 9.741 / 59 for background and 9.886 / 56 for glass motion. With corrected motion those become 12.369 / 68 and 8.440 / 57. The original `watch-fast` background result was 1.815 / 29; the corrected result is 1.618 / 26, which also changes how its comparison reads. Historical captures used Debug, while this pair uses Release; tiny numerical differences across builds are not attributed to the motion fix. The paired comparison above uses the same binary, differing only in `--glass-motion`.

### Where the difference is

Splitting the unchanged acceptance region by current glass coverage gives the following worst mean / p99. “Uncovered” is the rest of that region, not the whole opaque image.

| Required case | Covered, background → glass | Uncovered, background → glass |
|---|---|---|
| `rough-pane` | 0.198 / 1 → 0.204 / 1 | 1.673 / 8 → 1.673 / 8 |
| `masked-pane` | 1.821 / 19 → 2.608 / 22 | 3.676 / 19 → 3.669 / 19 |
| `watch` | 1.553 / 23 → 1.942 / 33 | 0.266 / 7 → 0.266 / 7 |
| `transmission-test` | 14.360 / 71 → 9.091 / 59 | 6.126 / 51 → 6.405 / 52 |

The rotating gain is concentrated in covered pixels, while uncovered error rises slightly. The pane and watch regressions are also concentrated in covered pixels. Coverage alone cannot distinguish a glass reflection from content seen through it, so the analysis adds narrower diagnostic masks. These are composites of the rendered image, not isolated shader lobes, and never replace the acceptance region.

For the watch, the interior is a circle centred at (640, 350), radius 140. Pixels varying by at least 8 levels across the measured references, dilated by two pixels, define moving content. The remainder is stationary interior. For the rough pane the same method uses a rectangle [180, 1100) × [100, 620) and a 4-level threshold. For masked panes, the fixture's camera, translation, stop time and lattice alpha define the current coverage; its two-pixel erosion is the transmitted-pattern interior, and dilation minus erosion identifies lattice edges. TransmissionTest uses [545, 735) × [286, 382), covering the clear and rough sphere rows while excluding the metallic row.

| Diagnostic region | Background, worst mean / p99 | Glass, worst mean / p99 |
|---|---|---|
| Watch moving content | 3.514 / 63 | 6.294 / 114 |
| Watch stationary interior | 1.197 / 15 | 1.220 / 16 |
| Fast watch moving content | 2.675 / 50 | 3.250 / 79 |
| Masked-pane transmitted pattern | 0.840 / 4 | 3.726 / 7 |
| Masked-pane lattice edges | 6.794 / 70 | 9.026 / 67 |
| Fast-pane transmitted pattern | 1.440 / 6 | 3.570 / 7 |
| Fast-pane lattice edges | 7.376 / 106 | 9.456 / 92 |
| Rough-pane moving content | 0.315 / 1 | 0.339 / 1 |
| Rotating clear/rough sphere rows | 6.989 / 59 | 5.235 / 49 |
| Still clear/rough sphere rows | 1.769 / 18 | 1.769 / 18 |

The watch loses most in moving content behind still glass, consistent with assigning zero glass motion to moving hands. The pane loses strongly in the pattern seen through its moving bars. These measurements support the layer-mismatch explanation, but do not identify a unique cause at every pixel. Conversely, the rotating sphere rows improve in both mean and p99, so the prior claim that glass detail gains nothing is unsupported after correcting motion.

[Per-frame diagnostic measurements](transmission-taa-metrics/glass-motion-corrected/diagnostics.csv) include the remaining regions. The native-pixel crops below show reference, background motion and glass motion on top; the diagnostic region and the two errors multiplied by 16 are underneath. They show the glass treatment's worst-mean frame for each case. Pink marks the auxiliary diagnostic region, not the acceptance mask.

![Watch, frame 33](transmission-taa-metrics/glass-motion-corrected/images/watch-033.png)

![Masked pane, frame 39](transmission-taa-metrics/glass-motion-corrected/images/masked-pane-039.png)

![Rotating TransmissionTest, frame 47](transmission-taa-metrics/glass-motion-corrected/images/transmission-test-047.png)

### Stopped pane

Frame 39 includes the final movement; frame 40 is the first full frame at rest. The normal masked pane's tail does not justify declaring success as soon as it stops:

| Frame | Background mean / p99 / pixels over 8 | Glass mean / p99 / pixels over 8 | Frames meeting the legacy pixel-recovery rule, background / glass |
|---|---|---|---|
| 40 | 1.281 / 8 / 0 | 1.512 / 10 / 0 | yes / no |
| 41 | 1.583 / 9 / 0 | 1.829 / 10 / 0 | no / no |
| 42 | 1.006 / 6 / 0 | 1.009 / 7 / 0 | yes / yes |
| 43 | 1.288 / 7 / 7 | 1.300 / 8 / 0 | no / yes |
| 44 | 1.146 / 5 / 0 | 1.175 / 5 / 0 | yes / yes |
| 48 | 1.103 / 5 / 0 | 1.093 / 4 / 0 | yes / yes |

By the legacy pixel-recovery metric, glass motion passes continuously from frame 42 through 55 versus frame 44 for background motion, and reduces the whole-run maximum count above 8 levels from 7 to 2. This metric is retained for comparison; ADR-0034 instead compares the vacated region's mean error with an unresolved sample. The fast pane has 13/24 frames meeting all legacy limits in both treatments and a higher maximum count above 8 levels with glass motion, 356 versus 257.

![Masked pane at rest, frame 48](transmission-taa-metrics/glass-motion-corrected/images/masked-pane-048.png)

## Reference motion residual is not an error floor

`ReferenceMotionResidual` replaces `TemporalFloor`; `referenceMotionResidual` replaces `temporalFloor`. The numerical matching algorithm is unchanged: compare consecutive references at the same pixel (`unshifted`) and after choosing a whole-pixel displacement in ±6 pixels by the RGB L1 difference of a 3×3 patch (`integerMatched`). Report the centre pixel's largest channel error and exclude pixels within eight pixels of the image border. This is a bounded patch-matching diagnostic, not a bound over arbitrary motion or accumulated TAA histories.

The regression test constructs `previous(x) = 10 + 6x` and `current(x) = 7 + 6x` in each RGB channel. A half-pixel bilinear read reconstructs the current ramp exactly: error 0. Integer matching still reports mean 3 and p99 3. Thus even a pure fractional translation can produce a nonzero residual; it cannot prove irreducible content error or justify dividing the acceptance error by that residual. Motion outside the search radius and patch-based selection introduce additional limits.

| Case | Unshifted reference residual, mean / p99 | Integer matched, mean / p99 |
|---|---|---|
| `rough-pane` | 0.418 / 5 | 0.131 / 2 |
| `masked-pane` | 2.212 / 93 | 0.927 / 32 |
| `watch` | 3.197 / 150 | 0.467 / 7 |
| `transmission-test` | 3.833 / 31 | 3.431 / 27 |
| `masked-pane-fast` | 5.467 / 115 | 2.621 / 73 |
| `watch-fast` | 2.593 / 133 | 0.818 / 18 |
| `transmission-test-still` | 0 / 0 | 0 / 0 |

These are maxima across reference pairs and are identical for both treatments. The corrected harness captures the last warm-up reference, so every measured row, including the first, has populated `residual_unshifted_*` and `residual_matched_*` columns. Earlier archived `floor_*` columns retain their recorded values and names; their summarizer describes them as reference matching residuals, not error bounds. The accepted ADR-0034 gate uses the bounded integer-match statistic as a policy input, which does not prove an irreducible error floor.

## Cost and remaining scope

The existing renderer experiment remains exactly the one in `5db1a8d`: a separate `RG16F` target, a depth-tested draw of blended/transmissive objects, and one more texture read in the resolve, selected by the current pixel's glass mark. Storage is 4 bytes per pixel: **3.52 MiB at 720p, 7.91 MiB at 1080p, 31.64 MiB at 4K**, excluding allocation overhead. The target and pass are absent when the setting is off. No second colour history or layered resolve is introduced.

The [existing Release benchmark](transmission-taa.md#glass-motion-352) is retained: a wall and 100 overlapping transmissive spheres at 1080p, seven timing samples per treatment, three runs on the RTX 4090. It recorded about **0.018 ms** for `glass motion`, about **0.001 ms** extra in TAA at timestamp resolution, and 0.143–0.146 ms for forward transparency. These are the earlier benchmark's observations, not new timing claims from the quality captures. The benchmark supplies explicit previous transforms and is independent of the harness bug. Other GPU capture work was active during this follow-up, so no new performance comparison is inferred from it. Phone cost remains unmeasured.

One experimental limitation also remains: the motion pass reuses the pre-pass alpha sample without the forward pass's TAA mip bias. Cutout coverage can therefore differ. This comparison preserves that implementation to measure the effect of the harness correction; a candidate for adoption still needs matching coverage validation. The narrower diagnostics cannot isolate that contribution from reprojection, clipping or reconstruction.

The corrected evidence supports retaining the background-motion default for now: glass motion lowers the moving TransmissionTest error (12.4 / 68 to 8.4 / 57 over the region) but worsens covered-detail metrics in the watch and masked-pane cases. See the corrected ADR-0034 case results below. ADR-0034 is restored by this stacked follow-up; these measurements clarify its scope but do not establish a mathematical lower bound on TAA reconstruction error.

## Reproduction and verification

Use the pinned TAA sample assets described in [rendering.md](../rendering.md#taa-checks). Configure a Release build with the project's normal prerequisites; the explicit override below selects `Release` rather than the preset's `RelWithDebInfo`. The measured local build reused the existing vcpkg installation with `VCPKG_MANIFEST_INSTALL=OFF` and `VCPKG_INSTALLED_DIR` pointing to it.

```sh
cmake --preset linux-release -B build/release-check -DCMAKE_BUILD_TYPE=Release
cmake --build build/release-check --target sonnet_taa_harness_app sonnet_samples runtime_tests renderer_tests --parallel
./build/release-check/apps/taa_harness/sonnet_taa_harness \
  build/release-check/apps/taa_harness/samples/taa \
  --out build/taa-motion/background --keep-frames
./build/release-check/apps/taa_harness/sonnet_taa_harness \
  build/release-check/apps/taa_harness/samples/taa \
  --out build/taa-motion/glass --keep-frames --glass-motion
python3 tools/analyze_taa_motion.py \
  --background build/taa-motion/background --glass build/taa-motion/glass \
  --out docs/reports/transmission-taa-metrics/glass-motion-corrected
./build/release-check/modules/runtime/runtime_tests '[taa]'
./build/release-check/modules/renderer/renderer_tests '[taa],[transmission]'
# Optional separate performance run on an idle GPU:
./build/release-check/modules/renderer/renderer_tests '[glass-motion]'
```

Run both harness commands to compare background motion and `--glass-motion`. Their exit statuses follow ADR-0034. Analysis requires NumPy and Pillow and the full `--keep-frames` output at 1280×720. It archives compact metrics, reference hashes and nine crops; the full frame captures stay in the build directory.

Verification completed: Release build; runtime `[taa]` **185 assertions in 11 cases**; renderer `[taa],[transmission]` **584 assertions in 29 cases**; both complete seven-case quality runs; 128 identical paired references and matching coverage/residual columns. Regression coverage includes four updates per render, advancing snapshots, reordered submeshes, new ids, mesh generation changes, unmatched draws and the fractional-motion counterexample. The legacy error metrics above are diagnostics; the appended table records the corrected acceptance results under ADR-0034.

## Corrected ADR-0034 results

The harness now evaluates the restored ADR-0034 criteria: covered mean ≤ `4 + 4 × referenceMotion.mean`, covered p99 ≤ `40 + 2 × referenceMotion.p99`, and (when at least 100 pixels qualify) vacated-region mean ≤ unresolved-sample mean + 3. All four required cases pass. This report's first ADR-0034 draft had slopes 2.5 and 1.5, at which `transmission-test` passed only 4/16 frames without glass motion and 16/16 with it; the slopes were then refitted to 4 and 2 on these corrected measurements ([the gate](transmission-taa.md#the-gate-360)), and the table counts frames at the refitted slopes (the `gate/` run, whose first row has a residual). The other three required cases pass every frame in both treatments. The three diagnostic cases also pass all their measured frames in both treatments.

| Case | Background motion | Glass motion | Frames passing ADR-0034, background → glass |
|---|---|---|---|
| `rough-pane` | 0.198 / 1 / 0.000 | 0.204 / 1 / 0.000 | 16/16 → 16/16 |
| `masked-pane` | 1.821 / 19 / 2.944 | 2.608 / 22 / 2.949 | 24/24 → 24/24 |
| `watch` | 1.553 / 23 / 0.000 | 1.942 / 33 / 0.000 | 16/16 → 16/16 |
| `transmission-test` | 14.360 / 71 / 20.576 | 9.091 / 59 / 20.205 | 16/16 → 16/16 |
| `masked-pane-fast` (diagnostic) | 2.160 / 20 / 5.244 | 2.905 / 24 / 4.763 | 24/24 → 24/24 |
| `watch-fast` (diagnostic) | 1.667 / 27 / 0.000 | 1.857 / 29 / 0.000 | 16/16 → 16/16 |
| `transmission-test-still` (diagnostic) | 3.210 / 31 / 0.000 | 3.210 / 31 / 0.000 | 16/16 → 16/16 |

Cells show worst covered mean / worst covered p99 / worst vacated-region mean; the maxima can occur on different frames. The first three columns follow the applicable budgets above, and recovery failures are included in the frame count. At the draft's slopes the moving TransmissionTest result was the only required-case gate outcome changed by this treatment; at the refitted ones neither treatment fails a required case. This does not mean lower global error in every scene: the watch and masked-pane covered metrics worsen, while remaining within their ADR-0034 budgets. The harness remains disabled by default pending the broader trade-offs and matching cutout coverage noted above.
