# Sonnet FBX integration feasibility

Links point at repository files; their labels give line numbers in checkout `4c18021`.

## Does FBX fit the existing architecture?

### Takeaway

Yes: an FBX importer can produce the same engine-owned model, mesh, material, texture, skin and animation data as glTF. A bounded importer need not change the renderer, world representation or cooked bundle format; broad fidelity claims would exceed what the current engine can represent.

### Cited Findings

- This review inspected checkout `4c18021`, version 0.23.0. The manifest pins vcpkg baseline `a1cae005c39be7b18ba319fced856b68d7276271`; the root project enables both C++ and C. [Root project, line 38](../../../CMakeLists.txt), [manifest, line 4](../../../vcpkg.json).
- Asset import, identity, hot reload and cooking belong to `assets`; `world` depends on `assets`, the player on `runtime`, and only the editor is desktop-only. Another model parser belongs privately in `assets`, alongside fastgltf. [Architecture, line 7](../../architecture.md), [assets build, line 1](../../../modules/assets/CMakeLists.txt).
- There is no importer plugin interface to implement. Importers are plain functions. `GltfImport` is already an intermediate result made of engine mesh/material/model/skin/clip types plus names and indices; only its naming is format-specific. The public interface also separates full import, structure-only import and dependency enumeration. [Importer declarations, line 51](../../../modules/assets/include/sonnet/assets/Importers.h).
- The existing cook switches on engine asset types and encodes engine-native models, materials, skins and animations. A Model payload does not retain glTF structures. [Cook, line 110](../../../modules/assets/src/Cook.cpp), [bundle payload documentation, line 219](../../assets.md).
- Source projects are also playable through the runtime; bundle playback instead reads cooked data. `assets` unconditionally links fastgltf today, so “import-time only” does not mean “automatically absent from player/mobile builds.” [Runtime modes, line 22](../../player.md), [assets build, line 1](../../../modules/assets/CMakeLists.txt).
- vcpkg is the default; FetchContent is allowed for a library without a port. Dependency versions are pinned, and patches belong in overlay ports. Accepted ADRs cannot be edited; cross-module decisions receive a new ADR. [Dependency policy, line 48](../../build.md), [project guidance, line 33](../../../AGENTS.md), [conventions, line 19](../../conventions.md).
- The roadmap has completed M16 and the third-person character release; 1.0 waits for formats to settle. Its explicit Later list contains terrain, native file dialogs and incremental cooking, with no explicit FBX entry. This does not establish the private issue-board priority. [Roadmap, line 190](../../roadmap.md), [third-person release, line 209](../../roadmap.md), [format freeze, line 228](../../roadmap.md), [Later, line 258](../../roadmap.md).

### Inferences

- Keep fastgltf. Generalize its shared output structs to `ModelImport`, `ImportedMesh`, etc. and introduce plain model dispatch functions rather than inventing a runtime importer/plugin framework. Add an FBX-specific parser implementation that produces this shared data.
- A small pinned overlay port is consistent with project policy if no registry port exists; otherwise use the available registry port. A permitted pinned FetchContent dependency is a fallback. Upstream-library research, not this source review, determines current port availability.
- Existing rendering and bundle payloads are sufficient for static meshes and conventional linear-blend skinned animation within current limits. More UV sets, bone influences, morph targets, material models or non-TRS transforms would expand scope into renderer/world/schema changes and require a separate decision.
- Dependency integration must compile for current target builds. Making all parsers a desktop-only library is a separate architectural change, not a prerequisite for FBX, and should not be implicitly promised.

### Gaps

- No FBX parser was added or built; no FBX fixture was imported. This is source-level feasibility, not a completed compatibility test.
- This research did not rank the private issue board or inspect external library license/platform support; those are separate research scopes.

## What does a model mean to users, and how stable are its identities?

### Takeaway

A model file is a hierarchy represented as a prefab, with mesh/material/etc. sub-assets. The infrastructure supports deterministic reimport, but current sub-asset indices and prefab-node name paths provide different levels of stability; an FBX implementation must preserve this distinction.

### Cited Findings

