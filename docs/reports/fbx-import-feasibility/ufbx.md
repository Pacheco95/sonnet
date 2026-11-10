# ufbx feasibility for Sonnet

## Does ufbx cover the FBX data Sonnet needs?

### Takeaway

ufbx is a strong candidate for a native FBX source importer. Its parsing and conversion utilities address the difficult FBX mechanics, but Sonnet still needs deliberate policies for materials, transform helpers, skin limits and morph semantics; accepting a file does not imply exact DCC appearance or animation parity.

### Cited Findings

- The project supports binary and ASCII FBX from version 3000, including meshes, skinning, blend shapes, embedded textures, animation evaluation and PBR interpretation. Its documented goal is FBX SDK feature parity, not a guarantee that every SDK feature is already reproduced. [Upstream README](https://github.com/ufbx/ufbx)
- The changelog identifies files below 3000 or above 7700 as unsupported versions that receive diagnostics. This is a better bounded compatibility statement than promising every past/future FBX version. [Changelog](https://github.com/ufbx/ufbx/blob/main/misc/changelog.md)
- Meshes expose independently indexed position/normal/UV/color attributes. `ufbx_triangulate_face()` handles polygon triangulation, and `ufbx_generate_indices()` can deduplicate converted vertices. Material parts identify face groups; node materials should be preferred to mesh materials because instances can override them. [Meshes](https://ufbx.github.io/elements/meshes/)
- Nodes preserve parent/child hierarchy and shared mesh instances. Conversion options cover target axes, units and handedness. Local transforms are TRS, while convenience matrices expose composed transforms. Geometry and scale helper nodes can adapt FBX-specific behavior to ordinary scene graphs. [Nodes](https://ufbx.github.io/elements/nodes/)
- FBX transform semantics include rotation order, pre/post rotation, separate rotation/scaling pivots and offsets. The library recommends consuming its computed representations rather than reconstructing these properties manually. [Node transforms](https://ufbx.github.io/fbx/node-transforms/)
- `ufbx_bake_anim()` converts stacks of layered animation into linear translation, quaternion rotation and scale tracks, also baking non-transform properties. Resampling controls handle cubic curves and Euler rotations; the default avoids unnecessarily resampling already dense keys. [Animation](https://ufbx.github.io/elements/animation/)
- Skin clusters expose mesh-to-bone bind transforms and influences sorted by weight. Skinning modes include rigid, linear, dual quaternion and blended linear/DQ. Blend channels may contain in-between shapes; the convenience `target_shape` is only the final shape. Morph position/normal offsets are sparse, and normal offsets may be absent. [Deformers](https://ufbx.github.io/elements/deformers/)
- Nonuniform or animated scale compensation can require helper nodes; the no-fallback modes sacrifice correctness. PBR properties can be approximate. Textures expose paths or embedded bytes; layered/procedural/shader textures require separate interpretation. Blender PBR recovery is opt-in through `use_blender_pbr_material`. [API reference](https://ufbx.github.io/reference)

### Inferences

- Proposed conversion sequence: normalize to Sonnet's axes and units; retain geometric/scale helper nodes when needed; triangulate material parts; expand per-corner attributes into Sonnet's GPU vertex layout; preserve source vertex mapping for skin and morph data; then use existing mesh cooking. This is an integration design, not verified code.
- Avoid blindly decomposing arbitrary world matrices back into a single TRS node. A TRS hierarchy can produce shear through composition; flattening that matrix or dropping helper nodes may change the result. Nonuniform scales, unusual inheritance, pivots and mirrored instances need reference-image and animated-pose fixtures.
- Bake animation during import. Sonnet should consume its normal runtime clips rather than carry FBX curve/layer evaluation into the player. Keep the sample rate and tolerance explicit; test rotations spanning more than 180 degrees, negative scale and stack time offsets.
- The parent researcher's verified Sonnet limits (four influences, eight morph targets, two UV sets) require truncation/rejection policies with diagnostics. Four-influence linear skinning cannot preserve dual-quaternion deformation exactly. Prefer an explicit unsupported-mode warning over claiming full skeletal fidelity.
- Progressive/in-between shapes need flattening into supported independent targets and remapped weights or an explicit exclusion. Simply selecting each channel's final shape loses intermediate sculpt behavior. Missing morph normals may need regeneration for matching shading.
- Material support should promise a defined subset: base color, normal, metallic/roughness, opacity/emission where representable. It cannot promise arbitrary DCC shader graphs. Separate roughness/metalness texture inputs may require repacking into Sonnet's expected channels, and texture file resolution/decoding remains importer work.

### Gaps

- No representative user FBX corpus was provided, so exporter-specific fidelity remains unmeasured. Blender, Maya, 3ds Max and a typical rigged asset source should each have fixtures before claiming broad support.
- No import/load probe or visual comparison was completed in this investigation. Findings above are upstream documentation/source inspection and proposed integration policies.

## How would ufbx fit Sonnet's dependency and platform setup?

### Takeaway

The library is a small portable source dependency with an MIT option, but it is not an existing vcpkg dependency at Sonnet's pinned baseline. Adding the string `ufbx` to the manifest alone is not an implementation plan.

### Cited Findings

- ufbx consists of `ufbx.c` plus `ufbx.h`, compiled as C99 or C++11 and later. The MIT or Unlicense alternatives permit choosing the MIT terms and carrying the required notice. [Getting started](https://ufbx.github.io/getting-started/); [license](https://github.com/ufbx/ufbx/blob/main/LICENSE)
- The checked upstream header reports version **0.23.1**. Its changelog dates that version **2026-09-24**. These were checked on 2026-11-09; they describe the observed upstream state rather than a future release promise. [Header](https://github.com/ufbx/ufbx/blob/main/ufbx.h); [changelog](https://github.com/ufbx/ufbx/blob/main/misc/changelog.md)
- Upstream lists CI on Windows MSVC/Clang/MinGW, macOS Clang/GCC, Linux including x86 and ARM variants, and WASI. The list does not establish tested Android or iOS integration for Sonnet. [README platform section](https://github.com/ufbx/ufbx#platforms)
- Optional subdivision, NURBS tessellation, geometry cache, skin evaluation, animation baking, triangulation and OBJ features can be compiled out. There is no need to disable animation baking or triangulation for the proposed importer. [Build options](https://ufbx.github.io/advanced/build-options/)
- Sonnet pins builtin baseline `a1cae005c39be7b18ba319fced856b68d7276271`. Local read-only `git show` and `git ls-tree` in a local vcpkg clone verified that baseline contains neither `ports/ufbx` nor `versions/u-/ufbx.json`. It does contain `ports/openfbx`. [Sonnet manifest, line 5](../../../vcpkg.json); [exact upstream baseline tree](https://github.com/microsoft/vcpkg/tree/a1cae005c39be7b18ba319fced856b68d7276271/ports)
- Current upstream `ports/ufbx/vcpkg.json` and `ports/ufbx/portfile.cmake` URLs both returned 404 during this investigation. No current official ufbx port was found. [Manifest location checked](https://raw.githubusercontent.com/microsoft/vcpkg/master/ports/ufbx/vcpkg.json); [portfile location checked](https://raw.githubusercontent.com/microsoft/vcpkg/master/ports/ufbx/portfile.cmake)
- Sonnet defaults to vcpkg manifest dependencies and permits `FetchContent` when a library lacks a port; a dependency must never be supplied by both. [Build dependency policy, line 50](../../build.md); [project guidance, line 102](../../../AGENTS.md)

### Inferences

- A small version-pinned local overlay port is consistent with the existing dependency workflow. A pinned `FetchContent` dependency plus a small CMake library target also fits the documented no-port exception. Choose one, record why, and keep upstream source unmodified.
- Version 0.23.1 is a reasonable candidate to validate, particularly because it incorporates the recent parser fixes described below. Pin a tag/commit and content hash rather than tracking a moving branch.
- C++23 itself presents no apparent language-version blocker. Desktop compilation and Sonnet link integration still require verification. Pure-source portability is encouraging for mobile builds but is not evidence that Sonnet's mobile toolchains have already passed.
- Keep FBX decoding in source import/cooking. Even if the present `assets` module build links the library on more targets, the player should continue loading the existing cooked payloads rather than require FBX files at runtime.

### Gaps

- The optional `/tmp` download attempt failed at DNS resolution in the sandbox before fetching any source. No compiler invocation or FBX parse was run. No package installation or engine modification was attempted.
- Library code/binary size and import throughput were not benchmarked. Single-source packaging should not be equated with a measured runtime or binary cost.
- If implementation starts later, recheck port availability; the negative current-port check is time-dependent.

## What confidence and failure-handling evidence exists?

### Takeaway

There is credible adoption and robustness evidence, but importer-specific tests remain necessary. Pin a current release and propagate parse warnings/errors through Sonnet's existing asset error reporting.

### Cited Findings

- Godot uses ufbx as the default importer for newly added FBX files from version 4.3, and recommends it over its older FBX2glTF workflow. [Godot supported formats](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html#importing-fbx-files-in-godot)
- Upstream reports sanitized scenes, malformed-input/out-of-memory handling, multiple fuzzing layers and broad fixture datasets. These are upstream testing claims, not tests reproduced here. [README testing](https://github.com/ufbx/ufbx#testing)
- Version 0.23.1 fixes buffer/integer overflows, malformed UTF-8 access, truncated compressed data handling and recursive XML cache file loading. That supports staying current; it does not establish absence of remaining defects. [Changelog](https://github.com/ufbx/ufbx/blob/main/misc/changelog.md)
- The API provides temporary/result allocation limits, hierarchy-depth limits and cancellable progress callbacks. External-file auto-loading is separate, applies to cache/OBJ references and warrants an explicit policy. Parse diagnostics are available as errors and metadata warnings. [API reference](https://ufbx.github.io/reference)

### Inferences

- Minimal acceptance should cover ASCII/binary, triangulation and seams, per-instance materials, unit/axis conversion, pivots/geometric transforms, negative/nonuniform scale, skinned bind pose, multiple animation takes, in-between morph handling, embedded/external images and malformed input. Compare cooked and source-import results.
- Establish bounds compatible with existing asset jobs and expose cancellation where the editor already supports it. Avoid enabling unrelated cache-file loading for a model importer.
- Use small original or clearly redistributable FBX fixtures in Sonnet tests. Upstream's existence of a large data directory is not permission to copy every third-party asset in it.

### Gaps

- No audit of upstream fixture licensing was performed. Select fixtures individually or generate project-owned ones.
- Neither all-platform builds nor integration tests were run because this is a feasibility investigation, not an implementation.
