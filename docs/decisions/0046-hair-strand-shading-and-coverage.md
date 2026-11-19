# ADR-0046: Hair strand shading and TAA-resolved coverage

- **Status:** Proposed. The owner accepts it after the spike and the phone measurements ([#609](https://github.com/Pacheco95/sonnet/issues/609)), not before; the "Open until the spike" part of the Decision is filled in first.
- **Amends:** [ADR-0041](0041-skin-shading.md)
- **Date:** 2026-11-19

## Context

[ADR-0041](0041-skin-shading.md) kept the humanoid's hair on the Standard model and the plain cutoff, and left strand shading and TAA-resolved coverage to a later ADR. [#591](https://github.com/Pacheco95/sonnet/issues/591) asks for it, from a spike with captures and timings. This ADR decides what is already clear from the existing evidence and marks what the spike must still settle. It is Proposed until then.

What the [skin and hair spike](../reports/skin-hair-spike.md#hair) and the code say:

- **Content.** The hair is a painted scalp cap, one submesh of `Body`, whose base colour is two mirrored, fan-shaped islands of painted strands. No constant U or V axis follows them: V is off by up to 45° at the fans' edges, with a sign flip at the mirror line, so the highlight is seamed and faceted. The painted strands do carry a usable direction: a structure tensor of the base colour's luminance has a median coherence of 0.55 to 0.61 over windows of 4 to 16 texels. A baked flow map is plausible, but nothing has been rendered with one yet.
- **Shading.** Two shifted Kajiya-Kay lobes cost +0.006 ms for skin and hair together on the RTX 4090, with the head at about half of a 1080p frame.
- **Coverage.** The spike's dithered mask failed under today's resolve ([ADR-0024](0024-temporal-anti-aliasing-and-occlusion-culling.md), with the age-weighted blend since): the 3×3 mean ± 1.25 σ clip box of `taa.slang` is mostly skin where coverage is low and pulls the history back to it. The hairline's temporal p99 over one TAA cycle was 12.3 sRGB levels against 2.5 for the cutoff, and after the camera stops the dither is about four frames behind the cutoff. The spike's threshold already varied with the TAA phase, so a moving threshold alone does not fix it.
- **Resolve constants are fitted.** [ADR-0034](0034-taa-glass-gate.md) fitted the resolve's constants to four glass cases and says a change of any of them is a new ADR. The scene's alpha is 1 for opaque and masked output and 0 for glass, which the resolve reads as the glass mark (neighbours below 0.5), so values from 0.5 to 1 are free.
- **Phone cost.** On the Galaxy S25 Ultra, skin and hair together added about +0.47 ms p95 in the spike, of which skin alone is +0.12 to +0.13. That leaves about +0.33 to +0.35 ms p95 for hair, over a 0.3 ms budget (a subtraction of two measured figures; the median is unusable, the clock switched levels, and hair alone was never isolated on the phone). The iPad was not measured.
- **Two pre-pass facts.** The pre-pass cuts masked draws with `Sample` and the forward pass with `SampleBias` (the mip bias is -0.5 under TAA), so a stochastic test evaluated in both would leave clear-colour holes. The existing masked fade is a separate screen-door test on the tint's alpha ([ADR-0041](0041-skin-shading.md#fade-on-masked-draws)).

Strand shading and coverage are independent: either can fail its gate without blocking the other, and this ADR is written so that the strand model can be accepted with the plain cutoff.

## Decision

**Hair gets a `Hair` shading model: two-lobe Kajiya-Kay along a flow map baked from the painted texture and stored in a new `strand` texture slot, compiled into forward and pre-pass pipelines of its own. The TAA resolve changes only for flagged hair pixels. Skin and hair share the one `RendererSettings::characterShading` switch.** Which coverage technique hair uses, and the numeric stability and trail gates, are open until the spike (below). This ADR adds no code by itself.

### Strand direction and storage

- **A flow map baked from the painted texture.** A tool under `tools/` bakes it from the base colour's local orientation (a structure tensor of the luminance), so the highlight follows what is painted and the mirror seam is solved by construction. The bake smooths the orientation over a window and blends to a combed field (crown outward and down, projected into each triangle's tangent frame and rasterised into UV space) where coherence is low. The window and the coherence threshold are set in the build card from the spike's captures.
- **Encoding.** The direction is the doubled angle (cos 2a, sin 2a) in R and G, so bilinear filtering and block compression are safe, and a shift in B. The shader recovers the direction and flips its sign so the strand's tip points down in world space, which gives the lobe shift a consistent root-to-tip sign. The texture is a linear data texture.
- **A new `strand` texture slot** on the material, with its own transform and sampler as the other slots have. The slot is read only by the `Hair` model.
- **Content.** The humanoid's glTF stays untouched: glTF materials cannot carry the model and the importer never sets `Hair`, as it never sets `Skin`. The sample ships a `Hair.material.json` and the baked flow texture next to the humanoid and assigns the material to the hair submesh's slot of the character prefab ([ADR-0041](0041-skin-shading.md#representation)'s per-submesh slots).

### The model

- **`ShadingModel::Hair`** is the next value of the model in the flag bits 11–12 of `GpuMaterial`, after `Skin`.
- **Two shifted Kajiya-Kay lobes** (Scheuermann): a primary lobe, white, and a secondary lobe tinted by the base colour with its own shift and a wider exponent, each shifted along the normal by the strand's shift. The diffuse is wrapped. The specular image-based lighting is sampled off the cross-strand normal at a higher roughness. The lobes are normalised with `(sqrt(e) + 1) / (2π)`: the 2D `(e + 2) / 2π` turned the spike's cap white.
- **Material fields.** The primary and secondary shift, the secondary's width and strength, and the strand slot's texture, transform and sampler. They live in `.material.json` and are cooked into the bundle. Their defaults are fitted in the build card from the spike's captures.
- **`GpuMaterial`** grows for the strand slot (a UV row, an offset, an index and a sampler word) and the lobe fields. The shading row of ADR-0041 has two free words, so a further row is added; the size is fixed when the work lands.
- **Formats.** `MaterialFileVersion` goes to 8 (7 when this was written); a file of an older version loads as Standard. `BundleVersion` goes to the next free number when this lands, since [ADR-0045](0045-positional-slot-material-overrides.md) ([#600](https://github.com/Pacheco95/sonnet/issues/600), which takes 22 if it lands first) also bumps it and the later of the two takes the next number. The inspector gains the model's rows and the strand slot, and `docs/assets.md` the fields.

### Shader strategy

- **No per-draw branch.** `forward.slang` gains a `hairFragmentMain` entry, and the shared generic body takes the model as a compile-time parameter, exactly as for Skin. The forward pipelines grow from model × double-sided (four) to six. The model joins the opaque batch key, so a Hair draw never shares a batch with another model.
- **Separate hair pre-pass entries and pipelines.** The depth and motion pre-pass gets hair entries (and their double-sided variants), picked from the batch's model. Standard draws keep today's entries, so they compile to today's code and the pre-pass of a Standard scene is unchanged. The culling batches are already split by model.
- **Pipelines on demand.** The hair pipelines, forward and pre-pass, are built only while `characterShading` is on, with the slot mechanism of the skin pipelines ([#610](https://github.com/Pacheco95/sonnet/issues/610)). Off, a `Hair` draw is drawn by the Standard pipeline and ignores the strand slot, so one bundle runs everywhere. A blended, transmissive or unlit material with the `Hair` model shades as Standard.
- **A debug term.** `--shading-term strand` shows the recovered direction.

### Coverage invariants

These hold whichever technique the spike picks, and are the testable part:

- Standard draws render pixel for pixel as today, including `showcase_taa.png`.
- The pre-pass decides stochastic coverage alone, and the forward pass's equal depth test agrees with it: no clear-colour holes.
- The fade keeps its meaning. The hair's coverage noise is independent of the fade's ordered threshold, so a faded hair draw keeps coverage × tint of its pixels and not the minimum of the two.
- Shadows test the cutoff without the tint and without coverage noise, as masked draws do today, so cast hair shadows stay as hard as they are.
- With TAA off, or a debug view on, hair takes the plain cutoff, as the spike did.
- Coverage applies to hair only. The flag below is designed so that eyebrows, eyelashes and other masked materials can follow in a later decision.

### The resolve

- **A flagged-pixel exception only.** The hair forward entry writes 0.75 in the scene's alpha for pixels whose coverage is stochastic. The glass mark (below 0.5) is unchanged, and the resolve already counts alpha below 0.5 in its 5×5 loop.
- **Confined by construction.** `taa.slang` treats only a pixel with a flagged neighbour differently. Every other pixel is bit-identical to today, the fitted constants of ADR-0034 are unchanged, and no global change of the clip box or of the 3×3 shape is made.
- **Verification.** The ADR-0034 harness's four required glass cases and `showcase_taa.png` are re-run with the exception in. They have no flagged pixels, so their output is asserted unchanged.
- **Scope.** The rule applies when the spike's coverage technique needs it (below). It adds to the resolve of ADR-0024, which this ADR does not otherwise change.

### Platforms and gates

- **The switch.** One `RendererSettings::characterShading` covers skin and hair: on by default on the desktop, off in the Android and iOS players until each platform is measured. Splitting it later is small: a second `bool`, with no file format change; the setting is read by the renderer, the capture options, the player and the tests.
- **Hair's budget is 0.3 ms.** On a phone, hair may add at most 0.3 ms of total GPU time to the median and to the p95, in cold and sustained windows, over skin alone, with the head at about half of a native-resolution frame. It is the enabling gate: an unmeasured or failing phone keeps hair off. Standard-material scenes regress by at most 0.1 ms, as in ADR-0041, and the Standard pipeline compiles to the same code as today.
- **A reduced phone hair is the pre-agreed fallback.** If the full hair misses the budget on a phone, that platform runs one lobe with the cutoff coverage, and is measured against the same gate. How the reduced form is selected (a lighter entry or a second setting) is left to the build card; a second setting is the switch split above.
- **Method.** ADR-0039's measurement method and the repository's temperature-based S25 cooldown rule. Each cold round reports its minimum and maximum beside the median, since the spike's median was bimodal. The iPad is measured separately. Desktop results do not validate a phone.

### Open until the spike

The spike runs on the in-repo TAA harness with new hair metrics (the per-pixel temporal deviation over a TAA cycle in the hairline region, and the trail after a camera stop, in code, since the spike's scripts are outside the repository) on the RTX 4090 and Lavapipe, with one set of S25 timings. It must not be skipped: this ADR is not accepted before its numbers are in.

- **The coverage technique is not decided.** Candidates, built on the same invariants:
  - the **flagged-resolve dither**: the spike's per-pixel, per-phase dither, with the flagged-pixel resolve exception (a wider box and a history age cap of about the jitter period, in a 3×3 or 5×5 neighbourhood, whose values are fitted);
  - **stable hashed alpha**: a threshold that is a hash of the texel, not of the pixel or the frame, so its grain sticks to the hair and the history reprojects it like any texture detail, with no resolve change;
  - the **plain cutoff**, kept if neither passes. This is an accepted outcome: the ADR then ships strand shading alone, and the resolve exception and the flag are not built.

  The spike also measures the fixed 4×4 ordered threshold on the partial texels only, as a control. The spike's dither under the current resolve is the baseline both must beat.
- **The numeric gates are not decided.** The cost budget above is fixed. The hair's temporal stability (the p99 temporal deviation of the hairline region at rest, against the cutoff's 2.5 levels and the spike's 12.3) and its trail (the p99 at frame 4 after a stop, against the cutoff's 2.0, and recovery to the method's floor by frame 8) are fitted from the spike, in the way ADR-0034 fitted its budgets, and written here before the ADR is accepted. Until then they are diagnostics.

## Consequences

- **Easier.** Hair reads as strands, with a tinted secondary highlight that follows the painted direction and no seam at the mirror. It fades with the body already, through the masked fade of ADR-0041. Standard draws keep today's code; a model is paid for in pipelines, not in every draw's registers.
- **Harder.** A seventh material texture slot, more fields in the material file, the bundle and `GpuMaterial`, six forward pipelines and more pre-pass pipelines (built only while `characterShading` is on), a bake step in the sample's content, and, if the spike picks it, a resolve exception on a channel that carries two meanings (the glass mark and the hair flag). Any later pass that reads the scene's alpha has to know both.
- **Formats.** This is one more bump of the material file and the bundle before the 1.0.0 freeze. The next-free-number rule for the bundle keeps it independent of ADR-0045.
- **Not fixed.** Cast hair shadows stay hard (the cutoff), there is no hair thickness or transmission, and the cap geometry of the humanoid stays. Hair cards are the content path for a later sample.
- **To do when accepted.** File the build cards (the bake and content, the model and its formats, coverage and the resolve, phone enablement), update `docs/rendering.md`, `docs/assets.md`, `docs/player.md` and the character sample's README and licences, and run `chmod a-w` on this file in the accepting commit.
- **Phones stay off** until measured; the reduced hair is the fallback, not the default.

## Alternatives considered

- **Hair cards authored along one UV axis:** the right long-term answer, but it needs new content, a sourced CC0 asset and modelling, outside this issue. Kept as the content path for a later sample.
- **A constant UV axis:** the spike's state, rejected: seam at the mirror and a faceted highlight.
- **A procedural field from a head-space pivot:** the deformed vertex buffer has no bind-pose position or bone-relative pivot, so it needs a new vertex attribute.
- **Reusing existing slot channels for the strand data (occlusion or normal):** no format change, but the slot's glTF meaning no longer holds and the inspector and the cook would special-case the model. The freeze is still ahead, so the honest slot wins.
- **One anisotropic lobe (`KHR_materials_anisotropy`'s shape):** one lobe and no tinted secondary, which is what makes hair read as hair. A single lobe stays as the phone fallback only.
- **Marschner:** far more ALU and assumes strands, as ADR-0041 decided; not reopened.
- **The spike's dither under today's resolve:** fails its own evidence (p99 12.3 against 2.5, about four frames of extra trail); it is the baseline to beat.
- **A global change of the clip box or the neighbourhood shape:** moves every scene and re-opens ADR-0034's fitted constants, which a new ADR would have to refit.
- **A second hair history:** a second history and resolve, which ADR-0034 declined for cost on mobile.
- **A blended fringe layer or alpha to coverage:** blended hair drops out of depth, motion and occlusion and needs sorting; the RHI has no multisampling, and adding it is a large unrelated change.
- **A separate hair switch now:** the likely first outcome on phones, but one switch is enough until the numbers say otherwise, and splitting it later is small.
- **A flag test inside the existing pre-pass entries:** fewer pipelines, but it adds code to every masked draw and breaks ADR-0041's "Standard compiles to today's code" gate in spirit.
- **Wider coverage scope (eyebrows, eyelashes, any masked material by a flag):** more to verify in one decision, and the spike left those undithered; the flag is designed so they can follow.