- Source extension detection is case-insensitive and recognizes only `.gltf`/`.glb` as model sources. Both become `AssetType::Model`. [Detection, line 38](../../../modules/assets/src/AssetDatabase.cpp).
- Sub-assets derive UUIDs from the parent UUID and `mesh/<index>`, `material/<index>`, `image/<index>`, `skin/<index>` and `animation/<index>`. Display names are not used for these identities. The sidecar stores sub-assets and a source-content hash. [Sub-assets, line 381](../../../modules/assets/src/AssetDatabase.cpp), [sidecar scan, line 459](../../../modules/assets/src/AssetDatabase.cpp).
- `loadModelPrefab` creates a root under the model UUID; child nodes get transforms, mesh renderers, optional skins/morph weights/cameras/lights. A model with clips gets an Animator playing its first clip. Editor and runtime both call this same function. [Prefab construction, line 754](../../../modules/world/src/Scene.cpp), [runtime, line 225](../../../modules/runtime/src/Game.cpp), [editor, line 2205](../../../modules/editor/src/Editor.cpp).
- Model prefab node UUIDs derive from `path/<node-name-path>`, with `#1`, `#2` suffixes for equal sibling names. Renames change identity, and reordering equal siblings swaps which override they receive; unnamed nodes can still carry the source index. These are accepted ADR-0030 limitations. [Node identity code, line 759](../../../modules/world/src/Scene.cpp), [ADR-0030, line 23](../../decisions/0030-model-node-identities.md).
- Animation channels and skin joints bind by name paths. The glTF importer warns on duplicate sibling names; binding takes the first. Node identity suffixes do not by themselves make animation binding to duplicate names unambiguous. [ADR-0010, line 13](../../decisions/0010-audio-and-animation.md), [ADR-0030 constraints, line 14](../../decisions/0030-model-node-identities.md).
- Dragging a model into the viewport loads its prefab if necessary; `dropAction` then instantiates it. A mesh sub-asset instead uses the separate mesh-drop action. [Viewport handling, line 846](../../../modules/editor/src/Editor.cpp), [drop actions, line 13](../../../modules/editor/src/ViewportDrop.cpp).

### Inferences

- Register `.fbx` as another Model source. Existing model browser rows, prefab placement, mesh sub-assets and thumbnails largely follow `AssetType`, so no separate FBX scene format is needed.
- Repeated import of identical content, and moving a file with its sidecar, can preserve identities. Re-export that changes mesh/material/clip ordering is not semantically stable under current index-derived identities. Do not promise stable reimport across arbitrary exporter rewrites.
- FBX import needs a deterministic ordering and one shared node walk for its structure-only and full imports. Any helper nodes used to represent pivots or geometric transforms must have deterministic names and match animation/skin paths.
- Choose and document FBX sub-asset identity policy early. Retaining current index-based identity is a bounded first version; improving semantic stability should be a deliberate change, tested with reorder/rename cases, rather than an incidental claim about FBX object IDs.

### Gaps

- The current code gives no guarantee that an external exporter preserves its object IDs or ordering across exports. That must be verified against the intended exporters.

## Which engine capabilities can be reused, and where are the limits?

### Takeaway

The engine already covers most conventional game model data. The main FBX work is converting source semantics correctly, especially transforms, materials and animation, rather than adding skeletal animation to Sonnet.

### Cited Findings

- Sonnet uses right-handed coordinates, +Y up, cameras down -Z, metres, seconds, radians, column-major matrices and parent × local transform composition. Nodes store translation, quaternion rotation and scale. [Conventions, line 29](../../conventions.md), [ModelNode, line 191](../../../modules/assets/include/sonnet/assets/Asset.h).
- `MeshData` holds counter-clockwise triangle indices, per-submesh material slots, normals, tangent sign, two UV sets and RGBA8 vertex colour. `SkinWeights` holds four influences per vertex; morph targets are dense position/normal/tangent deltas, capped at eight. [Mesh types, line 14](../../../modules/renderer/include/sonnet/renderer/Mesh.h).
- Tangent generation and coarse shadow-range generation are reusable renderer helpers. The existing glTF importer calls them after assembling the final mesh; its flat-normal expansion is an importer-local helper. [Mesh helper declarations, line 97](../../../modules/renderer/include/sonnet/renderer/Mesh.h), [glTF assembly, line 328](../../../modules/assets/src/GltfImporter.cpp), [glTF postprocessing, line 760](../../../modules/assets/src/GltfImporter.cpp).
- Materials are metallic-roughness PBR, with base colour, emissive, normal, occlusion and transmission slots; metallic and roughness share one packed texture (metallic blue, roughness green). [MaterialSource, line 85](../../../modules/assets/include/sonnet/assets/Asset.h), [renderer material, line 59](../../../modules/renderer/include/sonnet/renderer/Material.h).
- Clips already store translation/rotation/scale and morph-weight channels, linear/step/cubic-spline interpolation, seconds and sidecar-authored events; skins store joint paths and inverse bind matrices. [Animation types, line 16](../../../modules/assets/include/sonnet/assets/Animation.h), [event settings, line 70](../../../modules/assets/include/sonnet/assets/Asset.h).
- The default `ModelSettings.lightScale = 1/683` specifically compensates for Blender's glTF photometric conversion. It is not a general model-format convention. [ModelSettings, line 70](../../../modules/assets/include/sonnet/assets/Asset.h).

### Inferences

