# ADR-0040: FBX import with ufbx

- **Status:** Proposed
- **Date:** 2026-11-09

## Context

Sonnet imports `.gltf` and `.glb` through one fastgltf importer. An FBX asset currently needs an external conversion before it can enter the project, adding a tool dependency and a second place where materials, transforms or animation can change. FBX import should use the existing asset workflow: a source file and sidecar, derived sub-assets, a model prefab, asynchronous loading, reimport and cooking.

The engine already represents meshes, materials, node hierarchies, skins, clips and morph targets ([assets.md](../assets.md)). The limits are the renderer's vertex layout and metallic-roughness materials, four skin influences per vertex, eight morph targets, two UV sets, and local translation, quaternion rotation and scale. FBX adds conversion work around units, axes, pivots, geometric transforms, scale inheritance and exporter-specific materials. Parsing a file alone does not establish that the engine can reproduce it.

The existing `GltfImport` and `GltfStructure` results in [Importers.h](../../modules/assets/include/sonnet/assets/Importers.h) contain Sonnet types, with no public fastgltf objects. Generalizing these names and dispatch lets additional parsers feed the same asset pipeline. Supporting more formats does not require one parsing library or another intermediate scene representation.

[ufbx](https://github.com/ufbx/ufbx) provides FBX parsing, triangulation, transform conversion and animation baking as a C library with an [MIT license option](https://github.com/ufbx/ufbx/blob/main/LICENSE). [Godot uses it for new FBX imports](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html#importing-fbx-files-in-godot). The investigation found no ufbx port at Sonnet's pinned vcpkg baseline. Source and documentation review support this choice; no Sonnet FBX import or platform build has yet validated it.

The FBX animation contract is a concrete reason to prefer ufbx: its [evaluation and baking](https://ufbx.github.io/elements/animation/) handle curves, layers, pivots and Euler rotations before Sonnet consumes TRS tracks. The inspected Assimp FBX converters in [6.0.4](https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/FBX/FBXConverter.cpp#L3240) and [6.0.6](https://github.com/assimp/assimp/blob/v6.0.6/code/AssetLib/FBX/FBXConverter.cpp#L3266) interpolate scalar keys linearly and leave layer parameters unused in transform-key conversion. Assimp already supports pivot helpers, geometric transforms and material conversion; the concern is fidelity for the declared animation subset. These are source findings, not comparative importer benchmarks, and ufbx baking still needs explicit error tolerances and reference-pose validation.

Replacing fastgltf has separate preservation costs. Sonnet's pinned [vcpkg port](https://github.com/microsoft/vcpkg/blob/a1cae005c39be7b18ba319fced856b68d7276271/ports/assimp/vcpkg.json) offers Assimp 6.0.4#3, whose [glTF conversion](https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Importer.cpp#L1203) drops STEP interpolation and cubic TRS tangents; its [build patch](https://github.com/microsoft/vcpkg/blob/a1cae005c39be7b18ba319fced856b68d7276271/ports/assimp/build_fixes.patch) does not change that path. The inspected [6.0.6 conversion](https://github.com/assimp/assimp/blob/v6.0.6/code/AssetLib/glTF2/glTF2Importer.cpp#L1330) preserves those TRS semantics. Even there, [morph keys](https://github.com/assimp/assimp/blob/v6.0.6/include/assimp/anim.h#L198) lack interpolation/tangent fields, animation `extras.events` need supplementary preservation, and the inspected glTF paths have no meshopt decoder. An adapter could address these gaps, but format breadth alone does not justify that work.

## Decision

**Import `.fbx` directly with ufbx in `assets`, alongside fastgltf for `.gltf` and `.glb`. Both parsers produce the same engine-owned model data and use the existing database, prefab and cooking paths.** FBX is a source format; its supported subset becomes the existing cooked payloads, with no new scene, prefab or bundle schema required by this decision.

A future Assimp adapter in `assets` is explicitly permitted for concrete additional format requirements. It produces the same Sonnet data, enables only the required importers and defines each format's supported subset and fixtures. Adding it does not require migrating glTF or FBX. Replacing either chosen parser would need a new decision supported by a prototype that preserves existing behavior and identities, passes platform builds and demonstrates a maintenance or measured operational benefit.

### Dependency and ownership

- Package a reviewed ufbx release through a pinned `ports/ufbx` overlay port, with the source hash and MIT notice, following [ADR-0004](0004-vcpkg-first.md). Add it to the manifest when the importer is implemented and describe it in [ports/README.md](../../ports/README.md). Keep upstream source unmodified and provide the dependency through this one mechanism.
- Link ufbx privately to `assets`. Parser objects stay inside the importer; public results contain Sonnet types. There is no importer plugin system or new module.
- The current assets target contains source importers on every platform, and the player can open a source project as well as a bundle. ufbx must therefore build with the supported Windows, Linux, macOS, Android and iOS toolchains. Bundle playback does not parse FBX or evaluate its curves. Separating importers into host tools is outside this decision.

### One model pipeline

Generalize the glTF-specific result and metadata types in `Importers.h` and the calls that dispatch model import, structure reads and dependency enumeration. Keep format-specific parsing in its own implementation. Update the database and editor callers together; fastgltf's existing results, ordering and identities remain the regression baseline.

Preserve glTF's TRS and morph-weight interpolation, embedded and sidecar animation events, supported extensions including meshopt compression and Basis Universal textures, per-slot samplers and UV transforms, cameras and lights, and JSON-only structure reads. These are existing [importer behaviors](../../modules/assets/src/GltfImporter.cpp), not new requirements on every source format.

The dispatch covers source recognition, sub-asset discovery, synchronous and asynchronous loading, model hierarchy reads, thumbnails, import and duplicate commands, dependency-aware moves, reimport, animation metadata in the inspector, and desktop/mobile cooking. Workers produce CPU data; the existing main-thread publication creates renderer objects. A model and its sub-assets remain one import request.

Structure reads and full imports share the same hierarchy construction and conversion options. They produce identical node ordering, names, helper nodes, mesh/skin mappings and clip ordering. An FBX structure read may need more parsing than glTF's JSON-only read, but does no image decoding, mesh cooking or GPU work.

### Supported geometry and materials

- Read binary and ASCII FBX supported by the pinned parser. Document the exporter versions exercised by fixtures; parser acceptance is not a promise of arbitrary DCC scene fidelity.
- Import polygon meshes, triangulating faces and preserving material boundaries, hard edges, UV seams, normals, tangents and one vertex-colour set. Retain up to two UV sets. Shared geometry keeps its node instances; when instances assign different material-slot lists, create deterministic mesh variants where the existing model representation requires them.
- Convert axes, handedness and units to Sonnet's right-handed, +Y-up, metre convention. Use [ufbx's evaluated transforms](https://ufbx.github.io/fbx/node-transforms/) and geometry/scale helper nodes where FBX inheritance needs them. Preserve their hierarchy instead of decomposing an arbitrary composed matrix into one TRS node and discarding shear. Winding, tangent handedness and bind transforms follow the same conversion.
- Map supported base colour, metallic, roughness, normal, emissive and opacity properties to `MaterialSource`, including texture UV selection and transforms where representable. Pack separate metallic/roughness maps into the engine's expected channels. Legacy diffuse/specular/shininess conversion is an approximation whose defaults and limits are documented and tested. Arbitrary shader graphs, layered/procedural textures and renderer-specific material features are outside the supported subset.
- Import embedded image bytes and supported external images through the existing image and KTX2 pipeline. Resolve external files relative to the model's folder, preserving the dependency rule that files travel with the model. Prefer a valid relative reference when FBX also records an export-machine absolute path; do not search unrelated host directories. Missing or unsupported textures produce a named warning and the existing material fallback. Dependency enumeration and editor copy/move operations use the same resolution rules as import.

FBX cameras, lights, procedural geometry, constraints and control rigs are outside the initial contract. Export polygon meshes and baked animation from the authoring tool when those features affect the desired result. FBX export and round-trip editing are not provided.

### Animation and deformation

Animation uses the ownership and path bindings of [ADR-0010](0010-audio-and-animation.md), and morph targets use [ADR-0023](0023-animation-blending-morph-targets-and-particles.md). No FBX evaluation enters the frame loop.

- Convert each supported animation stack/take into an `AnimationClip`, baking evaluated layers, pivots and Euler rotations with [ufbx's animation facilities](https://ufbx.github.io/elements/animation/). Tracks use seconds, local translation/scale and quaternion rotation on the converted hierarchy, including helper nodes. Normalize each take's time origin and preserve its duration, rotation continuity and stepped changes. Sampling and error tolerances are explicit, documented and checked against evaluated reference poses.
- Import linear skinning with inverse bind matrices in the converted mesh/joint spaces. Keep the four greatest influences per vertex, break equal-weight ties deterministically and renormalize, with a warning when influences are discarded. Invalid bindings are import errors.
- Import ordinary morph targets and their weight channels within the existing eight-target limit. Keep target ordering consistent across mesh deltas, initial weights and clips; warn about discarded targets. Dual-quaternion or mixed skinning, progressive/in-between morphs and vertex-cache deformation are unsupported in this scope. They require an authoring-tool conversion rather than silently using a different deformation model.
- Reuse sidecar animation events and the inspector's clip metadata. Retargeting, animation-layer editing and live rig evaluation are separate work.

Malformed input and transforms or deformers that cannot be represented return a recoverable asset error naming the file and affected object. Optional appearance data may fall back with a warning. A required deformation is never silently dropped to make an animated asset appear successfully imported.

### Identity and reimport

The model keeps its sidecar UUID. Sub-assets use the existing kind/index derivation, and prefab nodes use the name paths of [ADR-0030](0030-model-node-identities.md). The importer generates deterministic helper names from their owning node and role, resolving name collisions consistently. Skin joints and animation channels address that same hierarchy.

Parser replacement must preserve the meaning of those indices. Sonnet groups a glTF mesh's primitives into submeshes, whereas Assimp's [glTF conversion](https://github.com/assimp/assimp/blob/v6.0.6/code/AssetLib/glTF2/glTF2Importer.cpp#L473) splits them into meshes and compacts embedded images into a separate array. Using parser indices directly would change existing UUID bindings; a migration needs explicit source grouping and index mappings.

Identical reimports and moving a model with its sidecar preserve identities. Adding an unrelated named node preserves the identities of existing named paths. This does not guarantee stable mesh/material/clip references after an exporter reorders sub-assets, or stable overrides after node renames, reparenting, duplicate-name reordering or changes to unnamed-node indices. Document and test those boundaries. This ADR does not introduce a new matching scheme or amend ADR-0030.

## Consequences

FBX assets enter the existing editor and export workflow without a conversion executable or Autodesk SDK. `world` and `renderer` consume their existing data; glTF remains supported by fastgltf. The cost is another parser dependency and maintained conversion policies and fixtures, especially for transforms, materials and animation.

Implementation proceeds through the complete static-model workflow first, then validated skin, clip and morph import. Each increment documents what it supports; assets requiring deformation that is not yet implemented fail with a diagnostic. A dependency-and-conversion spike precedes the integration so the actual exporters and platform toolchains can validate the assumptions. Accepting this ADR alone adds no importer or dependency.

Before declaring the corresponding support complete:

- Build the dependency with all supported desktop and mobile toolchains. Use small original or redistributable fixtures, recording their exporter/version and expected results, including binary and ASCII FBX from Blender and at least one Autodesk DCC.
- Cover units and axis conversion, mirrored and nonuniform scale, pivots and geometric transforms, material variants on instances, n-gons, seams, both UV sets, embedded/external/missing images, and malformed/truncated files. Unsupported features must exercise their diagnostics.
- For animation, compare bind poses and sampled deformation against evaluated reference poses: multiple takes, nonzero time origins, long rotations, stepped changes, helper nodes, excess influences, and supported morphs. Compare source-project playback with cooked playback.
- Test structure/full-import agreement, asynchronous publication, import undo/redo, thumbnails, dependency copying and moves, reimport and prefab overrides. Check repeated imports and exporter reordering separately, so the documented identity limits are visible.
- Verify desktop and mobile texture cooking and the presence of every expected model sub-asset in the bundle; a successful cook exit alone is insufficient because the current cook can warn and omit an asset. Use null-RHI tests and engine-generated editor/player captures, and retain glTF regression coverage.

The implementation updates [assets.md](../assets.md), [editor.md](../editor.md), [architecture.md](../architecture.md) and the dependency documentation to describe the shipped subset. Parser upgrades run the same fixture corpus; broader exporter compatibility is established by those results, not by the extension being recognized.

## Alternatives considered

- **Assimp.** Its common scene representation and postprocessing can reduce adapter work for several required formats, and [selective importer builds](https://github.com/assimp/assimp/blob/v6.0.6/code/CMakeLists.txt#L236) make a future adapter practical without enabling every format. It remains permitted for that role; ufbx better fits this FBX evaluation contract, while retaining fastgltf avoids the identified glTF preservation work.
- **Autodesk FBX SDK.** The vendor implementation is an option for a demonstrated fidelity gap, but adds proprietary distribution terms and platform packaging work. Its current [platform summary](https://aps.autodesk.com/developer/overview/fbx-sdk) omits Android, which matters while source import lives in the shared assets target.
- **Require external conversion to glTF/GLB.** Blender remains a useful manual workaround, but makes every user maintain a second import workflow and diagnose conversion losses outside Sonnet. Godot's [FBX2glTF fork](https://github.com/godotengine/FBX2glTF) is no longer actively maintained, so it is not adopted as a mandatory converter.
- **Write an FBX parser.** Avoids a dependency while making Sonnet responsible for format versions, transform semantics, malformed-file handling and exporter quirks already addressed by ufbx. That work does not advance the engine's asset model.
