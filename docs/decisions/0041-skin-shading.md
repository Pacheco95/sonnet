# ADR-0041: Pre-integrated skin shading, and masked draws that fade

- **Status:** Accepted
- **Amends:** [ADR-0008](0008-clustered-forward-rendering.md)
- **Date:** 2026-11-17

## Context

[#502](https://github.com/Pacheco95/sonnet/issues/502) settles how the character's skin and hair shade before the build cards [#542](https://github.com/Pacheco95/sonnet/issues/542), [#543](https://github.com/Pacheco95/sonnet/issues/543) and [#544](https://github.com/Pacheco95/sonnet/issues/544). In the [humanoid's captures](../reports/character-humanoid.md) the skin reads as matte paint with a hard terminator, and the hair's masked fringe is cut hard.

What the renderer does today:

- **One forward shader.** `forward.slang` is a single metallic-roughness shader: GGX, Smith and Schlick with Lambert diffuse, then split-sum image-based lighting (IBL).
- **No G-buffer.** [ADR-0039](0039-screen-space-ambient-occlusion-and-contact-shadows.md) declined to add one, on bandwidth grounds. The [transmission](0033-transmission.md) and ADR-0039 phone measurements show that extra attachments, full-screen passes and tile stores are what costs on phones.
- **Formats.** `GpuMaterial` is 240 bytes with one reserved word, and its flag bits 8–10 are taken. `MaterialFileVersion` is 6 and `BundleVersion` is 20.
- **Material overrides.** `MeshRenderer::material` overrides every slot of a mesh, and a glTF material can be edited in memory only. A submesh of the humanoid therefore cannot get a material of its own today.
- **The character fade.** The fade of [#529](https://github.com/Pacheco95/sonnet/issues/529) screen-doors `AlphaOpaque` draws only. A masked draw multiplies the tint's alpha into its cutoff, so the hair, eyebrows and eyelashes do not fade with the body.

A forward-shader spike measured the agreed design on the humanoid. It ran on the RTX 4090 and on Lavapipe, then for skin's cost on the Galaxy S25 Ultra; the iPad was not measured ([report](../reports/skin-hair-spike.md)). Five results changed the design:

1. **Skin works within the forward pass.** Pre-integration red-shifts the falloff (red over green 1.59 to 1.75 at the cheek's quarter-lit level) and softens the normal map's detail on the diffuse.
   - Derivative curvature is faceted per triangle, but at a 1 mm scatter radius the facets do not show.
   - Over one TAA cycle the face's temporal deviation rises only on silhouette edges (p99 1.9 to 3.5 sRGB levels).
   - Skin and hair together added 0.006 ms on the RTX 4090 with the head at about half a 1080p frame.
2. **The per-draw branch costs standard draws.** With no skin or hair in the scene, the branch added 12 % to the showcase's forward pass and 8 % to the stress scene's (0.040 and 0.022 ms). Compiling the branch away recovered all of it; the 256-byte material and larger frame constants cost nothing measurable. On the S25, that fraction of forward would exceed the 0.1 ms standard-scene gate.
3. **Kajiya-Kay hair depends on content this sample does not have.** The hair is a painted scalp cap whose texture is two mirrored, fan-shaped UV islands.
   - No constant U or V axis follows its strands. V is closest, but off by up to 45° at the fans' edges, with a sign flip at the mirror line.
   - The narrow lobe lights flat patches of the cap evenly up to their triangle edges, and reads as bleached patches at body distance.
4. **A TAA-dithered mask does not converge under the current resolve.** Its 3×3 mean ± 1.25 σ clip box pulls low-coverage history back to the background.
   - The hairline's temporal p99 is 12.3 sRGB levels, against 2.5 for the cutoff.
   - After motion stops, trails take about four frames longer to clear than the cutoff's.
5. **The fade does not need dithered alpha.** Dithered hair faded with the body only because its alpha included the tint's. The cutoff eyebrows and eyelashes still did not fade.

## Decision

**Characters get one new shading model, `Skin`: pre-integrated subsurface scattering in the forward pass, compiled into a forward pipeline of its own. Hair keeps the standard model and the plain cutoff for now. A faded draw screen-doors its tint on masked materials as it already does on opaque ones.** These are contracts for #542, #543 and #544. This ADR adds no code by itself.

### Representation

- **The shading model.** `renderer::ShadingModel` is `Standard` or `Skin`. It is `MaterialDesc::shading` and `assets::MaterialSource::shading`, with the fields:
  - `subsurface`: a weight from 0 to 1, default 1. At 0 a Skin material shades as Standard.
  - `subsurfaceRadius`: metres per millimetre of the skin profile, default 0.001 (human skin), clamped to 0–0.01.
- **Storage.** The fields live in `.material.json` files only, at the next `MaterialFileVersion` (7 today). A file of an older version loads as `Standard`. They are cooked into the bundle at the next `BundleVersion` (21 today). Recheck both numbers when the work lands.
- **glTF.** glTF materials are not extended and the importer never sets `Skin`: no ratified Khronos extension describes skin.
- **Per-submesh slots.** A glTF submesh gets a skin material through a slot (#542):
  - `MeshRenderer::slotMaterials` is a list indexed by `Submesh::materialSlot`, reflected and registered with `registerVector`.
  - In `appendDraws` the slot's entry wins, then `material` (every slot, as today), then the mesh's own slot material. A nil entry falls through.
  - An index past the mesh's slot count is ignored, with one warning.
  - The list is empty by default and omitted when empty, so an existing scene loads unchanged. Whether the scene version moves follows ADR-0029's rules for an additive field.
  - A prefab instance overrides one slot as a per-field override ([ADR-0037](0037-per-field-instance-overrides.md)).
  - The `MeshRenderer` inspector lists one row per slot, with the glTF material's name and an asset picker. "Make editable copy" writes that glTF material into a `.material.json` and assigns it, with undo.
- **GPU record.** `GpuMaterial` grows to 256 bytes with one 16-byte row: `subsurface`, the radius, and two words reserved for a later model. The model goes in flag bits 11–12.

### Skin

- **LUT.** A 128×128 `R16G16B16A16Sfloat` lookup table, built once at startup by a `skinLut` compute entry in `ibl.slang`, imported into the graph like `brdfLut`.
  - x is n·l from −1 to 1. y is (1 profile mm) / (surface radius in profile mm), from 0 to 0.5.
  - Each texel integrates the clamped cosine around a ring of that radius, weighted per channel by d'Eon and Luebke's six-Gaussian skin profile.
  - The ring is integrated over the arc where the widest Gaussian still weighs (three deviations), so narrow Gaussians are not undersampled.
- **Curvature.** `length(fwidth(N)) / length(fwidth(P))` of the interpolated vertex normal and world position. The LUT row is `saturate(curvature × subsurfaceRadius × 2)`. No curvature texture, no new texture slot.
- **Normals and direct light.**
  - Each colour channel's diffuse normal is `normalize(lerp(mapped, smooth, subsurface × w))`, with w = 0.9, 0.6 and 0.3 for red, green and blue.
  - Each light's diffuse cosine is the LUT at that channel's own n·l: three samples per light, sun and clustered lights alike. It is blended with the clamped cosine by `subsurface`.
  - The specular is the standard GGX on the mapped normal.
- **IBL.** Diffuse irradiance is sampled per channel at that channel's normal. Specular IBL, material occlusion and ADR-0039's screen-space occlusion apply exactly as for Standard.
- **Shadows.** Sun and local visibility multiply the skin's direct light as they do for Standard. Pre-integration does not soften a cast shadow's edge; a penumbra LUT is not part of this decision.
- **Debug view.** A `Curvature` shading term shows a Skin draw's LUT row in red and black elsewhere.

### Shader strategy

- **No per-draw branch.** `forward.slang` gains a second fragment entry point for `Skin`. Both share one generic body with the model as a compile-time parameter, and `Standard` keeps today's code. Spec constants are not used, because the RHI has none and this needs none.
- **Pipelines.** The forward pipelines double: model × double-sided. The model joins the opaque batch key, so a Skin draw never shares a batch with a Standard one.
- **Unchanged passes.** The depth pre-pass, the shadow passes and the transmission and blended paths are unchanged. Skin is opaque or masked, and a blended or transmissive Skin material shades as Standard.

### Fade on masked draws

- **The rule.** For an `AlphaMask` draw, the cutoff tests the material's, texture's and vertices' alpha without the tint's. The tint's alpha (`MeshRenderer::color.a`) is screen-doored by the same ordered threshold opaque draws use, in the pre-pass and the forward pass.
- **Shadows** test the cutoff without the tint, so a faded masked draw still casts its whole shadow, as a faded opaque one does.
- **Blended draws** keep the tint's alpha in their blend.
- **Behaviour change.** A masked material drawn with a tint alpha below 1 now dithers instead of moving its cutoff. #544 records this in `docs/rendering.md`.

### Platforms and gates

- **The switch.** `RendererSettings::characterShading`, on by default on the desktop. The Android and iOS players set it off. Off, a `Skin` material is drawn by the `Standard` pipeline, so one bundle runs everywhere.
- **These are acceptance budgets, not measurements.**
  - On the Galaxy S25 Ultra (Adreno 830, ADR-0019's 1.3 extension path), with the head at about half of a native-resolution frame, skin may add at most **0.5 ms** of total GPU time, median and p95, in cold and sustained windows.
  - The sustained p95 frame interval stays under **33 ms**, and the thermal status and time to throttling are no worse than with it off.
  - Standard-material scenes (the showcase and the stress scene) regress by at most **0.1 ms**. The Standard pipeline must compile to the same code as today's shader, and the Lavapipe goldens must not change.
- **Method.** Follow ADR-0039's measurement method and the repository's temperature-based S25 cooldown rule.
- **Phones.** The iPad is measured separately before iOS changes its default. An unmeasured or failing platform keeps `characterShading` off. Desktop results do not validate a phone.
- **The spike's S25 result.** With the head at about half a 1920×1080 frame, skin added 0.126 ms to the median and 0.129 ms to the p95 GPU frame over three cold pairs. Over a sustained 600 s pair it added 0.146 / 0.134 ms, with a p95 interval of 17.4 ms and thermal status 0 throughout, never throttling, as with it off ([report](../reports/skin-hair-spike.md#galaxy-s25-ultra)).
  - This was measured on the spike's per-draw branch, which both halves of each pair ran, so it isolates skin's own cost.
  - It does not enable Android. #543 measures the production pipelines against the same gates, including the standard-scene gate, before the Android player changes its default.

### Hair

Hair keeps `Standard` shading and the plain cutoff. Strand shading and TAA-resolved coverage need their own ADR. The spike's evidence for it is in the [report](../reports/skin-hair-spike.md#hair):

- **Strand direction and shift.** Two shifted Kajiya-Kay lobes cost little, but need a per-texel strand direction (a flow map) and a shift texture, or strand cards authored along one UV axis. A constant U/V axis is not enough.
- **Dithered coverage.** It needs a TAA resolve that does not clip a dithered pixel's history to the current neighbourhood, or a different coverage technique. Under today's resolve it shimmers and lags.

Excluded, as agreed in #502: thickness-based ear transmission, wet eyes and cornea, Marschner strands, and `KHR_materials_volume`, `KHR_materials_diffuse_transmission` and `KHR_materials_anisotropy`, which keep today's policy (warn if used, refuse if required).

### Implementation split and acceptance

- **#542: per-submesh slots.** The rules above, scene and prefab round-trips, the inspector rows, "Make editable copy" and undo.
  - Tests: slot order; nil falls through; omitted when empty; an old scene loads unchanged; a prefab instance overrides one slot; an out-of-range index warns once; source and cooked loads agree.
- **#543: Skin.** The material fields, file and bundle versions, `GpuMaterial`, the LUT pass, the pipelines, the setting, the inspector's shading combo and fields, the sample's `skin.material.json` assigned to the humanoid's Skin slot, and the RTX 4090 and S25 reports.
  - Unit tests: a v6 material loads as Standard; the fields round-trip through the file, cook and bundle; `GpuMaterial` is 256 bytes and 16-byte aligned.
  - Lavapipe GPU tests:
    - a Skin sphere lit at 90° has a red-shifted, wider terminator than a Standard one, within a pinned numeric band;
    - a Standard sphere and the showcase golden are pixel-identical to today;
    - `characterShading` off draws Skin as Standard;
    - the LUT's flat row equals the clamped cosine.
- **#544: rescoped from hair to the masked fade.** The rule above, in the pre-pass, the forward pass and the shadows.
  - Lavapipe tests: a faded masked quad screen-doors with no holes under the equal depth test; the cutoff ignores the tint; its shadow is unchanged.
  - The hair, eyebrows and eyelashes fade with the body on the course.
- **A new issue** carries hair shading and TAA-resolved coverage to the ADR that decides them.

## Consequences

- **Skin.** It gains a soft, red falloff without a pass, an attachment or a texture slot. Its cost is three LUT samples per light, three irradiance samples, and a pipeline and batch split for its draws.
- **Formats.** The material, bundle and `GpuMaterial` changes, and `MeshRenderer`'s slots, settle before the 1.0.0 format freeze.
  - Slots give every material field one override path, through the existing inspector, files and undo, instead of a second one through glTF `extras`.
  - A future model can use the reserved words of the shading row and the free values of bits 11–12.
- **Standard draws** keep today's shader. A second model is paid for in pipelines, not in every draw's registers.
- **Masked fade.** Masked materials fade with their draw, at the price of a changed meaning for a masked material's tint alpha.
- **What skin does not fix.** Cast-shadow edges stay hard on skin, and curvature is coarse on low-polygon faces, because it comes from screen derivatives of interpolated normals.
- **Hair** stays as today until content with strand directions and a coverage technique that suits the TAA resolve exist.

## Alternatives considered

- **Screen-space subsurface scattering** (separable blur or Burley diffusion): needs diffuse split from specular in an extra attachment and full-resolution blur passes, the bandwidth and tile traffic ADR-0033 and ADR-0039 avoid on phones.
- **A curvature texture or baked thickness:** more stable than derivatives, but adds a texture slot and an authoring step for a gain the spike did not need at a 1 mm radius.
- **A per-draw branch in the forward shader:** the agreed starting point. The spike measured an 8–12 % forward regression on scenes with no skin.
- **Specialisation constants:** equivalent pipelines, but the RHI has no support for them; a second entry point is the same result without new RHI surface.
- **Kajiya-Kay/Scheuermann hair with a constant U/V axis:** cheap, but seamed and faceted on this sample's cap. Deferred (see Hair).
- **Marschner hair or anisotropic GGX:** Marschner costs far more ALU and assumes strands. `KHR_materials_anisotropy` has one lobe and no tinted secondary.
- **TAA-dithered alpha for masked hair:** softens the fringe and fades with the tint, but shimmers and lags under the current resolve. The fade alone is the screen-door rule above.
- **Blended hair:** drops out of the pre-pass, so it gets no depth, motion, AO or contact shadows. It needs per-draw sorting and overlaps itself in index order (the ADR-0033 limits).
- **glTF `extras` and a model-settings block:** a second override path just for these fields, outside the material inspector, its files and undo.