- Triangulate polygonal source faces, construct indexed vertices with attribute seams preserved, convert units/axes/winding and tangent handedness, and preserve material slots. Unit and axis conversion must affect geometry, node transforms, bind matrices and animation consistently.
- Standard source materials need mapping to current PBR. Separate metallic/roughness textures may need channel packing; legacy shininess/specular conversion is an approximation. Shader graphs and arbitrary DCC materials should be explicitly outside a bounded importer contract.
- FBX pivots/geometric transforms/inheritance must be evaluated or baked into representable nodes/meshes; silently decomposing every result into TRS can lose shear. Test mirrored and non-uniform parent scales, multiple mesh instances and bind poses.
- Skeletal clips can map to existing animation after evaluating/baking unsupported curve or transform semantics. Four-influence truncation should select/renormalize weights with diagnostics. More than eight blend shapes, progressive shape behavior and unsupported skinning methods need an explicit rejection/approximation policy.
- Basic static mesh support should exclude camera/light import initially or define separate conversion rules. Reusing the glTF light default for FBX without checking its semantics is unsafe.

### Gaps

- Fidelity depends on the chosen FBX library and exporters; these statements identify required conversions and existing representation limits, not verified parser capabilities.
- No performance or memory measurements were taken on a large FBX asset.

## What are the concrete implementation touchpoints?

### Takeaway

Most changes are inside assets and a few editor callers. The broad glTF naming is incidental coupling that can be generalized; world, renderer and cooking already accept format-independent results.

### Cited Findings

- Build: add the dependency and source in `modules/assets/CMakeLists.txt` and `vcpkg.json` (or a pinned overlay if needed). [Build, line 1](../../../modules/assets/CMakeLists.txt), [manifest, line 1](../../../vcpkg.json).
- Shared results/functions: `Importers.h` exposes `GltfImport`, `GltfStructure`, `GltfAnimationInfo`, full import, hierarchy read and dependency read. [Interface, line 51](../../../modules/assets/include/sonnet/assets/Importers.h).
- Database extension/source classification, settings initialization, sub-asset discovery and registration all special-case glTF. [SourceKind, line 25](../../../modules/assets/src/AssetDatabase.cpp), [registration, line 349](../../../modules/assets/src/AssetDatabase.cpp).
- The full pipeline is `gltfRequest` → `importGltfFiles` → `publishGltf`, with `loadGltf` and `requestGltf` callers for meshes, textures, skins and clips. Parsing/texture preparation happen on workers; renderer publication happens on the main thread. [Pipeline, line 794](../../../modules/assets/src/AssetDatabase.cpp), [async request, line 1095](../../../modules/assets/src/AssetDatabase.cpp).
- `model()` separately calls `importGltfStructure`; already-published hierarchy storage must remain stable when mesh loading later publishes the full result. A regression test covers identical node indices and pointer lifetime. [Hierarchy loading, line 1292](../../../modules/assets/src/AssetDatabase.cpp), [lifetime test, line 595](../../../modules/assets/tests/DatabaseTests.cpp).
- Mobile ASTC recooking directly calls `importGltf` too, so changing only normal mesh loading misses mobile exports. [Mobile model images, line 717](../../../modules/assets/src/AssetDatabase.cpp).
- Moves check `SourceKind::Gltf` and `gltfDependencies`; reimport refreshes `listGltfSubAssets`, unloads/reloads via glTF machinery and reports changed models. [Moves, line 1807](../../../modules/assets/src/AssetDatabase.cpp), [reimport, line 1994](../../../modules/assets/src/AssetDatabase.cpp).
- The editor Import command calls `gltfDependencies` for every Model, copies the model and dependencies into a private folder, and its Duplicate command reuses that path. The file-dialog filter lists `gltf;glb`. [Import/duplicate, line 536](../../../modules/editor/src/AssetCommands.cpp), [filter, line 1571](../../../modules/editor/src/Editor.cpp).
- The inspector calls `importGltfStructure` directly and stores `GltfAnimationInfo`. [Inspector, line 608](../../../modules/editor/src/InspectorPanel.cpp), [inspector declaration, line 97](../../../modules/editor/include/sonnet/editor/InspectorPanel.h).
- Current dependency enumeration accepts only relative local paths staying within the model folder. Current poll/cache freshness tracks the model source, not a complete dependency fingerprint. [Dependency paths, line 1018](../../../modules/assets/src/GltfImporter.cpp), [polling, line 2084](../../../modules/assets/src/AssetDatabase.cpp), [image cache, line 822](../../../modules/assets/src/AssetDatabase.cpp).

### Inferences

