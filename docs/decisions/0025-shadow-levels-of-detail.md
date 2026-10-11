# ADR-0025: A coarser index range for the far shadow cascades

- **Status:** Accepted
- **Date:** 2026-10-11

## Context

[Issue 173](https://github.com/Pacheco95/sonnet/issues/173) found why the last shadow cascade dominates the stress scene on a phone, and [issue 180](https://github.com/Pacheco95/sonnet/issues/180) asks for what is left once its map is small ([report](../reports/far-cascade-mechanism.md)). The last cascade draws every caster of the scene (10 000 draws, 1.66 M triangles in the stress scene) at about three triangles to a 9 cm texel. On the Galaxy S25 Ultra (Adreno 830) its cost is a floor of about 3.3 ms for submitting the triangles plus a part that follows the map size; the phone player already takes the second part away with a 512 map on Android (1024 on iOS), which left the cascade at 3.64 ms cold and 5.99 ms sustained.

What is left is the triangle count, and a probe measured it. With the stress scene's spheres at 80 and 20 triangles instead of 320 (cold, 512 far map, two runs each):

| Triangles in cascade 3 | Cascade 3 | GPU total |
|---:|---:|---:|
| 1.66 M (320 per sphere) | 3.64 ms | 14.83 ms |
| 0.46 M (80 per sphere) | 2.20 ms | 14.4 ms |
| 0.16 M (20 per sphere) | 0.82 ms | 13.35 ms |

The cost falls with the triangle count down to a remainder near 0.8 ms. The probe is an upper bound on what the idea can give, not a measurement of an implementation, and says nothing about how the shadows look.

Constraints found in the code:

- The engine has no mesh levels of detail. A `Mesh` is one vertex buffer and one index buffer, and a `Submesh` is a range of the index buffer with a material slot (`Mesh.h`).
- A batch is a run of draws with the same mesh, submesh range, pipeline and winding (`Renderer::buildBatches`). Culling writes one command per batch per job, and the first survivor of a batch writes the command's index range from its candidate, so every draw of a batch has to carry the same range ([ADR-0014](0014-indirect-draws-without-count.md), [ADR-0016](0016-instanced-batches.md)).
- A cascade is a cull job of its own over the `all` candidate list, with its own commands (`ensureIndirectBuffers`). The push constants of the culling dispatch are exactly 128 bytes, the limit (`CullConstants`), but the job's `flags` word has free bits.
- The per-frame fill is bandwidth-bound ([roadmap](../roadmap.md#the-per-frame-fill-is-bandwidth-not-computation)): a candidate is 48 bytes, an object 96.
- `cookMesh` already reshapes a mesh for the player (weld, tipsify, renumber) and a bundle holds the result as one binary block (`encodeMesh`, `BundleVersion` 3). The twelve built-in primitives are generated at runtime and never cooked.
- Skinned and morphed draws read a per-instance deformed vertex buffer, and a morphed mesh is not welded because its vertices may move apart ([ADR-0015](0015-bindless-vertex-buffers.md), [ADR-0023](0023-animation-blending-morph-targets-and-particles.md)).
- The bundle format and the sidecar format are among those [M14](../roadmap.md#m14-performance-targets) means to freeze, so a change to a mesh payload is cheaper now than after the freeze.

## Decision

**A submesh may carry a second, coarser range of the same index buffer, which only the far shadow cascades draw.**

- *The data.* `Submesh` gains `shadowFirstIndex`, `shadowIndexCount` (0 when there is none) and `shadowError`, the worst distance the coarse range's surface is from the fine one, as a fraction of the mesh's bounding radius. The coarse range is a list of indices into the same vertices, appended to the index buffer after the fine ranges, so the vertex buffer, the bounds, the material slot and the batch's other state are shared. Nothing is decimated into new vertices, so no vertex is invented that a normal or a tangent would have to be made for.
- *Building it.* One function in `renderer`, `buildShadowRange(MeshData &)`, runs `meshopt_simplify` over each submesh's indices with `LockBorder` (a boundary of the submesh, and so a seam between material slots, does not move) and a target error of 10 % of the bounding radius (an icosphere's 80-triangle level deviates by 6.6 % of its radius, its 20-triangle level by 20.5 %, so a 3 % bound would leave it at 320), and keeps the result only if it has at most half the triangles. `cookMesh` calls it after tipsify, so a bundle carries the range and the player does no simplification; the database calls it for a mesh imported from source, and the built-in primitives call it when they are generated. It does not run for a skinned or a morphed mesh, whose draws are deformed per instance, or for a submesh under 64 triangles.
- *Choosing it.* `RendererSettings::shadowLodCascade` is the first cascade that draws the coarse ranges (`std::optional`, unset by default so desktop output does not change; the phone player sets it to the last cascade, as it sets `farShadowMapSize`). When resolving a draw the renderer marks it coarse if `shadowError` times the draw's world bounding radius is at most half the texel size of that cascade (`RendererSettings::shadowLodTexels`, 0.5). A large mesh keeps its fine range, since its error is many texels, and a small one drops to the coarse range, which is where the triangles are. The mark is part of the batch's key, so a batch's draws stay uniform and the command's single range stays valid.
- *Drawing it.* `CullDraw` gains `shadowIndexCount` and `shadowFirstIndex` (56 bytes) and a `CullCoarse` flag. A cull job that carries a new `ShadowCoarse` flag in its job flags writes the shadow range into the command for a candidate marked coarse; every other job writes the fine range as it does now. The depth pre-pass, the forward pass, the id and mask passes, the local shadow maps and the first cascades are untouched.
- *Skin and morph, hashed alpha.* They keep their fine range in this ADR. A masked or blended caster in the far cascade keeps its alpha test, which reads the same vertices' UVs.
- *Format.* `encodeMesh` writes the three fields per submesh, `BundleVersion` becomes 4 and the loader refuses a bundle of an older version as it does now. `docs/assets.md` and `docs/rendering.md` follow.

**Acceptance for the implementation** (the S25, cold and sustained, with the 512 far map, in the stress scene): cascade 3 at or under 2.5 ms cold, from 3.64; the sustained GPU frame not slower than today's 23.77 ms; the shadow-factor view of the far field changes by no more than 512 against 1024 did (mean 0.56 of 255, 1.5 % of pixels over 8). On the desktop, the stress scene's captures are byte-identical with the setting unset, and the cascade regression passes with it set.

## Consequences

- The far cascade stops paying for triangles it cannot resolve, without a sharper-shadow trade: the probe says 2.2 ms at 80 triangles per sphere, against 3.64 ms, and the 512 map's softness is untouched.
- A mesh's index buffer grows by its coarse range (about 25 % for the stress scene's 320-triangle spheres at 80; less for a mesh that keeps a sharper range), and every mesh payload grows by three words per submesh. The cook gets slower by one simplification per submesh.
- The candidate array grows by 8 bytes a draw: 80 KB at ten thousand draws, against the 960 KB of the object array.
- Batches can split in two where a mesh's draws differ in scale across the eligibility line, so a scene with one mesh at very different scales has more commands in the all list.
- The shape of a far shadow changes where a mesh's coarse range deviates by up to half a texel; at the far cascade's 37 cm texel on a phone, that is invisible for a sphere of a metre, and the 10 % bound with the half-texel gate keeps it small for the rest.
- A coarse range is a second thing to keep right under re-import and hot reload: it is derived from the fine range, so it is rebuilt with it, and a mesh edit in the editor drops it.
- `bundle` version 4 is a break before the format freeze, not after it.

## Alternatives considered

- **A mesh LOD chain for every pass.** The engine has no camera-distance LOD, and the forward pass is already limited by fill and bandwidth on a phone, not by these triangles. A shadow-only range is the part with a measurement behind it.
- **Per-draw selection in the shader by distance.** It would make the draws of a batch carry different ranges, which the command model forbids, or double the commands for every mesh. The CPU's per-draw mark folded into the batch key gives the same choice with the model intact.
- **A hierarchy of coarser ranges, one per cascade.** Cascades 0 to 2 are cheap (cascade 2 is 0.37 M triangles at 1.05 ms), so the second range would buy little; one range, one cascade setting, until a scene needs more.
- **Decimating into new vertices (quadric edge collapse with attributes).** Better shapes at a given count, but it needs its own normals and tangents and a vertex buffer to match; reusing the fine vertices keeps the draw state identical.
- **Drawing the far cascade less often.** It removes the cost on skipped frames and is orthogonal, but it needs the cached cascade's matrix and the texel snapping to stay valid as the camera moves, and a hitch on the frames it is redrawn; it can follow this if the sustained number asks.
- **Writing our own simplifier.** `meshoptimizer` is a vcpkg port, the standard for this, and [ADR-0004](0004-vcpkg-first.md) prefers an installed dependency to a new implementation; the cost is one dependency in `assets` and `renderer`, to be confirmed at the dependency rule.

## Open questions

Settled in review:

- `meshoptimizer` is acceptable as a dependency of `renderer` (MIT, a small vcpkg port, 1.2 in the tree; to be confirmed against the repository's baseline when implementing). `buildShadowRange` has to live there, since the built-in primitives are generated in `renderer` and `assets` (above it in the dependency rule) calls it from `cookMesh` and the importers.
- The build bound starts at 10 % of the bounding radius with the half-texel gate. The gate, not the bound, protects larger meshes: a 4 m object's 10 % is 40 cm, over a half texel, so it keeps its fine range. If the far-field shadow-factor diff stays within the 512-against-1024 level, raise the bound to about 25 % to reach the 20-triangle level (0.82 ms in the probe); a sphere's 20-triangle level is 0.28 of a 37 cm texel.
- The remainder near 0.8 ms at 20 triangles is not a fixed cost: skipping the cascade's draws leaves 0.01 ms, and the probe's cost per triangle falls as triangles get smaller (about 4.6 ns from 20 to 80 per sphere, about 1.2 ns from 80 to 320). It is measured again after the implementation, not decided here.
