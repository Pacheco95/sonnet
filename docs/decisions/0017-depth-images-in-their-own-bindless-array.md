# ADR-0017: Depth images in their own bindless array

- **Status:** Accepted
- **Date:** 2026-09-23

## Context

On an Apple M4 Max (MoltenVK 1.4.1) the basic sample rendered washed out, with the sun lighting only the sides of objects and no visible shadows. A probe branch rendered the forward shading term by term and read the results back. The BRDF lookup table's texels, copied out with a blit, matched an RTX 4090 to four decimals. The forward shader read the same texture back with green equal to red, through a filtered sample, a nearest sample and a sampler-free `Load` alike.

MoltenVK's translated fragment shader shows why: it declares set 0's sampled-image array as `array<depth2d<float>, 4096> textures`. SPIRV-Cross types a whole array as Metal depth textures when any use of it is a depth comparison, and the only one in the engine is the shadow lookup's `SampleCmpLevelZero` on the cascades, which lived in `textures[]` beside every colour texture. A Metal depth texture reads as one float, which the shader widens to `(r, r, r, r)`. Every colour read in the forward shader was affected. Base-colour textures lost their colour. The flat normal map turned every normal towards a diagonal, which is why the sun lit the sides of objects and not their tops. Metallic-roughness read the wrong channel. The lookup table's bias read as its scale, which added nearly the whole sky to every surface.

The same cause explains the earlier finding that a live `SampleCmp` result "loses the sun's lighting arithmetic" on MoltenVK. The workaround for it (`DeviceInfo::comparisonSamplersUsable`, a manual depth compare) left the `SampleCmp` in the shader behind a runtime branch, so the array stayed depth-typed and the bug stayed. A loosened environment test (the sky-lit sphere allowed 20 of 255 above the sky) had also been hiding the lookup table's error.

## Decision

- **Depth images have a bindless array of their own.** Set 0 gains binding 6, `Texture2D<float> depthTextures[]`, a runtime array of up to `MaxBindlessDepthImages` (64), partially bound and updated after bind like the others. An image with `Sampled` usage and a depth format gets its sampled descriptor and its slot there, the way a cube image gets one in binding 3. `IDevice::sampledImageIndex` returns that slot.
- **Comparisons go through the depth array only.** `textures[]` is never used with `SampleCmp`, so SPIRV-Cross keeps it `texture2d` on Metal. The shadow lookup reads the cascades as `depthTextures[...]` with the hardware comparison, on every platform.
- **The MoltenVK workaround is removed.** `DeviceInfo::comparisonSamplersUsable`, `RendererSettings::manualShadowCompare`, the nearest shadow sampler and the manual comparison go. The environment test is strict again: the sky-lit sphere is dimmer than the sky.
- **Tests that would have caught it.** A GPU test reads a green texture's albedo in the same shader as the shadow comparison, and another reads the lookup table's scale and bias through `RendererSettings::debugView`.

## Consequences

- Colour textures read correctly on MoltenVK in any shader that also compares depth. The rule to keep: a bindless array is used with comparisons or without them, never both.
- 64 depth images can be sampled at once. The renderer samples four, the shadow cascades.
- A depth image cannot be read through `textures[]` any more; a shader that wants to read depth without comparing uses `depthTextures[]` too, which is valid in Vulkan and in Metal.
- The debug view that found this stays in the renderer and the editor (View > Shading term).

## Alternatives considered

- **Keep the manual comparison on MoltenVK and drop `SampleCmp` from that platform's shaders with a specialisation constant.** It would leave hardware PCF off on Apple platforms for no reason, and any future comparison in a shader would bring the bug back.
- **Pass the cascades through the per-pass push descriptors (set 1).** Set 1 holds one sampled image for post passes over a single image; four cascades would need four new bindings used by one pass.
- **Drop MoltenVK.** It was the plan before the probe, but it would also drop iOS, and KosmicKrisp, the alternative Apple driver in the SDK, is Vulkan 1.3 only (SDK 1.4.341.1), below [ADR-0001](0001-vulkan-1.4-only.md).
