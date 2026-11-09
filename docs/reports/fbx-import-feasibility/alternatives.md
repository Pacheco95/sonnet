# FBX importer alternatives and scope

Research date: 2026-11-09. This is a source-based feasibility assessment, not a benchmark or compatibility certification. No library was built or imported into Sonnet for this subtask. The separate Sonnet and ufbx notes contain repository findings and the primary candidate assessment.

## Which importer alternative fits Sonnet?

### Takeaway

Assimp is a viable alternative when support for many additional source formats is an actual requirement. For an FBX-only addition, prefer evaluating ufbx first. The Autodesk SDK and FBX2glTF introduce packaging or licensing considerations without eliminating the work of translating imported data to Sonnet's representation.

### Cited Findings

- **Assimp is a portable, broad importer:** its upstream README describes more than 40 input formats, C/C++ APIs, Android and iOS support, and postprocessing for triangulation, normals, tangents, duplicate vertices and other mesh cleanup. Its format list includes FBX. These are project claims rather than tests performed against Sonnet. [Assimp README](https://github.com/assimp/assimp/blob/master/Readme.md), [format list](https://github.com/assimp/assimp/blob/master/doc/Fileformats.md).
- **Assimp's FBX path has substantial capabilities:** configuration exposes materials, embedded textures, cameras, lights, animation and weights. Pivot preservation is enabled by default through additional nodes; disabling it evaluates pivots/offsets where possible. Settings also cover centimeter-to-meter conversion and axis handling. The strict-mode documentation identifies FBX 2013 as the primary tested target, but strict mode defaults to false: this is not evidence that newer FBX files are unsupported. [Assimp configuration header](https://github.com/assimp/assimp/blob/master/include/assimp/config.h.in).
- **Assimp has straightforward licensing and CMake integration:** the main license is the three-clause BSD license, with notices required for distribution; bundled components have their own terms. CMake requires C++17 and C99 and offers switches to disable exporters and most importers. That meets Sonnet's language level in principle. Test models under `test/models-nonbsd` have separate terms; do not assume the library license covers every fixture. [Assimp LICENSE](https://github.com/assimp/assimp/blob/master/LICENSE), [build instructions](https://github.com/assimp/assimp/blob/master/Build.md).
- **Assimp is available in vcpkg:** the retrieved package page lists 6.0.5, all triplets, optional Draco support and dependencies including zlib, pugixml, rapidjson, poly2tri, polyclipping, minizip, kubazip, stb and utfcpp, plus CMake helpers. The standard port does not offer a small FBX-only feature; it configures the normal broad build. This is current registry metadata, not proof of which version Sonnet's pinned baseline selects or of successful builds on Sonnet's triplets. [vcpkg Assimp package](https://vcpkg.io/en/package/assimp.html), [port configuration](https://github.com/microsoft/vcpkg/blob/master/ports/assimp/portfile.cmake).
- **Autodesk SDK is a vendor C++ option:** the current official landing page lists Windows, macOS, iOS and Linux, displays a 2020.3.11 download heading, and states that free use is subject to the accompanying EULA. It does not list Android in the supported-platform summary. This absence does not prove Android is technically impossible, and old headers containing Android macros are not proof of a currently supplied Android SDK. [Autodesk FBX SDK](https://aps.autodesk.com/developer/overview/fbx-sdk).
- **The Autodesk SDK's proprietary code affects converter distribution too:** Godot's FBX2glTF fork identifies its own tool code as BSD-3-Clause but explicitly states that its binaries contain proprietary Autodesk SDK code under Autodesk's license. [Godot FBX2glTF README](https://github.com/godotengine/FBX2glTF/blob/master/README.md).
- **Current engine practice supports native ufbx integration:** Godot's stable docs identify ufbx as the default FBX method and recommend it over its earlier external FBX2glTF workflow. This is evidence of real engine use, not proof that Sonnet can reuse the adapter unchanged. [Godot supported 3D formats](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html).

### Inferences

- Assimp is a reasonable second choice, not a fundamentally incapable FBX loader. Its extra formats and common scene API become valuable if Sonnet actually plans OBJ/DAE/etc. support; otherwise its standard dependency/build surface is broader than the stated need.
- An FBX-only Assimp build is possible upstream but may require a tailored vcpkg overlay. This adds packaging work and should not be represented as a reason to replace Sonnet's working glTF importer.
- The vendor SDK could be an option for a demonstrated exporter-specific case that other readers fail. Its license, prebuilt toolchain/platform packages and distribution terms need deliberate integration. No legal incompatibility with Sonnet has been established here.
- Android availability matters if the importer remains in Sonnet's asset target built for every platform. It would not block Android games that consume bundles cooked on a supported desktop, provided import code is actually separated into host tooling. Do not conflate SDK availability with runtime asset portability.

### Gaps

- No controlled Assimp/ufbx/Autodesk fidelity or performance comparison was run on the same Sonnet corpus. Do not claim one is quantitatively faster, smaller or more correct.
- Exact Assimp version at Sonnet's pinned registry baseline was not verified by this researcher.
- Autodesk's current binary/toolchain combinations and full EULA were not audited. The notes make no legal conclusion or guarantee of Android availability.

## What can users do before native FBX support exists?

### Takeaway

Importing an FBX into Blender, checking it, and exporting a `.glb` is a practical immediate workflow. FBX2glTF can also produce glTF, but its upkeep and SDK dependency make it a weak choice for a new mandatory built-in conversion stage.

### Cited Findings

- Godot's native-import announcement explicitly describes the previous practice of importing troublesome FBX characters into Blender and re-exporting glTF 2.0. It also documents why changing importer can change hierarchy, rest poses and material ordering. [Godot ufbx introduction](https://godotengine.org/article/introducing-the-improved-ufbx-importer-in-godot-4-3/).
- Blender's glTF exporter supports meshes, materials, textures, cameras, punctual lights and keyframe/shape-key/skinning animation; it triangulates polygons and splits vertices at attribute discontinuities. Its supported material inputs follow glTF's material model rather than arbitrary DCC shader graphs. Export mode, action association and NLA organization determine which animation clips are written. The documentation warns that readers differ in extension support. [Khronos glTF Blender exporter documentation](https://github.com/KhronosGroup/glTF-Blender-IO/blob/main/docs/blender_docs/scene_gltf2.rst).
- FBX2glTF can output `.glb` and bakes animation transforms; its documentation also describes blend-shape conversion. Baking can increase file size. Its material section acknowledges differences between legacy Lambert/Phong and metal/rough PBR and describes its PBR extraction as exporter-dependent. That README contains obsolete extension-status and build instructions, so do not treat all of it as current format guidance. [Original FBX2glTF README](https://github.com/facebookincubator/FBX2glTF/blob/main/README.md).
- Godot's FBX2glTF fork says it is no longer actively maintained after the move to ufbx. It offers last-built binaries for desktop Linux, macOS and Windows, and exposes four-weight and animation-bake-rate options. [Godot FBX2glTF README](https://github.com/godotengine/FBX2glTF/blob/master/README.md).

### Inferences

- For immediate asset use: open the FBX in Blender, inspect scale/orientation/materials/animation, export uncompressed GLB using features Sonnet currently imports, and inspect the result in Sonnet. This requires no new engine importer, but it adds an artist-controlled conversion step.
- Conversion does not automatically preserve arbitrary shaders, controllers, constraints, animation layers or full DCC scenes. If a feature is outside the target engine's data model, it still needs baking, approximation, explicit rejection or omission with diagnostics.
- Automatically invoking a converter would add installation/path/version management, process handling and conversion caching. It saves writing an FBX parser but does not remove fidelity and stable reimport policy work.

### Gaps

- No specific user FBX asset was available to establish that Blender conversion preserves its desired content.
- The latest Blender FBX manual page returned a retrieval error. Avoid stale claims that Blender only supports binary FBX or cannot import a particular feature; this investigation did not verify the exact current importer matrix.
- No conversion was run, and no required glTF extension should be enabled until Sonnet's existing support is checked.

## What should the first release promise?

### Takeaway

Promise a tested game-asset subset, with documented limits and diagnostics. A library exposing a field does not mean Sonnet can faithfully render, animate or edit the corresponding DCC feature.

### Cited Findings

- Godot's experience identifies units, geometric pivots, nonstandard transform inheritance, material ordering and bone rest/bind poses as concrete integration concerns even with a capable FBX reader. Its implementation sometimes creates helper nodes for transforms that ordinary translation/rotation/scale nodes cannot express directly. [Godot ufbx introduction](https://godotengine.org/article/introducing-the-improved-ufbx-importer-in-godot-4-3/).
- Assimp exposes policy switches for pivots, axes and empty animation curves; FBX2glTF exposes baking and material conversion choices. These are evidence that representation and conversion policy sit above parsing. [Assimp configuration header](https://github.com/assimp/assimp/blob/master/include/assimp/config.h.in), [FBX2glTF conversion documentation](https://github.com/facebookincubator/FBX2glTF/blob/main/README.md).

### Inferences

- Proposed first increment: static mesh assets with triangulation, normals/tangents, material assignments, supported UV channels, textures and deterministic units/axes. Preserve mesh instancing/hierarchy where the current Sonnet model allows it; bake or reject unsupported transforms explicitly. Add skinning/clips/morphs as a second acceptance gate instead of making an unbounded all-FBX promise.
- A release described as “character import” should also prove bind poses, deformation across clips, root and intermediate-node transforms, multiple takes, morph animation and texture lookup on real exports. Importing a static rest pose is insufficient evidence.
- Recommend fixtures exported from Blender plus at least one Autodesk DCC and a representative character source, covering binary/ASCII where supported, centimeters/meters, Z-up/Y-up, negative/nonuniform scale, pivots, UV seams, multiple materials, external/embedded textures and missing resources. Test reimport identity and editor/cook equivalence, not just successful parsing.
- Treat advanced material graphs, procedural/non-mesh geometry, live constraints/control rigs, retargeting, animation-layer editing, arbitrary property tracks, camera/light fidelity and FBX export as separate scope unless a concrete acceptance case requires them. Baked results may fit existing structures even where original authoring semantics do not.
- Use the separate Sonnet analysis for exact limits (reported to this researcher: four skin influences, eight morph targets, two UV sets, TRS node transforms). Do not present these numbers as upstream FBX limitations.

### Gaps

- The user's actual asset producers and required FBX features are unspecified. The recommendation assumes game-model import and is intentionally conditional rather than universal format compatibility.
- No quantitative effort estimate can be derived from upstream feature lists; use the repository analysis's clearly labeled engineering estimates.
