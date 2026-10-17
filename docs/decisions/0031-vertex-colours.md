# ADR-0031: Vertex colour is a packed RGBA8 in Vertex

- **Status:** Accepted
- **Date:** 2026-10-17

## Context

glTF's `COLOR_0` is a core attribute: a vertex colour that multiplies the base colour, with its alpha multiplying the base alpha. Stylized, low-poly and photogrammetry assets rely on it. The importer does not read it and `renderer::Vertex` has no field for it, so those assets import flat white or untinted ([#281](https://github.com/Pacheco95/sonnet/issues/281)).

`Vertex` is 56 bytes behind a `static_assert`, matches `Vertex` in `shaders/sonnet.slang` under scalar block layout, and is read from a bindless `StructuredBuffer<Vertex>`. The second UV set ([#251](https://github.com/Pacheco95/sonnet/issues/251)) went into it the same way: `uv1` is always present and zero where a mesh has no second set. Its consumers are the shaders that read the vertex buffers (`forward`, `depth`, `overlay`, `skin`, `sonnet`) and `Mesh`, `GltfImporter`, `Renderer` and the cook on the C++ side.

The [1.0.0](../roadmap.md#100) rule wants the mesh and bundle formats settled first. This change alters both.

## Decision

**`Vertex` gains `std::uint32_t color`, four unsigned-normalized bytes (R, G, B, A) in linear space, after `uv1`. `Vertex` becomes 60 bytes, and the bundle version becomes 17.**

- *Default.* A mesh without `COLOR_0` stores `0xFFFFFFFF` (white, opaque), not zero, so an uncoloured mesh renders as it does today. The primitives and the importer write it.
- *Import.* `COLOR_0` as `vec3` or `vec4`, as float, normalized `ubyte` or normalized `ushort`, converts to RGBA8. A `vec3` gets alpha 1. The values are linear, as glTF defines them, so no sRGB conversion happens. Out-of-range floats are clamped, since an 8-bit store cannot hold them.
- *Shading.* `forward.slang` multiplies the unpacked colour into the base colour, alpha included. `depth.slang` multiplies it into the alpha used by the alpha test and by hashed alpha, so a shadow cut out by vertex alpha matches the lit surface. `skin.slang` carries the field through unchanged.
- *Cook and bundle.* The cook's vertex welding compares the colour with the other attributes, so two vertices that differ only in colour stay apart. The mesh section of the bundle stores the 60-byte vertex, which raises `BundleVersion`.
- *Morph targets.* `COLOR_0` is not morphed. glTF allows a morph target to carry `COLOR_0`; the importer ignores it.

## Consequences

- Vertex-coloured assets import and render as authored. A mesh with no colour pays 4 bytes per vertex (about 7%) for a constant.
- Every shader that declares `Vertex` changes its struct, and a mismatch is silent under scalar layout. `static_assert(sizeof(Vertex) == 60)` guards the C++ side. A GPU test that draws a vertex-coloured mesh through `forward` and its shadow through `depth` guards the shaders, and is part of this work.
- Existing bundles are rebuilt rather than migrated, as the compatibility proposal in [PR #151](https://github.com/Pacheco95/sonnet/pull/151) has it. Sidecars and project files do not change.
- RGBA8 holds 8 bits per channel. A `ushort` source loses its low bits, which is not visible in a colour that multiplies a texture. HDR vertex colour (the `KHR_materials_emissive_strength` style of overbright values on `COLOR_0`) is not supported.

## Alternatives considered

- *A separate optional colour stream.* Only coloured meshes would pay for it. It needs a second vertex buffer binding in every pipeline and shader that draws or skins a mesh, a per-mesh flag, and cull and indirect-draw paths that know whether the stream exists. For 4 bytes per vertex that is more machinery than the saving, and the second UV set already took the always-present route.
- *`vec4` float colour in `Vertex`.* 16 bytes per vertex (56 → 72) for precision a multiplier on a texture does not show.
- *Colour as a material property baked per mesh.* It cannot vary across a surface, which is the point of vertex colour.
- *Ignore `COLOR_0` and document it.* The format would then change later, after the freeze, for an attribute the spec makes core.
