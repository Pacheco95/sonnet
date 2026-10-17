# Assets

The `assets` module owns asset identity, the database that resolves ids to loaded objects, the importers, hot reload, and cooking for export.

## Identity

Every asset has a UUID assigned at import and stored in a sidecar file next to the source (`hero.gltf` and `hero.gltf.meta`). Scenes and components reference assets by UUID, never by path, so files can be moved or renamed without breaking references. Paths are for humans and for the importer only.

An asset may produce sub-assets: a glTF file yields meshes, materials, textures, skins and animation clips, each with its own stable UUID derived from the parent's UUID and the sub-asset name (`core::Uuid::derive`, `mesh/0`, `material/2`, `image/1`, `skin/0`, `animation/1`) so re-import keeps them. The twelve built-in primitive meshes have fixed identities of the same kind (`assets::builtin::box()` and the others), so a scene made of primitives references them like any other mesh. They are box, sphere, plane, cylinder, capsule, cone, torus, ramp, stairs, hemisphere, arch and icosphere, and `assets::builtin::all()` lists them in menu order for the database and the hierarchy. Adding a shape is a generator in `renderer::primitives`, an identity and an entry there, and a branch in `AssetDatabase::mesh`; the identity is never renamed, since scene files hold it.

The sidecar is JSON: a `version`, the `uuid`, the asset `type`, the import `settings`, and for a glTF file a hash of the file's content and the list of its sub-assets with their names and, for a mesh, its default material per slot, so opening a project does not parse every model and a sidecar committed with the project stays valid in every checkout. Version 2 of the sidecar lists a model's skins and clips among the sub-assets; a glTF file whose sidecar is older is parsed again on the next scan and its list rewritten. A sidecar with a newer version than the engine knows is logged and skipped, its file untouched, so an older engine never rewrites it without the fields it cannot read. Sidecars belong in version control next to their sources.

## Source and cooked

- **Source assets** are what the user edits: glTF, PNG, HDR, `.slang`, `.lua`, WAV and the other sound files, TrueType and OpenType fonts, JSON scenes. They live in the project folder and are what the editor imports.
- **Cooked assets** are what the player loads: engine-native binary formats laid out for direct upload. They are produced by the cook step for a target platform and packed into a bundle. The editor cooks on demand into a cache folder so it uses the same loading path as the player.

## Database

