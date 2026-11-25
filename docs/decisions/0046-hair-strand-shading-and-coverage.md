# ADR-0046: Hair strand shading and TAA-resolved coverage

- **Status:** Accepted. The spike and the phone measurements are in ([hair-strand-spike](../reports/hair-strand-spike.md), [skin-shading-s25](../reports/skin-shading-s25.md) and [skin-shading-ipad](../reports/skin-shading-ipad.md), for [#591](https://github.com/Pacheco95/sonnet/issues/591) and [#609](https://github.com/Pacheco95/sonnet/issues/609)), and "Settled by the spike" below records what they decided.
- **Amends:** [ADR-0041](0041-skin-shading.md)
- **Date:** 2026-11-19

## Context

[ADR-0041](0041-skin-shading.md) kept the humanoid's hair on the Standard model and the plain cutoff, and left strand shading and TAA-resolved coverage to a later ADR. [#591](https://github.com/Pacheco95/sonnet/issues/591) asks for it, from a spike with captures and timings. This ADR decided what the existing evidence made clear and left two things to the spike, the coverage technique and its numeric gates; "Settled by the spike" records the answer.

What the [skin and hair spike](../reports/skin-hair-spike.md#hair) and the code say:

- **Content.** The hair is a painted scalp cap, one submesh of `Body`, whose base colour is two mirrored, fan-shaped islands of painted strands. No constant U or V axis follows them: V is off by up to 45° at the fans' edges, with a sign flip at the mirror line, so the highlight is seamed and faceted. The painted strands do carry a usable direction: a structure tensor of the base colour's luminance has a median coherence of 0.55 to 0.61 over windows of 4 to 16 texels. A baked flow map was plausible when this was written; the spike rendered one ("Settled by the spike").
- **Shading.** Two shifted Kajiya-Kay lobes cost +0.006 ms for skin and hair together on the RTX 4090, with the head at about half of a 1080p frame.
- **Coverage.** The spike's dithered mask failed under today's resolve ([ADR-0024](0024-temporal-anti-aliasing-and-occlusion-culling.md), with the age-weighted blend since): the 3×3 mean ± 1.25 σ clip box of `taa.slang` is mostly skin where coverage is low and pulls the history back to it. The hairline's temporal p99 over one TAA cycle was 12.3 sRGB levels against 2.5 for the cutoff, and after the camera stops the dither is about four frames behind the cutoff. The spike's threshold already varied with the TAA phase, so a moving threshold alone does not fix it.
- **Resolve constants are fitted.** [ADR-0034](0034-taa-glass-gate.md) fitted the resolve's constants to four glass cases and says a change of any of them is a new ADR. The scene's alpha is 1 for opaque and masked output and 0 for glass, which the resolve reads as the glass mark (neighbours below 0.5), so values from 0.5 to 1 are free.
- **Phone cost.** On the Galaxy S25 Ultra, skin and hair together added about +0.47 ms p95 in the spike, of which skin alone is +0.12 to +0.13. That leaves about +0.33 to +0.35 ms p95 for hair, over a 0.3 ms budget (a subtraction of two measured figures; the median is unusable, the clock switched levels, and hair alone was never isolated on the phone). The iPad was not measured.
- **Two pre-pass facts.** The pre-pass cuts masked draws with `Sample` and the forward pass with `SampleBias` (the mip bias is -0.5 under TAA), so a stochastic test evaluated in both would leave clear-colour holes. The existing masked fade is a separate screen-door test on the tint's alpha ([ADR-0041](0041-skin-shading.md#fade-on-masked-draws)).

Strand shading and coverage are independent: either can fail its gate without blocking the other, and this ADR is written so that the strand model can be accepted with the plain cutoff.

## Decision

**Hair gets a `Hair` shading model: two-lobe Kajiya-Kay along a flow map baked from the painted texture and stored in a new `strand` texture slot, compiled into forward and pre-pass pipelines of its own. Hair keeps the plain alpha cutoff, and the TAA resolve does not change. Skin and hair share the one `RendererSettings::characterShading` switch.** The spike measured the stochastic coverage techniques and none passes the gates, so strand shading ships alone ("Settled by the spike"). This ADR adds no code by itself.

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

With the plain cutoff these hold by construction. They stay as the testable bar for a later decision on stochastic coverage, which has to meet them as well as the gates in "Settled by the spike":

- Standard draws render pixel for pixel as today, including `showcase_taa.png`.
- The pre-pass decides stochastic coverage alone, and the forward pass's equal depth test agrees with it: no clear-colour holes.
- The fade keeps its meaning. The hair's coverage noise is independent of the fade's ordered threshold, so a faded hair draw keeps coverage × tint of its pixels and not the minimum of the two.
- Shadows test the cutoff without the tint and without coverage noise, as masked draws do today, so cast hair shadows stay as hard as they are.
- With TAA off, or a debug view on, hair takes the plain cutoff, as the spike did.
- Coverage applies to hair only. The flag below is designed so that eyebrows, eyelashes and other masked materials can follow in a later decision.

### The resolve

- **Unchanged.** `taa.slang`, the constants ADR-0034 fitted and the scene's alpha stay as they are: the hair pipelines write alpha 1 like every opaque and masked draw, and the glass mark (alpha below 0.5) keeps its one meaning.
- **What was tried.** A flagged-pixel exception: the hair forward entry writes 0.75 in the scene's alpha for pixels whose coverage is stochastic, and `taa.slang` gives a pixel with a flagged neighbour a wider clip box and a longer history age cap, with every other pixel bit-identical to today. The spike built it, confined as described, and the ADR-0034 harness's four glass cases and `showcase_taa.png` came out unchanged with it live. It still does not pass the coverage gates below, so it is not built. A later decision can start from it.

### Platforms and gates

- **The switch.** One `RendererSettings::characterShading` covers skin and hair: on by default on the desktop and on Android (the S25 passes ADR-0041's gates, [#640](https://github.com/Pacheco95/sonnet/pull/640)), off in the iOS player until it is measured. Splitting it later is small: a second `bool`, with no file format change; the setting is read by the renderer, the capture options, the player and the tests.
- **Hair's budget is 0.3 ms.** On a phone, hair may add at most 0.3 ms of total GPU time to the median and to the p95, in cold and sustained windows, over skin alone, with the head at about half of a native-resolution frame. It is the enabling gate: an unmeasured or failing phone keeps hair off. Standard-material scenes regress by at most 0.1 ms, as in ADR-0041, and the Standard pipeline compiles to the same code as today.
- **A reduced phone hair is the pre-agreed fallback.** If the full hair misses the budget on a phone, that platform runs one lobe with the cutoff coverage, and is measured against the same gate. How the reduced form is selected (a lighter entry or a second setting) is left to the build card; a second setting is the switch split above.
- **Method.** ADR-0039's measurement method and the repository's temperature-based S25 cooldown rule. Each cold round reports its minimum and maximum beside the median, since the spike's median was bimodal. The iPad is measured separately. Desktop results do not validate a phone. On MoltenVK the timestamp samples are sparse ([#355](https://github.com/Pacheco95/sonnet/issues/355)), so a gate on the iPad's native size or on a Standard scene cannot be read from the engine's own timings; it stays unmeasured until a method that does not depend on them exists.

### Settled by the spike

The spike ([hair-strand-spike](../reports/hair-strand-spike.md), branch `spike/hair-flow-map`) measured the coverage candidates and a control on the in-repo harness over the hairline band, against the plain cutoff, on the RTX 4090, and the strand model on the Galaxy S25 Ultra ([skin-shading-s25](../reports/skin-shading-s25.md)) and the iPad ([skin-shading-ipad](../reports/skin-shading-ipad.md)).

- **Coverage: the plain cutoff.** Hair keeps `alphaCutoff`. None of the flagged-resolve dither, the stable hashed alpha and the fixed ordered threshold passes the gates below. The best of them, the flagged-resolve dither, has 2.9 and 3.0 times the cutoff's p99 temporal deviation at rest in the two views measured, and 3.2 times its p99 four frames after a stop in the first. The resolve exception and the flag in the scene's alpha are **not built**, `taa.slang` and the constants of ADR-0034 stay as they are, and the hair pipelines write alpha 1.
- **Strand direction: the baked flow map, as decided.** The shader's direction follows the painted strands to a mean |cos| of 0.86 on screen, against 0.66 for the opposite sign convention, and the highlight shows no seam in the lit image. Its bake uses a window of radius 6 texels, takes the combed field over below a coherence of 0.18 and is out above 0.45, and the combed field is a per-island crown fit; the bake tool in the content build card reproduces these. The strand term itself still has a discontinuity of about 30° in a band five columns wide on the mirror line; the build card isolates its cause (the vertex tangent frames are the likeliest).
- **Gates for a coverage technique** (hairline band: where a converged stochastic frame differs from a converged cutoff frame by more than 12 levels, grown by two pixels; 1280×720; the player's captures of the character sample's `spike_orbit_stop` views, measured with `tools/hair_taa_metrics.py` on the spike branch): the p99 of the per-pixel temporal deviation at rest, of the difference from the converged frame four frames after a stop, and of the same eight frames after a stop are each **at most twice the same measure of the Hair model with the cutoff** in the same view. The cutoff's own are 1.9 / 6.0 / 3.3 levels in the front view and 2.7 / 14.3 / 8.7 in the side view. The gate is a ratio, since a level gate moves with the view. A coverage technique has to pass it and the invariants above.
- **Hair's phone budget on the Galaxy S25 Ultra: met.** Hair adds at most 0.3 ms over skin alone: the forward pass, the only one the model changes, adds 0.00 to 0.09 ms over skin at 1920×1080 and at the native size, cold, and 0.06 ms in a 600 s run, with the head at about half of the frame (`capture_face`); hair alone adds 0.02 to 0.05 ms over `off`. The whole frame resolves no better than about ±0.4 ms on the phone, because its GPU clock changes level between runs, so the budget is read on the forward pass and on the plain frame difference.
- **Hair's phone budget on the iPad (A16): not shown.** Over skin at 1920×1080 in three 600 s runs the clock-corrected frame median adds +0.364, −0.254 and +0.115 ms, and the forward pass +0.384, −0.116 and +0.114 ms; the spread, 0.618 ms, is twice the budget, so the method cannot tell a pass from a fail, and all three corrected p95 differences are within 0.3 ms. At the native size the samples are too sparse to say anything. The iPad keeps hair off, and the reduced one-lobe hair of the fallback above is unmeasured there.

## Consequences

- **Easier.** Hair reads as strands, with a tinted secondary highlight that follows the painted direction and no seam at the mirror. It fades with the body already, through the masked fade of ADR-0041. Standard draws keep today's code; a model is paid for in pipelines, not in every draw's registers.
- **Harder.** A seventh material texture slot, more fields in the material file, the bundle and `GpuMaterial`, six forward pipelines and more pre-pass pipelines (built only while `characterShading` is on), and a bake step in the sample's content. The scene's alpha keeps its one meaning, since no hair flag is built.
- **Formats.** This is one more bump of the material file and the bundle before the 1.0.0 freeze. The next-free-number rule for the bundle keeps it independent of ADR-0045.
- **Not fixed.** Cast hair shadows stay hard (the cutoff), there is no hair thickness or transmission, and the cap geometry of the humanoid stays. Hair cards are the content path for a later sample.
- **To do when accepted.** File the build cards (the bake and content, the model and its formats, phone enablement), none for coverage or the resolve; keep the spike's code out of them, since it takes shortcuts (the flow map in the emissive slot, parameters fixed in the shader, a per-material branch in the shared pre-pass) the cards replace; do not use floating-point `std::from_chars` where the iOS deployment target is 16.3, which the spike's parser failed on; update `docs/rendering.md`, `docs/assets.md`, `docs/player.md` and the character sample's README and licences, and run `chmod a-w` on this file in the accepting commit.
- **The iPad stays off** until measured; the reduced hair is the fallback, not the default. Android follows the one switch, so skin and hair come on together there, on the S25's numbers.

## Alternatives considered

- **Hair cards authored along one UV axis:** the right long-term answer, but it needs new content, a sourced CC0 asset and modelling, outside this issue. Kept as the content path for a later sample.
- **A constant UV axis:** the spike's state, rejected: seam at the mirror and a faceted highlight.
- **A procedural field from a head-space pivot:** the deformed vertex buffer has no bind-pose position or bone-relative pivot, so it needs a new vertex attribute.
- **Reusing existing slot channels for the strand data (occlusion or normal):** no format change, but the slot's glTF meaning no longer holds and the inspector and the cook would special-case the model. The freeze is still ahead, so the honest slot wins.
- **One anisotropic lobe (`KHR_materials_anisotropy`'s shape):** one lobe and no tinted secondary, which is what makes hair read as hair. A single lobe stays as the phone fallback only.
- **Marschner:** far more ALU and assumes strands, as ADR-0041 decided; not reopened.
- **The spike's dither under today's resolve:** fails its own evidence (p99 12.3 against 2.5, about four frames of extra trail); it was the baseline to beat, and the flagged-resolve dither, the stable hashed alpha and the fixed ordered threshold all measured worse than the cutoff at rest.
- **The flagged-resolve exception and its alpha flag:** built and confined in the spike, with the glass cases and the golden frame unchanged, but the best coverage it allowed misses the gates, so no channel is spent on it.
- **A global change of the clip box or the neighbourhood shape:** moves every scene and re-opens ADR-0034's fitted constants, which a new ADR would have to refit.
- **A second hair history:** a second history and resolve, which ADR-0034 declined for cost on mobile.
- **A blended fringe layer or alpha to coverage:** blended hair drops out of depth, motion and occlusion and needs sorting; the RHI has no multisampling, and adding it is a large unrelated change.
- **A separate hair switch now:** the likely first outcome on phones, but one switch is enough until the numbers say otherwise, and splitting it later is small.
- **A flag test inside the existing pre-pass entries:** fewer pipelines, but it adds code to every masked draw and breaks ADR-0041's "Standard compiles to today's code" gate in spirit.
- **Wider coverage scope (eyebrows, eyelashes, any masked material by a flag):** more to verify in one decision, and the spike left those undithered; the flag is designed so they can follow.