- Generalize one model import pipeline and dispatch by detected source format; retain per-format parsers. No need for an interface hierarchy or new engine module.
- FBX external/embedded texture handling must agree between import, copy, move, duplicate, reimport and mobile cook. In particular, foreign absolute paths should not become hidden dependencies on the author's machine. A bounded relative-path resolution policy is appropriate; broader remapping needs explicit design.
- Preserve hierarchy-only import responsiveness. A parser that reads geometry as part of scanning may have different cost from glTF JSON-only reads; skip unused payloads or cache structure metadata where needed, then measure. Do not introduce full texture import into `model()`.
- Existing external-image reload/cache tracking is a pre-existing limitation, not something FBX introduces. Either include dependency invalidation in the FBX scope or document that limitation honestly.
- Update assets/editor/build/architecture docs with actual supported subset, conversions and limits. If importer identity/settings policy changes across modules, document it in a new ADR rather than modifying accepted ADRs.

### Gaps

- Exact source-file count is not a useful estimate until the shared API shape and identity/settings decisions are made.

## How should this be validated, and how much work is it?

### Takeaway

A small proof of concept can answer the parser/transform questions quickly; production support needs end-to-end import and reimport coverage, not only “triangle appears.” The following estimates are engineering judgment from the reviewed integration surface, not measured delivery commitments.

### Cited Findings

- Existing tests cover glTF geometry/materials/hierarchy, skins/clips/morphs, database identities, asynchronous publication, hierarchy-only loading, moves and dependencies. These provide reusable patterns for FBX fixtures. [glTF tests, line 142](../../../modules/assets/tests/GltfTests.cpp), [skin tests, line 699](../../../modules/assets/tests/GltfTests.cpp), [database tests, line 102](../../../modules/assets/tests/DatabaseTests.cpp), [asynchronous tests, line 548](../../../modules/assets/tests/DatabaseTests.cpp).
- Source-to-bundle roundtrip and mobile cooking already have tests. The cook may succeed overall while warning and omitting a failed asset, so exit status alone is not proof that FBX assets survived export. [Cook tests, line 284](../../../modules/assets/tests/CookTests.cpp), [mobile tests, line 551](../../../modules/assets/tests/CookTests.cpp), [warning behavior, line 160](../../../modules/assets/src/Cook.cpp).
- Engine-generated editor/player screenshots and deterministic capture steps are the project's prescribed visual verification path. [Capture instructions, line 64](../../../AGENTS.md).

### Inferences

- Proof of concept: 1–2 engineer-days. Pin/build the parser and import several redistributable static and rigged fixtures into existing CPU types, checking units, axes, pivot transforms and bind pose. This is not a shippable feature.
- Bounded production static support: approximately 5–10 engineer-days total, including the proof of concept. Covers ASCII/binary parsing if the library supports both, triangles/n-gon triangulation, hierarchy, units/axes, two UV sets, normals/tangents/colour, basic material/texture mapping, editor integration, reimport and desktop/mobile cooking. Assumes limited, named exporter configurations and prompt access to fixtures; unusual material conversions or dependency remapping can exceed this range.
- Animated support within current engine limits: another 5–10 engineer-days, approximately 10–20 days total. Covers inverse bind transforms, four-influence handling, baked takes, clip naming/events, morph-weight conversion within the eight-target cap, and source/cooked visual comparison. This is not arbitrary rig retargeting or every FBX animation feature.
- Broad compatibility across DCC packages, legacy versions, complex materials, unusual transforms/skin methods and many customer files: budget at least 4–8 weeks of engineering plus ongoing fixture-driven maintenance. Confidence is lower, and no finite estimate can guarantee full FBX fidelity without an exporter/feature contract.
- Acceptance fixtures: ASCII and binary; Blender and at least one Autodesk-produced export; centimetres/metres and Y/Z-up; mirrored/non-uniform scales; pivot/geometric transforms; n-gons, UV seams and multiple materials; embedded and relative textures, missing texture and invalid/outside paths; duplicate and unnamed nodes; reordered mesh/material entries; one rig with multiple clips and a root-motion clip; excess influences; morphs; malformed/truncated input.
- Acceptance outcomes: same semantic hierarchy for metadata/full import; no renderer access from workers; stable identities under supported reimport/move cases; explicit diagnostics for unsupported features; undo/redo import and model drop work; cooked bundle contains all expected sub-assets with no ignored warnings; editor and player captures match expected geometry/poses; mobile texture export completes; existing glTF tests remain green.
- Run normal module tests on null RHI for the conversion/database/cooking portions, and use engine screenshots for visual material/animation checks. Include platform compilation checks for the new parser dependency; do not assume desktop-only linkage.

### Gaps

- The task supplied no target exporter, sample FBX files or required feature subset. Those choices materially affect the estimate.
- No tests/builds were run because engine code was not changed. No runtime, performance or cross-platform success is claimed.