`AssetDatabase` is constructed on a renderer and a `core::JobSystem`, which carries its asynchronous requests and has to outlive it; a pool with no workers imports on the thread that drains it, which is what `sonnet_cook` and the tests use. It maps UUIDs to metadata (`AssetInfo`: type, source path, name, parent, default materials) and to loaded renderer objects and data. `open` scans the project's asset roots, writes the sidecars that are missing and registers every file whose extension has an importer; `assets` lists them by type for the editor's browser. `open` closes the project that was open first, and `close` releases everything it loaded, materials included: a material kept across projects would still read the textures the close destroyed. Loading runs on the main thread, on first use: `mesh`, `texture`, `material` and `environment` import what they are asked for and return the renderer's handle, or an invalid handle when the import failed, which is logged once and not retried until a re-import. `script` returns a script's source text with a revision that changes on every read, so the scripting runtime knows when to reload ([scripting.md](scripting.md#errors-and-hot-reload)), `sound` the still encoded bytes of a sound file with a revision of the same kind, which `audio` decodes ([audio.md](audio.md#miniaudio)), and `font` a font file's bytes, its preload ranges and a revision, which `gui` rasterises glyphs from ([gui.md](gui.md#text)); `skin` and `animation` return a model's skins and clips, loaded with the rest of their glTF file, with a revision that changes when the file is re-imported so the bindings made from them are rebuilt ([world.md](world.md#animation)). `model` returns a model's node hierarchy, and for a source glTF it reads that from the file's JSON alone, without loading a buffer or an image: every model is placed as a prefab when a project opens, and a prefab needs nothing else. The nodes are the full import's index for index, since both walk the file through one function, and the full import replaces the hierarchy with an identical one when it arrives. From a bundle the model is a payload of its own and was never tied to its meshes. `createScript` writes a new `.lua` file and registers it, as `createMaterial` does for materials. Both go through `registerFile`, which registers a source file under one of the asset roots as `open` would have found it, writing its sidecar when it has none (a file outside the roots is refused, since the next `open` would not find it), and `sourceType` says from a file's extension whether, and as what, the database imports it. `unregister` is the other way: it forgets a file asset and its sub-assets, destroys what was loaded from them, materials included, and resolves the materials that read one of its textures again, so they fall back rather than read a destroyed texture; the files stay. The editor's import uses the pair ([editor.md](editor.md#importing-assets)). A glTF file's sub-assets are imported together the first time any of them is asked for. The draw list resolves mesh identities through the database every frame, so a swap under an identity reaches the next frame without anyone holding a stale handle.

`move(from, to)` moves a source file with its sidecar, or an entire folder, preserving UUIDs,
sub-asset names, loaded handles and modification times. It waits for affected imports before
moving and updates their source paths; a moved script gets a new revision so its chunk name
and log links follow the file. Destinations must be inside the project's asset roots, with
scene and prefab files also allowed elsewhere inside the project. A folder can also be renamed
in place elsewhere in the project, provided it neither is nor contains a configured asset root;
the project root and `.sonnet` remain protected. This allows the browser's project-level folders
to be renamed without moving imported assets outside their roots. Existing destinations,
extension changes, and moving a glTF with external dependencies without its folder are refused.
Case-only renames use a temporary path. `unregister` also removes every `<uuid>*.ktx2` cache
entry for the file and its sub-assets, including mobile variants; source files and sidecars stay.
`Project::files` excludes `.sonnet`, including cached files and deleted files awaiting undo.

`requestMesh`, `requestTexture`, `requestSkin` and `requestAnimation` are the asynchronous form ([ADR-0013](decisions/0013-job-system.md)). A loaded asset comes back at once; anything else returns an invalid handle and schedules the import on the job system, so the caller draws nothing for it this frame and asks again on the next. The import runs on a worker — parsing, decoding, mip generation, KTX2 cooking, none of which touches the renderer — and a main-thread job creates the renderer objects, since recording an upload is not thread-safe. The application publishes them by calling `core::JobSystem::runMainThreadJobs` once a frame, before the draw list resolves identities. Asking again while a request is in flight joins it rather than importing twice, and a glTF file is one request for the whole file, so a mesh and its textures arrive together exactly as the synchronous path loads them. `loading` says whether any request is outstanding, which is what a loading screen waits on, and `waitForLoads` runs them all to completion for a caller that wants the synchronous behaviour back; `close` calls it, so a database is never torn down under a job still writing into it.

Materials request their textures rather than load them, so neither `material` nor an edit through `setMaterialSource` imports an image on the main thread: the material is created at once and its slots fill in as the textures publish. A slot whose texture is still importing reads the renderer's fallback, white or a flat normal, which already means "no effect" — except the base colour, which reads a neutral grey placeholder the database creates on first use, since white would show the material's untextured colour as though it were finished. A texture that failed keeps the fallback, so pending and failed look different. The main-thread job that publishes a texture, or a glTF file's images, resolves every loaded material reading it again, which is also what replaces the placeholder with the fallback when an import fails. A glTF file's own materials never see the placeholder, since its images are published before its materials resolve. A synchronous load that meets a request in flight for the same file — `texture` or `mesh` while a draw list's request is importing, or a re-import — waits for it and publishes it rather than importing the file a second time.

`buildDrawList` uses the request form, so an entity whose mesh is not in memory yet draws nothing for a frame or two instead of stalling the one that asks ([world.md](world.md#draw-list)), and so do the animation systems for skins and clips ([world.md](world.md#animation)). Together with `model` reading only the hierarchy, opening a project or a scene imports no glTF file on the main thread: the prefabs exist at once, complete as entities, and their meshes, textures, skins and clips arrive on the frames after. On the basic sample, placing its three models went from 0.38 ms with a warm cache and 11.2 ms on a first open, which cooks the textures, to 0.023 ms either way; `assets_tests "[benchmark]"` measures it. The sample's files are small, and what the old path cost grew with a file's images while the new one grows only with its JSON.

## Importers

| Source | Importer | Produces |
|---|---|---|
| glTF 2.0 (`.gltf`, `.glb`) | fastgltf | Meshes (one submesh per triangle primitive, flat normals and tangents generated when missing), PBR materials, the images they use, skins, animation clips, and a `Model` with the node hierarchy and its perspective and orthographic cameras and punctual lights that `world` turns into a prefab |
| PNG, JPEG, TGA, BMP | stb_image | Textures, decoded to RGBA8 with a box-filtered mip chain and cooked to KTX2 |
| `.hdr` | stb_image | Environments, decoded to RGBA16F for the renderer's cubes |
| KTX2 | KTX-Software | Textures, used directly; Basis Universal data is transcoded on load, and a format the device cannot sample fails the import |
| `.material.json` | assets | Materials ([Materials](#materials)) |
| `.slang` | Slang, through `editor::ShaderCompiler` | SPIR-V modules for the editor's shader hot reload ([rendering.md](rendering.md#shaders)) |
| `.lua` | assets | Script assets: the source text, run by `scripting` ([scripting.md](scripting.md)) |
| `.wav`, `.ogg`, `.mp3`, `.flac` | assets | Sound assets: the encoded file, decoded and played by `audio` ([audio.md](audio.md)) |
| `.ttf`, `.otf` | assets | Font assets: the file, parsed and rasterised on demand by `gui` ([gui.md](gui.md#text)); the sidecar's `preload` ranges say which codepoints to rasterise at load ([Fonts](#fonts)) |
| `.scene.json`, `.prefab.json` | world | Scenes and prefabs |

The glTF importer reads eight extensions, `KHR_materials_emissive_strength`, `KHR_materials_transmission`, `KHR_materials_unlit` and `KHR_texture_transform` ([Materials](#materials)), `KHR_mesh_quantization`, `EXT_meshopt_compression` and `KHR_texture_basisu` ([Textures](#textures)), and `KHR_lights_punctual` ([Skins and animations](#skins-and-animations)); `ReadExtensions` in `GltfImporter.cpp` lists them. `KHR_mesh_quantization`, which gltfpack's output requires, stores positions, normals, tangents and UVs as 8- or 16-bit integers, normalized or not; the importer converts them to floats as it reads every accessor, and the scale and offset that undo the quantization are the node's transform, imported as any other. `EXT_meshopt_compression` is the geometry compression of gltfpack's `-cc` and of many three.js and Sketchfab downloads: before any accessor is read, each compressed buffer view is decoded with meshoptimizer into bytes of its own, its filter (octahedral, quaternion or exponential) undone, and every accessor read goes through a fastgltf buffer data adapter that hands it those bytes. The extension's fallback buffer, which has no `uri`, is never loaded, nor is it a dependency of the file. Compressed data that does not decode makes the import fail, naming the buffer view. A file whose `extensionsUsed` names others still imports, with one warning per import naming the file and every extension it does not read, so a model that looks wrong says why. A file whose `extensionsRequired` names one is refused, as the glTF specification asks, and the error names it.

Import settings live in the sidecar file and are edited in the inspector. A texture's are `TextureSettings`: sRGB or linear, mipmaps, compression; changing them rewrites the sidecar and re-imports at once. A glTF file's are `ModelSettings`: `lightScale`, the factor its punctual lights' intensities are imported with ([Skins and animations](#skins-and-animations)); `AssetDatabase::setModelSettings` rewrites the sidecar and re-imports the same way. The importers are also plain functions in `Importers.h` (`importImage`, `importHdr`, `importGltf`, `readKtx2`, `markKtx2`, `cookKtx2`, `cookAstcKtx2`, `cookHdrKtx2`) for tools and tests.

## Fonts

A `.ttf` or `.otf` file is a Font asset with a sidecar like any other. Its settings are `FontSettings`: `preload`, a list of `[first, last]` codepoint ranges that `gui` rasterises when the font loads, on the job system, so the text of a game does not hitch on its first frame. The default is Basic Latin and Latin-1, `[[32, 126], [160, 255]]`; a font for a script that uses other characters lists their ranges, and an empty list rasterises every glyph when it is first drawn. The settings are edited in the `.meta` file, and the inspector shows them. A font has no importer: `AssetDatabase::font` returns the file's bytes, read on first use and again, under a new revision, when it changes, and `gui` parses it ([gui.md](gui.md#text)).

## Textures

Import decodes to RGBA8, generates mipmaps, and cooks to KTX2 in the project's cache (`.sonnet/cache/<uuid>.ktx2`, ignored by git), which is rebuilt when the source or its sidecar is newer. A compressed texture is stored as Basis Universal UASTC with zstd supercompression, the portable form, and transcoded on load to BC7 where the device supports block compression (`DeviceInfo::blockCompressionSupported`: desktop GPUs and Lavapipe), to ASTC 4×4 where it has ASTC but no BC (`astcSupported`), and to RGBA8 where it has neither. UASTC is a restricted form of ASTC 4×4, so the second costs a phone 8 bits per texel rather than RGBA8's 32. The UASTC encode runs on a thread pool of Basis Universal's own, one thread per core, created and torn down around each texture; the `ktx` port patches its shutdown, which could hang ([ports/README.md](../ports/README.md)). An uncompressed texture is stored as plain RGBA8. Colour textures are sRGB, data textures (normals, roughness, metallic, occlusion) are linear; the glTF importer decides per image from how the materials use it, and a file texture's sidecar says.

A glTF texture under `KHR_texture_basisu` reads the extension's image, a Basis Universal KTX2 file (ETC1S or UASTC), and its own `source`, when it has one, is only the fallback for clients without the extension, as the extension specifies. The importer marks an image as KTX2 by its file identifier (`GltfImage::ktx2`), and the KTX2 image is not decoded and encoded again: `markKtx2` copies it with its data format descriptor's transfer function set to sRGB or linear, from how the materials use it like any other glTF image, since the file's own may disagree with the slot, and that copy is the cache entry `readKtx2` transcodes. The levels stay as the file has them, so a KTX2 image without a mip chain gets none. The cook passes the same entry through to every platform, a mobile bundle included, since Basis Universal transcodes to ASTC on a phone as a KTX2 file texture does; the bundle format does not change. A fallback image no material reads is imported all the same, as any unused image is.

A mobile bundle carries ASTC instead of UASTC ([ADR-0018](decisions/0018-mobile-export.md#textures)). `AssetDatabase::mobileTexture` encodes a compressed texture's RGBA8 mip chain, decoded from its source, with KTX-Software's `ktxTexture2_CompressAstcEx`, never from the UASTC in the cache, which would stack two lossy encodings. An sRGB texture is 6×6 blocks (3.56 bits per texel) in perceptual mode, and a linear one 4×4 (8 bits per texel, as BC7), since its error turns into shading error; both are at astcenc's medium quality, without its normal-map mode, which keeps two channels where `forward.slang` reads three. The result is cached as `.sonnet/cache/<uuid>.astc.ktx2` under the UASTC entry's rule: encoded again when the source or its sidecar is newer, and a glTF file's images all at once, since reading one imports the file. A second mobile cook reads the cache, and Android and iOS cook the same bytes. An uncompressed texture is RGBA8 on every platform, a KTX2 source goes in as it is, and an environment uses the portable UASTC HDR payload ([Environments](#environments)). A 2048×2048 colour map encodes in 0.01 to 0.4 s on 16 threads. `readKtx2` uploads an ASTC file as it is, and a KTX2 file in a format the device cannot sample (`rhi::formatSupported`) is an error at load, which falls back as a failed import does ([Database](#database)). `assets_tests` decodes an encode of each kind back with `ktxTexture2_DecodeAstc` and holds it above a PSNR floor, on two 512×512 maps in `modules/assets/tests/data` ([README](../modules/assets/tests/data/README.md)).

## Thumbnails

The editor's Assets panel shows a picture for each texture, environment, material, model, mesh and prefab ([editor.md](editor.md#asset-browser)). `AssetDatabase::thumbnailRequest` copies what one needs out of the database (a texture or an environment that is a file of its own, other than a KTX2 one, a material file, a model, or a mesh, which is a glTF sub-asset as old as its file or a built-in one; the other sub-assets and types have none), and `makeThumbnail` turns the request of a texture or an environment into one RGBA8 level on any thread. A texture's is the largest level of its mip chain whose longest side is at most `ThumbnailSize` (128), taken from the source rather than from the cooked entry, whose compressed form a CPU cannot read back; an environment's is its equirectangular map box-filtered to 128 pixels wide, Reinhard tone mapped and sRGB encoded. The bytes are those ImGui shows, so the format is Unorm even for a colour texture, and a linear texture (a normal map) is shown as stored.

The picture is cached as `.sonnet/cache/<uuid>.thumb.ktx2`, an uncompressed KTX2 file read back by `readKtx2`, and made again when the source or its sidecar is newer, as the cooked entry is. Unregistering the asset removes it with the other cache entries of that identity ([Database](#database)). A bundle has none: thumbnails are an editor feature.

A material file has a request too, whose `dependencies` are the source files and sidecars of the textures it reads, and its entry is as old as the newest of them. A model, a mesh and a prefab (`ThumbnailRequest::prefab`, which the editor makes from the prefab file, its nested prefabs and the files of what it draws, since a prefab is not an asset) are as old as their file, sidecar and dependencies. `makeThumbnail` has no file to make these pictures from, which the editor renders ([editor.md](editor.md#rendered-pictures); `isRendered` says which), so the job system only reads the entry (`cachedThumbnail`) and writes the rendered picture (`storeThumbnail`). A built-in mesh has no file, so its request does not store. A request made while the material has edits that are not saved says not to store (`store`), as the file would not match. `takeChangedMaterials` reports the materials whose look changed since the last call, which is how the editor knows an entry is stale before any file is.

## Environments

An environment is an equirectangular `.hdr` map, decoded by `importHdr` to RGBA16F with no mip chain and given a full RGBA16F mip chain by the renderer for the sky background and converted into lighting cubes ([rendering.md](rendering.md#image-based-lighting)). A cooked bundle holds it as one KTX2 payload instead ([ADR-0024](decisions/0024-temporal-anti-aliasing-and-occlusion-culling.md)): `cookHdrKtx2` builds the same radiance mip chain and encodes every level as Binomial's UASTC HDR 4x4 with zstd supercompression, 8 bits per texel with no alpha, through KTX-Software 5.0.0, the first release that has it ([ports/README.md](../ports/README.md)). UASTC HDR is a 24-mode subset of ASTC HDR 4x4, so it transcodes to BC6H with very little loss and needs no transcoding at all on a device with ASTC HDR. `readKtx2` recognises it by its colour model, not by the library's `NeedsTranscoding`, which says no, since the file is stored as the ASTC HDR blocks it is made of, and picks the target by the same rule as an LDR texture: BC6H where the device has block compression (`DeviceInfo::bc6hSupported`), ASTC HDR 4x4 where it has `textureCompressionASTC_HDR` (`astcHdrSupported`: recent phones and Apple silicon), and RGBA16F where it has neither, so every device runs a bundle's environment, as it ran the raw map. A mobile bundle holds the same payload; it is the one portable form, so the cook does not branch on the platform.

The encoder takes 16-bit components, so the map goes in as the RGBA16F it already is; level 2 of its five quality levels is a good trade for a cook that runs once per environment. The numbers: the basic sample's 256 by 128 sky was 262,264 bytes in the bundle and is 1,216, and its bundle went from 759,199 to 498,151 bytes; the cook of the whole sample takes 0.33 s. Quality is held two ways (`assets_tests "[hdr]"`): the transcode to RGBA16F of a synthetic sky with a sun 3,000 times brighter than the rest stays above 50 dB of PSNR after the Reinhard curve (53.5 dB measured), and a frame of two spheres under the cooked environment, which shows the skybox, the irradiance and the prefiltered cubes, differs from the frame under the uncompressed map by 0.2 levels on average and 5 at worst, through RGBA16F and through BC6H alike, on an RTX 4090 and on Lavapipe, where a flat sky differs by 80. The RTX 4090 and Lavapipe do not have ASTC HDR, so the rhi test that samples a hand-built block of each of the new formats skips that format there. The Galaxy S25 Ultra (Adreno 830) has both BC and ASTC HDR, so a bundle takes BC6H there; with the order of the two targets swapped for one run, the basic sample rendered through ASTC HDR on it and its frame differed from the BC6H frame by 0.03 levels on average and 4 at worst. The [iPhone M13 report](reports/iphone-m13.md) checks Apple A17 Pro on iOS 27.0 with MoltenVK 1.4.2: it reports BC as well as ASTC HDR, so the normal loader takes BC6H. A temporary ASTC-first diagnostic run preserves the encoded blocks and renders within 0.029 RGB levels on average and 4 at worst of BC6H; the diffuse and specular IBL comparisons differ by at most 1. Automatic selection on a device reporting ASTC HDR without BC remains [issue 143](https://github.com/Pacheco95/sonnet/issues/143).

## Meshes

Meshes are uploaded in the vertex layout the renderer pulls from (position, normal, tangent, two UV sets) with a 32-bit index buffer, bounds and a material slot list, straight from the glTF file. The database keeps the CPU copy of every loaded mesh, built-in or imported, which `meshData` returns for physics to build mesh colliders from ([physics.md](physics.md#components)).

`cookMesh` reshapes a mesh for the player: vertices equal to the bit are welded into one, each submesh's triangles are reordered for a sixteen-entry post-transform vertex cache with tipsify (Sander, Nehab and Barczak), and the vertices are renumbered by first use so the fetch runs forwards and vertices no index names are dropped. The result draws the same triangles wound the same way; only their order and numbering change, and a submesh stays one range of the index buffer. `averageCacheMissRatio` measures what that bought: an unwelded grid of quads goes from three misses per triangle, the worst there is, to about 0.65 against a floor of 0.5. A mesh the cook cannot reshape, one whose indices are not whole triangles or name vertices that are not there, comes back as it was. A mesh the cook did reshape gets its coarse shadow range built again over the new indices, and a bundle holds it as a second index array, `shadowIndices`, after the fine one ([Shadow levels of detail](rendering.md#shadow-levels-of-detail); bundle version 4).

## Skins and animations

A glTF skin becomes a `Skin`: the joints of a skinned mesh in the order its weights index, each named by its path of node names from the model's root (`Armature/Hips/Spine`), with the inverse bind matrix that takes the mesh's bind pose into the joint's space. A glTF animation becomes an `AnimationClip`: translation, rotation and scale channels with their keys, each naming its node by the same kind of path, linear (spherical for rotations), stepped or cubic-spline interpolated, and `assets::sample` reads one at a time. A channel's values are a flat run of floats, `width` of them to a value: three for translation and scale, four for a rotation and one per morph target for a `Weights` channel (ADR-0023). A clip also carries its animation events, `{time, name, argument}`, read from the `events` array of the glTF animation's `extras`. Paths, rather than indices or identities, are what survives prefab instantiation and re-import ([ADR-0010](decisions/0010-audio-and-animation.md)); `world` resolves them against the entities of a model's instance ([world.md](world.md#animation)). 

A glTF node's camera becomes `ModelNode::camera`. Perspective cameras keep `yfov` and `znear`; their `zfar` is dropped because perspective projection has an infinite far plane. Orthographic cameras keep `znear` and `zfar`, with `orthoHeight = 2 * ymag`. The viewport decides the aspect, so `aspectRatio` and `xmag` are ignored. Both look along -Z, so the node's transform needs no change. `world` turns the camera into a `Camera` component ([world.md](world.md#prefabs)).

A glTF node's `KHR_lights_punctual` light becomes `ModelNode::light`, its type, colour and intensity, a point or spot light's range, and a spot light's `innerConeAngle` and `outerConeAngle` (glTF's defaults 0 and π/4 when absent), which are half-angles from the axis as the engine's are. Intensity is multiplied by the sidecar's `lightScale` before anything else; a point or spot light's scaled candela becomes the engine's radiance at one metre, and a directional light's scaled lux its intensity, since the engine's falloff is the same inverse square. The scale defaults to `1/683`, written into every new sidecar and assumed when a sidecar has none: Blender's exporter always converts its watts and sun strength to photometric units at 683 lm/W (a 1000 W point light becomes about 54,000 cd), which under the default exposure turns the frame white, and the default undoes it so that a Blender export looks as it did in Eevee. A file authored in true photometric units sets `"lightScale": 1`. The scale is applied at import, so a bundle carries the scaled lights and nothing in it changed. A `range` maps one to one too, the engine's window being the one the extension recommends; a point or spot light without one reaches where its scaled intensity falls to 0.01, `10 * sqrt(intensity)` metres (about 89 m for Blender's 1000 W light), at most 100 m so one light does not reach every cluster, and the import logs the range it chose with the node's name. glTF lights shine along -Z as the engine's do, so the node's transform needs no change. `world` turns the light into a `DirectionalLight`, `PointLight` or `SpotLight` without shadows ([world.md](world.md#prefabs)).

A glTF mesh's morph targets (`POSITION`, `NORMAL` and `TANGENT` deltas) become `MeshData::morphDeltas`, at most 8 targets kept with a warning for the rest, and the weights a node or its mesh starts with become `ModelNode::morphWeights` (ADR-0023). `cookMesh` does not weld a morphed mesh, since vertices that look alike may move apart, but reorders it with its deltas following.

A primitive's `COLOR_0` (`vec3` or `vec4`, as float or normalized unsigned byte or short) becomes `Vertex::color`, linear RGBA8 with alpha 1 for a `vec3`, white where a primitive has none; floats outside 0 to 1 are clamped and a morph target's `COLOR_0` is ignored ([ADR-0031](decisions/0031-vertex-colours.md)). `cookMesh` keeps vertices that differ only in colour apart.

A mesh whose primitives carry `JOINTS_0` and `WEIGHTS_0` keeps them as one `SkinWeights` per vertex, which the renderer uploads alongside the vertices for its skinning pass ([rendering.md](rendering.md#skinning)).

## Materials

A material file (`<name>.material.json`) holds a `MaterialSource`: the renderer's metallic-roughness description with its textures named by UUID, versioned like the scene files:

```json
{
  "version": 5,
  "baseColor": [1.0, 1.0, 1.0, 1.0], "emissive": [0.0, 0.0, 0.0],
  "metallic": 0.0, "roughness": 0.5, "normalScale": 1.0, "occlusionStrength": 1.0, "alphaCutoff": 0.5,
  "alphaMode": "Opaque", "doubleSided": false, "unlit": false,
  "textures": { "baseColor": "6f1c...", "normal": "a3e9..." },
  "transforms": { "baseColor": { "offset": [0.0, 0.0], "rotation": 0.0, "scale": [4.0, 4.0], "texCoord": 0 } }
}
```

`transforms` holds how a texture slot reads its UVs, under the slot's key in `textures`: `texCoord` is the vertex UV set it takes (glTF's `texCoord`: 0 for the first set, 1 for the second), and the rest is its `KHR_texture_transform`, sampling at `offset + rotate(rotation) * (scale * uv)` with the rotation in radians, counter-clockwise, as the glTF extension defines it. A slot that is not listed, or a field that is not, keeps set 0 and the identity, and only slots that differ from that are written. Version 2 added `transforms` and version 3 `texCoord`; an older file loads with every slot on set 0 (and a version 1 file with the identity everywhere), and an engine that knows only an older version refuses a newer file rather than drop what it adds. `unlit` (version 4) shows the base colour and emissive without lighting ([rendering.md](rendering.md#resources-and-materials)); a file older than version 4 loads lit.

`AssetDatabase::materialSource` and `setMaterialSource` are what the editor edits; a change updates the renderer's material at once and `saveMaterial` writes the file. A glTF material is a sub-asset with the same source, editable in memory but not saved back into the glTF. `emissive` is unclamped: a glTF material's `emissiveFactor` imports multiplied by its `KHR_materials_emissive_strength`, so a surface Blender exports with a strength above 1 glows as authored. A glTF material with `KHR_materials_unlit` imports with `unlit` set. Until the renderer refracts ([#254](https://github.com/Pacheco95/sonnet/issues/254)), a glTF material with `KHR_materials_transmission` imports as alpha-blended, its base colour's alpha multiplied by `1 - transmissionFactor`: clear glass shows what is behind it, unbent, and keeps its highlights only where its alpha does. The `transmissionTexture` is ignored. A mesh keeps a primitive's `TEXCOORD_0` and `TEXCOORD_1`, the second as zeros where a primitive has none. A texture slot's `texCoord` imports into the slot's transform, overridden by its `KHR_texture_transform`'s own `texCoord`, as is the rest of the extension. A slot that names a later set reads the first, a primitive without `TEXCOORD_1` whose material reads it, and a normal map on the second set of a primitive without tangents (generated from the first set) are warned about. A mesh from a glTF file carries its default material per slot; a `MeshRenderer` may override them all with one material.

### Material samplers

Material version 5 stores a `samplers` object keyed by texture slot (`baseColor`,
`metallicRoughness`, `normal`, `occlusion`, `emissive`), like `transforms`.
Only slots differing from the default are written. Each entry contains `wrapS` and
`wrapT` (`Repeat`, `ClampToEdge`, `MirroredRepeat`), `magFilter` and `minFilter`
(`Nearest`, `Linear`), and `mipFilter` (`Nearest`, `Linear`, `None`). Missing fields
use Repeat on both axes and Linear for all filters. For example:

```json
{"samplers": {"baseColor": {"wrapS": "ClampToEdge", "wrapT": "Repeat",
  "magFilter": "Nearest", "minFilter": "Nearest", "mipFilter": "None"}}}
```

Version 1–4 materials migrate their single `wrap` to both axes of every slot.
glTF imports each texture's own sampler, including independent S/T wrapping and
all six minification filter choices. Undefined filters use linear mip filtering;
plain NEAREST and LINEAR minification disable mipmaps. Cooking still retains the
mip chain, since another material may sample the same image with mipmaps.
Bundle version 17 adds a packed vertex colour (60-byte vertices), 16 orthographic model cameras and version 7 scenes and prefabs, and carries version 5 materials; older bundles must be cooked again.
The [sampler verification report](reports/material-samplers.md) records the Khronos
reference capture and the generated checker regression.

### Rescan

`rescan` brings the database up to date with the asset roots after files changed outside the
editor; the editor calls it when its window gains the focus and when the Assets panel is shown,
not on a timer. It compares the importable files on disk with the registered sources. A file
that appeared is registered as `open` would have, writing its sidecar when it has none. An
asset whose source is gone is unregistered and logged, with its sub-assets and what was loaded
from it. A new file whose sidecar carries the identity of a vanished asset is that asset
moved: it keeps its identity, its loaded handles, its sub-assets and its cache entries, the
same as `move` through the shared relocation step. A file moved without its sidecar is a
removal and an addition with a new identity. A file whose sidecar names an identity that is
still registered at another path is a copy of both, and is skipped with the error `open` logs.
The result lists the added, removed and moved assets, with the old and new paths, so the editor
can follow open scenes and rebuild the prefabs of models. A bundle has nothing to rescan.

## Hot reload

`AssetDatabase::pollChanges`, called by the editor every frame, compares the sources' modification times every half second. A changed file is re-imported: a texture gets a new renderer texture under the same identity and every loaded material that reads it is updated, a material file updates its material in place so handles stay valid, an environment or a glTF file is unloaded and loaded again with its sub-asset list refreshed (a glTF file is also reported by `takeReimportedModels`, which is how the editor knows to rebuild its prefab), a script or a sound is read again under a new revision, which reloads the script and restarts the sources playing the sound. Entities resolve meshes through the database each frame and pick up the new objects. Shaders reuse the previous pipeline if compilation fails and surface the error in the editor.

## Project file

A project is a folder with a `project.json` at its root:

```json
{
  "version": 2,
  "name": "Hook",
  "engineVersion": "0.18.0",
  "startScene": "scenes/main.scene.json",
  "assetRoots": ["assets", "scripts"],
  "module": "hook"
}
```

`version` is the schema version of the file, independent of the engine version ([conventions.md](conventions.md#versioning)): a file without one is version 1, a newer one than the engine knows is refused, and every save writes the current one. `engineVersion` only records the engine that last saved it. `module` names the project's game module, the C++ its scenes' components and systems come from ([ADR-0027](decisions/0027-game-modules.md), [player.md](player.md#game-modules)); version 2 added it, a version 1 file and a project without native code have none, and only a player or an editor built with that module opens the project. Everything in the project is referenced relative to this folder so projects are portable. `assets::Project` reads and writes it and resolves paths against the folder, because the editor and the player both open projects ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)); creating one with its starter scene needs `world` and stays in the editor ([editor.md](editor.md#projects-and-scenes)).

## Scene file

Scenes are JSON produced by the `world` serializer: a list of entities with their UUID, name, parent, components with reflected fields, and prefab references. Prefab instances store only overrides, and what their descendants changed. The format is versioned with a top-level `version` field; migrations are applied on load and old versions are never written. The format as built, with an example, is in [world.md](world.md#scenes).

## Reading JSON and CBOR

Every JSON text and CBOR document the engine reads arrives as bytes, from `core::readFile` or a bundle, and goes through `assets::parseJson` or `assets::parseCbor` (`sonnet/assets/Json.h`), never nlohmann's `parse` or `from_cbor` directly. Given `std::byte`, nlohmann reads through `std::char_traits<std::byte>`, which libc++ stopped defining in LLVM 19, since `std::byte` is not a character type. libstdc++ and the libc++ in Xcode 26 still accept it, so every CI job passed while an NDK newer than r27 would not compile the call. The two functions hand nlohmann `char` and `unsigned char` instead. Invalid input gives a discarded value (`is_discarded()`), never an exception, and a CBOR document with trailing bytes is invalid.

## Cooking and export

`assets::cook` writes a project into `<out>/game.sbundle`, the name the player looks for beside its own binary. `sonnet_cook <project> [--platform windows|linux|macos|android|ios] [--out <dir>] [--scene <scene>]` is its command line, and the editor's export dialog is the other caller, which also copies a player next to a desktop bundle ([editor.md](editor.md#export)). `--scene` accepts a project-relative scene path (or an absolute path within the project). It makes that scene the bundle's start scene and leaves other scenes out; all prefabs and assets remain available to scripts at run time. Without it, the project start scene and all scenes are cooked. The tool returns 2 for invalid arguments and 1 for a failed cook or an exception, including during argument parsing; exceptions are reported to standard error.

The cook asks the open `AssetDatabase` for every asset, so the importers run in the code that already runs them and the texture cache is reused rather than rebuilt ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)). `sonnet_cook` opens the project in a database on a null device, so cooking needs no GPU and no window; the editor cooks from the database it already has open, which means an exported material is the one on screen. An asset that will not cook is a warning in the `CookReport` and is left out, the way a missing asset is logged and skipped at run time; only a database open on another project, an invalid selected scene, or a bundle that cannot be written, fails the cook outright. The built-in primitives are never written: every database registers them.

The three desktop platforms cook the same bytes and differ only in which player binary export copies. Android and iOS cook the same bytes as each other, with their compressed textures in ASTC ([Textures](#textures)), which a device without ASTC cannot run ([player.md](player.md#opening-a-game)). The manifest records the platform a bundle was cooked for.

### The bundle

A bundle is one file, `<name>.sbundle` ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)): a 32-byte header (the magic `SONNETBN`, the format version, and where the index is), the payload blobs back to back, and the index at the end. The index is CBOR, so it is a JSON document at both ends:

The format version is `BundleVersion`, 17 today: 17 vertices with a packed colour, 16 orthographic model cameras and version 7 scenes and prefabs, 15 per-slot material samplers, 14 version 6 scenes and prefabs, with exposure and bloom on the camera, 13 model nodes that may carry a light, 12 a camera, 11 carries version 4 materials, which may be unlit, 10 version 5 scenes and prefabs, with vectors as arrays ([world.md](world.md#scenes)), 9 vertices with a second UV set and version 3 materials, with a UV set per texture slot, 8 version 2 materials, with their texture transforms ([Materials](#materials)), 7 scenes and prefabs that may hold version 4's `instanced` entries ([world.md](world.md#scenes)), 6 added the game module's name to the manifest (`module`, empty for none), which the player checks before anything loads ([player.md](player.md#game-modules)), 5 the Font type and its `preload` ranges in the index, 4 a mesh's far shadow range, 3 HDR environments as KTX2. `Bundle::open` refuses any other version with `bundle version N is not version 17`, so a bundle is cooked again after an engine upgrade that changes it; a project's scenes and sidecars are not touched by it, and a project made before version 7 keeps its scenes.

- `manifest`: the project's `name`, the `engineVersion` that cooked it, the `platform` it was cooked for, and the `startScene` as a path.
- `assets`: one entry per asset with its `uuid`, `type`, `name`, the `parent` it is a sub-asset of, a mesh's default `materials`, a font's `preload` ranges, and the `offset` and `size` of its payload.
- `files`: the scene and prefab paths, each with an offset and size. Scenes keep their project-relative path because that is how `project.json` and the editor name them.

`Bundle::open` reads the index and nothing else; a payload is read when it is asked for, as a seek and a read on the stream the bundle keeps open. The bundle is read through `platform::Platform::openContent` ([platform.md](platform.md#paths)), so a relative path is in the content root (the APK's `assets/` on Android, beside the binary elsewhere) and an absolute one is an ordinary file. The desktop callers, the editor's export check, the cook and the tests, pass absolute paths. A payload's offset and size are checked against the stream's size before anything is allocated, so a corrupt index is an `Io` error rather than a huge allocation. `BundleWriter` streams payloads out as they arrive, so a project's textures never all sit in memory at once, and `finish` writes the index and rewrites the header.

### What each payload is

| Type | Cooked as |
|---|---|
| Texture | The KTX2 file the editor already cooks into its cache, or on Android and iOS the ASTC one `mobileTexture` caches ([Textures](#textures)) |
| Mesh | A binary block of the renderer's vertex layout, welded and reordered ([Meshes](#meshes)) |
| Environment | A UASTC HDR 4x4 KTX2 file of the equirectangular map, transcoded by device on load ([Environments](#environments)) |
| Skin, Animation, Model | Binary: the joints and matrices, the channels and their keys, the node hierarchy |
| Material | CBOR of the `.material.json` document |
| Script | The Lua source, as it is |
| Sound | The encoded file, as it is; `audio` decodes it either way |
| Font | The `.ttf` or `.otf` file, as it is; the index entry carries its `preload` ranges, since a bundle has no sidecar |
| Scene, prefab | CBOR of the scene document, so its `version` field and the migrations keep working |

Every payload begins with a four-character tag, and every decode is defensive: a payload of the wrong kind, one that is truncated, or one whose counts promise more than it holds, is an `Io` error rather than a read past the end.

## See also

- [Architecture](architecture.md)
- [Rendering](rendering.md)
