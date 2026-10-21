# ADR-0034: Glass under TAA is judged against what its content allows

- **Status:** Accepted
- **Date:** 2026-10-21

## Context

[ADR-0033](0033-transmission.md#taa) gated glass under TAA on four cases, after 32 warm-up frames: in every measured frame the glass region's error against a 16-sample reference had a mean of at most 2 levels and a 99th percentile of at most 8, and every pixel a moving pane left was within 8 levels after four frames. It called these acceptance budgets, not measured results. [#339](https://github.com/Pacheco95/sonnet/issues/339) measured them ([the report](../reports/transmission-taa.md)) and found:

- Only smooth glass meets them. `rough-pane` passes; `watch` and `transmission-test` fail at rest as well as in motion (`transmission-test-still`, with nothing moving, is at 2.93 / 28), because a single clamped history cannot hold detail finer than a pixel. A resolve that reaches 0.89 / 4 on that case is an unclipped, evenly weighted average of 16 jitters, and the clip it drops is what keeps `watch` from ghosting ([the stationary error](../reports/transmission-taa.md#the-stationary-error-350)).
- No motion treatment changes this: the glass's own motion ([#352](../reports/transmission-taa.md#glass-motion-352)) and every resolve variant tried on the turning TransmissionTest ([#361](../reports/transmission-taa.md#the-rotating-error-361)) improve one case and cost another. That case's one-sample error of 10.6 holds 3.4 for the content itself (the change from one frame to the next that no whole-pixel displacement explains, `TemporalFloor`); accumulation takes about 1 of the rest, a shorter or disagreement-aware history up to 3 more, and about 4 are the aliasing of changing sub-pixel detail, which a single history cannot average.
- The recovery rule cannot tell a ghost from detail. TransmissionTest has 25 to 47 pixels in its swept area four frames after the glass leaves, and up to 72 % of them are more than 8 levels off with no ghost, because the content is aliased there; a ghost of the masked pane has 15 % beyond 16 levels where TransmissionTest has 55 %.

So the gate measures how well one history accumulates fine detail rather than how the glass is handled, and no resolve change reaches it without breaking another case. A history per layer was the other candidate; it is below.

## Decision

**The covered pixels (what the glass covers in the frame) are held to a budget that grows with how much their content changes, and the pixels the glass has left are held to what an unresolved sample of the same frame leaves on them. The harness judges against this, and the four required cases pass it.**

For each measured frame, with the same reference, region and 32 warm-up frames as ADR-0033, the same error (the largest RGB-channel difference in the display-encoded output, in levels out of 255) and `f` the *temporal floor* of the covered pixels (`TemporalFloor::best`: the reference's own change since the previous frame, at the best whole-pixel displacement within 6 pixels, chosen on a 3×3 patch):

1. **Covered mean** at most `4 + 2.5 × f.mean`.
2. **Covered 99th percentile** at most `40 + 1.5 × f.p99`.
3. **Pixels the glass has left.** Of those the glass left four frames ago and nothing covers now, once there are at least 100 of them, the mean error is at most 3 levels more than that of one unresolved, jittered sample of the same frame over the same pixels.

The first two are the rest budget (a pane whose content does not change since the frame before has `f = 0`) plus an allowance per level of change a motion vector cannot explain. The third is the recovery rule: a history that outlives the glass shows as error where an unresolved sample has none.

*Where the numbers come from.*

- **4 and 40** are the rest floor of the covered pixels: `transmission-test-still` at 3.21 / 31 in its worst frame, `watch`'s frames with no change at up to 1.3 / 20, `masked-pane` after it stops at up to 1.3 / 6. They sit 25 to 30 % above the worst. The budget they replace (2 / 8) was reached only by the unclipped average above.
- **2.5 and 1.5** are what the turning TransmissionTest needs: 10.76 / 61 over a floor of 3.43 / 27 needs at least 2.0 for the mean and 0.8 for the percentile, so the slopes leave it 8 % and 20 % of slack in its tightest frames (0.9 level, 15 levels). `transmission-test-still` has 0.8 level and 9 levels, everything else more.
- **100 pixels and 3 levels** come from the masked pane. Its pixels four frames after the pane leaves, a thousand or more, are 0.9 to 1.9 levels worse than a sample (0.9 for `masked-pane`, 1.9 for the 8 px a frame `masked-pane-fast`) with the mark drop, and 4.3 to 6.0 worse without it (`GlassMarkChange` above 1: the history never dropped, the ghost the harness found first). TransmissionTest's 25 to 47 pixels are too few to be a statistic of anything but its aliased edges.

*What this lets through that ADR-0033's gate does not*, all measured in [the report](../reports/transmission-taa.md#the-gate-360): `watch` (p99 23 against 8), `watch-fast`, TransmissionTest turning (10.76 / 61) and at rest (3.21 / 31), and the masked pane while it moves (p99 19).

*What it still fails.* Measured on the variants of [#350, #352 and #361](../reports/transmission-taa.md) with rules 1 and 2 (the older runs have no sample to judge recovery, and no floor in their first frame): the evenly weighted, unclipped averages of eight and sixteen frames on TransmissionTest (all 15 frames; means of 16.8 and 24.8); a history without the clip's or the motion's term in the blend (4 and 5 of 15); a clip box of 3 deviations (2); a clip that relaxes from half a box inside (the still case, 12 of 15, at 5.03 / 48); and, by rule 3, the build with no mark drop, on the masked pane (frame 43 of both runs).

*What it does not tell.* It is a regression guard on this content, not a proof that TAA helps: no history at all (age cap 1) passes 14 of 15 TransmissionTest frames (10.64 / 75 against 9.74 / 59), because the budget is more than a third above what the content needs. The glass-motion experiment also passes. Whether TAA is worth its cost on turning sub-pixel glass is a different question, and the answer the numbers give ([#361](../reports/transmission-taa.md#the-rotating-error-361)) is "about 1 level".

*Diagnostics, not budgets:* the opaque error outside the region, the uncovered part of the region, the glass region's own mean and percentile, the floor at the pixel itself, and the error of the unresolved sample over the covered pixels.

*A history per layer* (the glass layer and what is behind it resolved apart and composed after) is **rejected for now**. It would cost a second history and a second resolve, on mobile where the transmission copy alone is over its 2 ms on the iPad, and the content that fails a single history is not a translation of anything (the best single vector takes TransmissionTest from 3.83 to 3.43), so two histories would not obviously explain it either. It is reconsidered when a scene whose covered content a vector does explain (`f.mean` under 1) exceeds the covered budget: that is the failure a second layer's motion could fix, and none of the four required cases is one.

## Consequences

- `TaaCheck`'s budgets and `FrameResult::pass` change; `evaluate` takes the error image of an unresolved sample of each frame, and the caller sets each result's `floor`. The harness renders one jittered sample per frame beside the reference (the first of its 16, tone mapped alone), gives the first measured frame a floor (the last warm-up frame now has a reference), and writes the sample's error and the vacated pixels' statistics to `metrics.csv`. Its exit code is 1 when a judged case fails, as before.
- ADR-0033's TAA acceptance, the measurement and the report, is met by this gate. Its mobile acceptance and the phone player's setting are not changed (they are [#342](https://github.com/Pacheco95/sonnet/issues/342) and its follow-ups).
- [#339](https://github.com/Pacheco95/sonnet/issues/339) closes with this change, and the TAA part of [#254](https://github.com/Pacheco95/sonnet/issues/254) with it.
- The constants are fitted to four cases and a RTX 4090 Debug build; a fifth case or another device is checked against them, not tuned into them silently. A change of any of them is a new ADR.
- The percentile budget is loose enough (40, and up to 80 for TransmissionTest) that it will not see a ghost the mean does not; the recovery rule is what does.

## Alternatives considered

- *The covered error against the same scene's opaque error.* TransmissionTest is 10.8 against 0.33, 33 times, and `rough-pane` 0.2 against 1.5: the opaque pixels measure how plain the wall is, not what the glass can reach, so no ratio separates a regression from the scene.
- *A budget on the frames after the glass stops alone.* It would judge `masked-pane` (11 to 14 of 24 frames pass the old budget) and `transmission-test-still`, but not TransmissionTest turning, `watch` or the ghost while it moves, which are the cases the glass handling matters in.
- *A 99th percentile without the pixels within a pixel of a silhouette or cut-out edge.* The failing detail is inside the glass (reflections, refracted stripes, rims) as much as at its outline, and the harness would have to find the edges of a masked pane's cut-outs, which the silhouette pass does not know; it was not measured, and the budget it would replace sits on the interior.
- *The error against the unresolved sample, for the covered pixels too* ("TAA is no worse than not accumulating"). The sample is better than TAA in the frames that matter (`masked-pane` p99 13 against 19, TransmissionTest 8.9 against 10.8 for a sample at the pixel's centre), so the rule would fail the cases the decision is to accept; as a recovery rule on the vacated pixels it separates the ghost, so it is used there.
- *Pixels beyond 8 levels, as a count or a share of the vacated pixels.* 0.7 % of the masked pane's after the fix, 16 % with the ghost, up to 72 % for TransmissionTest without one.
- *Keeping the budgets and the mitigation sequence.* It leaves #339 and #254 open with a resolve that cannot meet them without breaking `watch`.
- *A history per layer now.* Above.
- *Sixteen jitters with an evenly weighted, unclipped average at rest.* It meets the old rest budget (0.89 / 4) and breaks `watch` (2.91 / 55), and the jitter contract of [ADR-0024](0024-temporal-anti-aliasing-and-occlusion-culling.md) says eight.
