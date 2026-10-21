# ADR-0033: Transmissive materials sample a copy of the opaque scene

- **Status:** Accepted, TAA gate superseded by [ADR-0034](0034-taa-glass-gate.md)
- **Date:** 2026-10-18

## Context

[#252](https://github.com/Pacheco95/sonnet/issues/252) imports a `KHR_materials_transmission` material as alpha-blended, its base colour's alpha multiplied by `1 - transmissionFactor` (`GltfImporter.cpp`). [#254](https://github.com/Pacheco95/sonnet/issues/254) asks for the effect the glTF specification describes for a surface without `KHR_materials_volume`: zero thickness, no bending, and in place of the diffuse lobe, the scene behind the pixel blurred by roughness and multiplied by the base colour, with the specular reflection on top.
*What blending can and cannot do.* A blend sees only the one pixel behind a fragment. Premultiplied blending can keep the highlight at full strength and dim what is behind, which is all clear, untinted glass needs. A colour tint needs a per-channel factor on the destination, which is dual-source blending, and the RHI has none. A blur by roughness needs the neighbouring pixels, which no blend sees. #252's blend also scales the highlight by the alpha it lowers, so clear glass loses its reflection.

*What the Khronos sample models ask for.* Of the 150 models in [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) (commit `edc7c9e67c63`), 33 use `KHR_materials_transmission`; most of those also use `KHR_materials_volume` (thick, refracting glass). The thin ones:

- *TransmissionTest*, Khronos's test grid: red, yellow, blue and green glass, each clear (roughness 0), rough (roughness by texture), and metallic, some masked, half with a `transmissionTexture`.
- *CommercialRefrigerator*: a frosted door, roughness 0.75 by texture, with transmission in `extensionsRequired`.
- *CompareTransmission*, *GlassBrokenWindow*, *FlightHelmet*'s lenses, *LightsPunctualLamp*'s shade: rough by texture, some tinted by a colour texture.
- *ToyCar*: green glass, roughness 0.
- *ChronographWatch*, where #254 started: clear, white, roughness 0, the case blending already covers.

Six of the 33, TransmissionTest among them, use `transmissionTexture`.

*The reference renderer.* [glTF-Sample-Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer) (commit `74aaf40853dd`) draws the opaque and the ordinary blended surfaces, not the transmissive ones, into a fixed 1024×1024 buffer, mips it with `generateMipmap` and samples it at `log2(1024) * roughness` (`renderer.js`, `ibl.glsl`). In `pbr.frag` transmission replaces the whole diffuse term, image-based included, before the Fresnel mix with the specular, and a separate `getPunctualRadianceTransmission` term lets a punctual light behind the surface shine through it.

What the renderer has today:

- *One forward pass* ([rendering.md](../rendering.md), step 7) into a transient `R16G16B16A16Sfloat`: opaque draws with an equal depth test, the skybox, the blended draws sorted back to front without depth writes, then the particles. The scene behind a surface cannot be read while that pass is drawing into it.
- *Shading* (`forward.slang`): `shade` returns direct diffuse, `kd * albedo / Pi` with `kd = (1 - F) * (1 - metallic)`, and specular together per light; image-based lighting adds `iblDiffuse`, with its own `kd` from `fresnelSchlickRoughness`, and `iblSpecular`, both times occlusion; without an environment a flat ambient term times albedo and occlusion. Roughness is clamped to at least 0.04 for the specular terms.
- *The depth pre-pass* (step 5) draws opaque and masked draws, writes motion under TAA, and builds the depth pyramid occlusion culling tests against ([ADR-0024](0024-temporal-anti-aliasing-and-occlusion-culling.md)). Anything it draws hides what is behind it from the forward pass and from culling. The resolve (`taa.slang`) uses one motion per pixel, taken at the closest depth in a 3×3 neighbourhood.
- *Shadow casters* (`depth.slang`): a masked material discards under its cutoff; a blended one discards through a hashed threshold on its alpha (base colour factor, texture and vertex colour).
- *Blended draws* are recorded on the CPU and sorted on the CPU ([ADR-0012](0012-gpu-driven-rendering.md)); `resolveDraws` splits each view's draws into `opaqueOrder` and `blendedOrder` by `AlphaMode::Blend`.
- *Mip chains in compute.* The RHI writes one mip level of an image through `storageImageIndex(image, level)`, and the environment's "cube mip N" passes build a chain one level per graph pass, each reading the level above through the sampled view. `ImageBarrier` covers a whole image, which is why each level is its own pass. There is no blit in `ICommandList`, and no `R11G11B10` format in `rhi::Format`.
- *Transient images* come from the render graph's pool, which reuses an image of the same description and releases one no frame has used for `PoolRetentionFrames`.
- *Two views per frame* ([ADR-0021](0021-two-views-in-one-frame.md)), each with its own transient images.
- *The material record.* `MaterialSource` has no transmission field (`MaterialFileVersion` 5). `GpuMaterial` is 208 bytes with five texture slots: their bindless sampler indices are packed six bits each into one `sampler` word (30 of 32 bits), each has a bit in `uvSets` and a 2×2 matrix and offset for `KHR_texture_transform`, and the record ends in an unused `uvPadding`. Bundles are at version 18.
- *Colour space.* The importer marks an image sRGB, for every material that uses it, when any material uses it as base colour or emissive; others are linear. An image shared between a colour slot and a data slot is therefore read as sRGB by both, as it already is for the metallic-roughness, normal and occlusion slots.
- *Extensions.* The importer reads a fixed list; one only in `extensionsUsed` imports with a warning, one in `extensionsRequired` that it does not read refuses the file (`GltfTests.cpp`).
- *Mobile.* The S25 (Adreno 830) is constrained by heat and the phone player runs FIFO at 60 fps. The stress scene's 31.85 ms sustained GPU time was measured with a 2048 far shadow map; the later 512 map with a coarse shadow range measured 19.05 ms at 734 MHz ([far-cascade-mechanism](../reports/far-cascade-mechanism.md)). That later run used a shadow simplification error margin of 1.5, subsequently raised to 1.75 without repeating the measurement. These are results for those configurations; the transmission measurements establish a fresh baseline on the current build. On a tiler, ending a pass and starting another stores and reloads its attachments through memory.

The [1.0.0](../roadmap.md#100) rule wants the material and bundle formats settled first, and this adds to both.

## Decision

**A material has a scalar `transmission` and a `transmissionTexture`. When a view has a transmissive draw, the forward pass splits after the skybox, the opaque scene is copied with a mip chain, and transmissive and blended draws then draw together, sampling the copy at a mip chosen by roughness. A view without one renders as today. `RendererSettings::transmission` turns it off, and transmissive materials then draw blended with an opacity from the same transmission, so one bundle runs everywhere.**

### Material and format

- *Fields.* `transmission` (0 to 1, default 0) on `MaterialSource`, `MaterialDesc` and `Material`, and a sixth texture slot, `transmissionTexture`, with its `transmissionTransform` and `transmissionSampler` like the other five. Material file version 6, and the bundle version goes up by one. A version 5 file has neither field and loads with 0 and no texture, so nothing is migrated.
- *Definitions,* used by the shading, the shadows and the fallback alike:
  - `t = transmission * transmissionTexture.r`, 0 for an unlit material (`KHR_materials_unlit` ignores other extensions);
  - `m = saturate(metallic * metallicRoughnessTexture.b)`, as the shader computes it today;
  - `T = t * (1 - m)`, the *effective* transmission: metal does not transmit;
  - coverage `a`: 1 for `Opaque`; for `Mask`, 1 for a fragment that passes its cutoff, which runs first and discards the rest; for `Blend`, the authored alpha (factor, texture and vertex colour).
- *Classification.* A draw takes the transmission path when its material has `transmission > 0`, is lit, and can have `T > 0`. The CPU can prove `T = 0` for a metallic factor of 1 with no metallic-roughness texture, whose neutral default is white. Such a material, a material with transmission 0, and an unlit material keep their ordinary `AlphaMode` path. This preserves opaque and masked metals' depth, motion and presence in the copy. Texture-dependent materials are classified conservatively: the CPU does not scan textures, and a draw that might transmit stays on the transmission path even where individual texels have `T = 0`. Transmission remains orthogonal to alpha.
- *GPU record.* `GpuMaterial` grows from 208 to 240 bytes: after `uvSets`, a 16-byte row of `transmission`, `transmissionTexture`, `transmissionSampler` (the sixth sampler index, which does not fit in `sampler`) and one reserved word; a sixth matrix, `transmissionUv`, after the five; and a sixth offset, `transmissionUvOffset`, where `uvPadding` was. `SlotTransmission` is 5 in `uvSets`. Every `float4` stays 16-byte aligned for Metal. The texture is sampled only by transmissive fragments.
- *Import.* The glTF importer stores `transmissionFactor` and `transmissionTexture` (with its `texCoord`, sampler and `KHR_texture_transform`) and stops changing the alpha. The transmission image is linear unless it is shared with a colour slot, the restriction every data slot already has; the importer warns once per image shared between a colour slot and any data slot, naming both, and `docs/assets.md` states the rule.
- *Not in this version.* `ior`, volume, and a punctual light's transmission lobe. `KHR_materials_volume` and `KHR_materials_ior` in `extensionsUsed` only import with the importer's warning and draw as thin glass; in `extensionsRequired` they refuse the file, as every unread required extension does (PotOfCoalsAnimationPointer among the samples). This ADR does not change that policy. A later ADR adds them with a version, as this one does.

### Draw lists and passes

- *Draw lists.* `resolveDraws` applies the classification above and puts a draw that can transmit in `blendedOrder` with the blended ones, sorted with them back to front, and never in the opaque batches. So it is not in the depth pre-pass, writes no depth and no motion, does not occlude in culling, and is not in the copy. A draw whose effective transmission is provably zero keeps its ordinary opaque, masked or blended path, with transmission on or off.
- *Passes, only in a view with a transmissive draw.*
  1. `forward` draws the opaque draws and the skybox and stores its colour and depth.
  2. `transmission copy`, a compute pass, reads the forward target and writes level 0 of a transient `R16G16B16A16Sfloat` image of the view's size with a full mip chain; then one `transmission mip N` pass per level, each building level N from N-1 with the bloom chain's 13-tap filter (`bloomDownsample`) to reduce shimmer at moving edges. It is the "cube mip N" pattern: one pass per level, each declaring `storage` on the image.
  3. `forward transparent` loads colour and depth and draws `blendedOrder`, transmissive and blended together, then the particles.
  A view without a transmissive draw, or with transmission off, keeps the one `forward` pass and declares none of these: no image, no pass, no store and reload.

### Shading

- *Roughness for transmission.* The transmission lookup reads the perceptual roughness unclamped, `r = saturate(material.roughness * metallicRoughnessTexture.g)`; the 0.04 floor stays on the specular terms only. So roughness 0 reads level 0 exactly.
- *Lookup.* At the fragment's own screen position (zero thickness: no offset), with linear mip filtering and clamp-to-edge addressing. The LOD uses the actual mip extents rather than assuming that level `i` spans exactly `exp2(i)` base pixels:
  - `size = max(width, height)`, `q = (exp2(10 * r) - 1) / 1023`, and the desired nominal footprint in base pixels is `p = 1 + (size - 1) * q`;
  - for each level `i`, let `extent[i]` be the larger dimension of `rhi::mipSize(viewSize, i)` and `p[i] = float(size) / extent[i]`;
  - for `size = 1` or `r = 0`, use level 0; for `r = 1`, use the final level; otherwise find adjacent levels with `p[i] <= p <= p[i + 1]` and use `lod = i + log2(p / p[i]) / log2(p[i + 1] / p[i])`.
  This preserves exact level-zero sampling for clear glass, reaches the 1×1 level only at roughness 1, and gives `lod = 10 * r` at size 1024. At other sizes it calibrates the nominal excess footprint to the longest dimension. It is an approximation to the filtered image's blur, which the resize regressions check; it does not promise identical pixels across sizes. A rectangular view's shorter dimension has a larger fractional blur, so aspect changes do not preserve the same blur fraction in both axes or match the square reference buffer.
- *Combination, with transmission enabled.* With `S` the copy's sample, `albedo` the base colour as today (factor, texture, vertex colour), and `F` the view Fresnel the image-based term uses (`fresnelSchlickRoughness(nDotV, f0, roughness)`, the same with no environment):
  - direct diffuse from each light becomes `(1 - t) * kd * albedo / Pi`, and `iblDiffuse` becomes `(1 - t)` times itself; the flat ambient term becomes `(1 - T)` times itself, since it has no `kd`;
  - the transmitted term `t * (1 - F) * (1 - m) * albedo * S` is added once;
  - the direct and image-based specular terms and emissive are unchanged;
  - occlusion multiplies the image-based terms as today and not the transmitted term, whose light the copy already holds shaded.
  This is the reference's order (transmission replaces the diffuse before the Fresnel mix), so reflection and transmission share the energy `F` splits and nothing is counted twice. The fragment's output alpha is `a`.

### Limits, stated in `docs/rendering.md`

- The copy holds only opaque surfaces and the sky. Through glass one sees no blended surface, no particle and no other draw on the transmission path, including the opaque texels of a mixed-transmission draw. A particle in front of or behind glass draws over it. The reference renderer also leaves transmissive surfaces out of its buffer but includes blended ones; leaving them out here keeps one copy per view after the opaque draws, and is a Sonnet trade-off, not the reference's.
- A draw on the transmission path writes no depth, so a non-convex or self-overlapping mesh draws its own triangles in index order, not by depth, as a blended mesh does today. That includes draws classified conservatively, such as a metal whose metallic-roughness texture decides `T`.
- The blur differs from the reference's in profile: a 13-tap chain, not `generateMipmap`'s box. Comparisons with the reference are of the features (tint, blur growing with roughness, masks, metal), not of the exact blur.
- The blur is of the screen, not of what is behind the surface: at a high roughness an opaque silhouette in front of the glass bleeds into the neighbouring glass pixels.
- A punctual light behind the glass lights the surfaces the copy holds, but the light itself does not glint through rough glass: that is the deferred punctual transmission lobe.

### Shadows

A transmissive caster's coverage is `a * (1 - T)` through the hashed test of ADR-0024, run after a masked material's cutoff as today. `T` holds the texture and the metallic exclusion, so TransmissionTest's metallic row casts a full shadow and a masked pane's cut-out casts none. A clear dielectric pane (`T = 1`) casts no shadow, as in the reference renderer, which has no shadows of transmitted light. The rule is the same with transmission on or off, so switching it does not change shadows.

### TAA

The copy is the jittered, unresolved opaque frame, and transmissive draws write no motion, as blended draws do. The pixel's motion is then that of the closest surface behind it. That is right for what is seen through clear glass, and an approximation for everything else the pixel holds: the glass's highlight, its tint and transmission textures, its residual diffuse, its edges, and a rough sample that mixes surfaces of different depths and motions. No single motion is right for both layers, so a motion written by the glass would trade one error for another, not remove it.

Before #254 closes, these cases are rendered on the RTX 4090: ChronographWatch with its second hand moving behind still glass; TransmissionTest turning on a turntable; a rough pane still in front of a box moving across a depth edge; and a textured, masked pane translating over a stationary pattern, then stopping. For each frame, the reference holds the simulation and camera pose fixed, renders 16 spatially jittered samples without temporal accumulation, averages them in linear HDR, and applies the same tone mapping. The test harness supplies the reference jitter and the same material-sampling settings as the TAA run; disabling TAA in the normal renderer alone supplies neither the jitter nor its material mip bias.

The evaluation region includes the current glass coverage, its coverage in the preceding four frames, and the regions swept between those poses, dilated by two pixels for the reconstruction filters. It includes pixels the glass has left, so a ghost trail cannot move into an excluded background region. Per-pixel error is the largest absolute RGB-channel difference in the display-encoded output, expressed in levels out of 255. After 32 warm-up frames, every measured frame must have mean error at most 2 levels and a 99th-percentile error at most 8 levels in that region. In the translating-pane case, every vacated pixel must return to within 8 levels of the reference within four frames. Opaque-region error is reported as a diagnostic, not used as a denominator. These are acceptance budgets, not measured results. The results are written in a report linked from #254. If a case fails, its captures and measures go in that report and a mitigation is chosen for that failure (for instance a stronger neighbourhood clamp on transmissive pixels, or no history for them), then the checks are repeated before #254 closes.

### Off and mobile

- *Off.* `RendererSettings::transmission` (default true). Off, a draw on the transmission path uses the ordinary material lighting calculation: direct diffuse, diffuse IBL and flat ambient have their original weights, and there is no transmitted term. Its output alpha is `a * (1 - T)` in the blended pipeline; `T` still controls that alpha and the shadows, which are as on. The copy and mip passes are not declared. Thus a dielectric with `a = 1` and `t = 0.5` contributes half its ordinary diffuse after blending, not a quarter from reducing diffuse twice. A draw with provably zero effective transmission keeps its ordinary path. This is #252's approximation with corrected coverage, the metallic exclusion and the transmission texture. Its limits stay: no tint of the background, no blur, and the highlight dims with the alpha.
- *Phone player.* Measured on the S25 and the iPad at their native resolution with TransmissionTest and CommercialRefrigerator, both cold (3 s) and sustained (the stress-scene recipe: `--play 600` or longer, until clocks and thermal state settle), each with transmission on and off. Record the current build, renderer settings, initial and final thermal status, and GPU clock where available; each comparison establishes its own baseline. The phone player turns transmission on if both hold: cold, the added total GPU frame time is at most 2 ms, including the changed shading and pass split; sustained, the frame stays under the 33 ms target and the thermal status reached is no worse than with transmission off. Otherwise it turns it off. The numbers and the choice are written in a report linked from #254 before it closes, as `farShadowMapSize`'s were ([far-cascade-mechanism](../reports/far-cascade-mechanism.md)). A spike of the copy, its chain and the split, without the transmission shading, added 0.96 ms cold and 1.2 to 1.4 ms sustained on the S25 stress scene; the frame stayed at 21 ms and the thermal status reached was 2 with and without it, reached sooner with it ([transmission-cost-phone](../reports/transmission-cost-phone.md)). The editor and the desktop player leave it on.

### Editor

The material inspector shows a Transmission slider and a Transmission texture slot with the same row (texture, UV set, transform, sampler) as the other five. The Scene and Game views each make their own copy, since the copy is a transient image of each view.

### Acceptance

- *GPU regressions* (`renderer_tests`, run on Lavapipe in CI), each with a numeric check:
  - transmission 0 and 1 against metallic 0 and 1: provably non-transmitting materials render as without the extension, including an opaque metal with no metallic-roughness texture behind a glass pane and with overlapping triangles; it retains depth and motion and is present in the opaque copy. Texture-dependent metals are also checked for zero transmitted radiance and the correct shadow coverage;
  - `Opaque`, `Mask` and `Blend` coverage, shadows included, with transmission on and off;
  - a non-convex transmissive mesh whose triangles overlap on screen, viewed from both sides: the result is the documented index-order composition of the stated limit, pinned by the test, so a change to it is seen;
  - with transmission off, a dielectric at `t = 0.5` matches the ordinary lit material blended at half coverage, exercising direct diffuse, diffuse IBL and flat ambient separately;
  - a transmission texture with black and white halves, on the second UV set, with a texture transform and a nearest sampler: transmission follows the texture;
  - an unlit high-frequency pattern behind a flat white pane at `t = 1`, `m = 0` and roughness 0, viewed orthographically along its normal, matches the analytically Fresnel-weighted reference with the same tone mapping: its HDR contribution is `0.96 * S`. The pane has no emissive contribution, the environment and lights are absent, and ambient, TAA and bloom are off. This checks detail preservation with the expected attenuation; at roughness 1 the contrast falls by an amount the test pins;
  - the same scene in a Scene and a Game view of different sizes, an odd size (1023×577), and after a resize. Sweep the longest dimension through 1023/1024/1025 and 2047/2048 in landscape and portrait at roughness 0, 0.5, 0.95 and 1: check LOD endpoints and monotonicity, no premature final-mip selection, and image contrast across neighbouring sizes within a tolerance recorded by the test;
  - no transmission image and no transmission pass in the graph when transmission is off or the view has no draw that can transmit, including a view containing only provably non-transmitting metals with a nonzero transmission factor.
- *Reference comparison.* TransmissionTest, CommercialRefrigerator and ChronographWatch, rendered by Sonnet and by the reference renderer at the commit above, each framed by its default bounding-box camera, under the `Cannon_Exterior` environment of [glTF-Sample-Environments](https://github.com/KhronosGroup/glTF-Sample-Environments), exposure 1, ACES tone mapping in both, at 1920×1080. They are compared feature by feature (each TransmissionTest row's tint, blur and mask, the metallic row's opacity, the frosted door, the watch's clear glass and reflection), not pixel by pixel, because of the differences under *Limits*. The renders are attached to the PRs.
- *TAA and mobile*: the measurements above.

## Consequences

- TransmissionTest's thin cases render with the features the reference renderer shows: tinted clear glass, glass blurred by its roughness texture, transmission masked by its texture, and none on the metallic row. CommercialRefrigerator's door is frosted. ChronographWatch keeps its clear glass and gains its reflection.
- A scene without transmissive materials pays nothing: no image, no pass, no change to the forward pass. Every material is 32 bytes larger on the GPU.
- A view with glass costs a compute copy, one small pass per level, and the store and reload of the HDR target and depth between the two forward passes. The copy and its chain take about 21 MiB at 1920×1080 and 84 MiB at 3840×2160, per view with glass; the editor's two 4K views take about 169 MiB for them alone. The copy keeps no history between frames, but its memory stays in the render graph's pool, like any transient image, until no frame has used it for `PoolRetentionFrames`.
- On a tiler the split loses tile residency for the whole target, which is the cost the mobile measurement decides on.
- Transmissive draws join the CPU-recorded blended path, so many transmissive draws cost what as many blended draws cost.
- The importer no longer bakes transmission into alpha, so a reimported glTF material with transmission has its authored alpha back; with transmission off the renderer draws a similar blend.
- Models with `KHR_materials_volume` as an optional extension (ABeautifulGame, DragonAttenuation, MosquitoInAmber and others) draw as thin glass: blurred and tinted, but not bent and not attenuated by thickness. Models that require it are still refused.
- The material and bundle formats change once more before the freeze, and the material has six texture slots everywhere five were listed (`docs/assets.md`, the inspector, the cook).
- Work, as planned in #254: (1) the passes, the split and the draw-list change; (2) the material fields and texture slot, the record, the shading, the importer, the inspector and the GPU regressions; (3) the TAA and mobile measurements and the phone player's setting. `docs/rendering.md` and `docs/assets.md` change with them, and `docs/assets.md`'s line about transmission importing as alpha blending goes.

## Alternatives considered

- *Blending only.* Premultiplied blending fixes #252's lost highlights and covers ChronographWatch with no pass and no memory, and dual-source blending, once the RHI has it, would add ToyCar's tint. Neither blurs, and TransmissionTest's rough row, CommercialRefrigerator, CompareTransmission and GlassBrokenWindow are rough.
- *Keeping #252's import as the only path.* No format change and no cost, and #254 moves back to Post-1.0, but glass loses its reflection, never blurs and never tints.
- *Leaving `transmissionTexture` for later.* The record stays at 208 bytes, but six of the sample models, TransmissionTest among them, use it, and adding it after 1.0.0 is another material and bundle version.
- *Packing the sixth sampler index into `flags` or `sampler`.* It saves a word, but `sampler` has two bits left and `flags`' upper bits are its future flags; the record needs a new 16-byte row for `transmission` anyway, and the index fits in it.
- *A linear copy of an image shared between a colour and a transmission slot.* It would read both correctly, but it is a second texture per shared image, and the other data slots have the same restriction today; one rule for all of them, with a warning, is simpler, and the fix, if needed, is for all six slots.
- *Including blended surfaces in the copy,* as the reference does. It needs the blended draws drawn twice, once into the copy and once over it, or the copy taken after them, which then shows the blended surfaces in front of the glass as if behind it.
- *The punctual transmission lobe now.* It is one more term per light in the forward pass; no acceptance model needs it to show its features, and it waits for volume and IOR, which change the same lobe.
- *Baking the fallback at cook time.* A cook per platform could write either form, but then one bundle no longer runs everywhere, and the editor and a desktop player could not switch.
- *A copy at a fixed size* (the reference's 1024²). It makes the blur independent of resolution and is cheaper at 4K, but clear glass shows a resampled image that does not match the scene beside it. The view's size with the extent-based LOD mapping keeps level 0 exact and calibrates the nominal blur to the longest dimension; a smaller level 0 can come later as a mobile setting if the measurement asks for it.
- *Taking the log of a desired pixel footprint and clamping to the last mip.* It assumes power-of-two mip footprints. At roughness 0.95 a 1023-pixel view clamps to its final 1×1 level while a 1024-pixel view still blends 2×2 and 1×1. Mapping through the actual extents avoids that premature clamp; the image regressions still check the filter's behaviour across those resize boundaries.
- *An `R11G11B10` copy.* Half the bytes, but the RHI has no such format, and the copy would no longer match the HDR target it is read against.
- *Last frame's resolved TAA history as the copy.* No copy and no split, but it exists only under TAA, so FXAA and no anti-aliasing would need the copy anyway, and it already holds last frame's glass, so glass would see itself, reprojected with motion that does not exist behind glass.
- *A motion written by transmissive surfaces.* It gives the glass's own layer the right motion and the layer behind it the wrong one; which error is worse is what the TAA measurement decides.
- *Reading the copy inside one pass* (input attachments or framebuffer fetch). It keeps tile residency, but it reads only the pixel under the fragment, with no mip chain to blur by roughness.
- *Sorting transmissive draws apart from blended ones* (all transmissive after all blended, or before). Either order draws some surfaces in the wrong order; sorting them together keeps back-to-front order for both.
