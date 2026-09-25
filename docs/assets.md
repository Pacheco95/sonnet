# Assets

The `assets` module owns asset identity, the database that resolves ids to loaded objects, the importers, hot reload, and cooking for export.

## Identity

Every asset has a UUID assigned at import and stored in a sidecar file next to the source (`hero.gltf` and `hero.gltf.meta`). Scenes and components reference assets by UUID, never by path, so files can be moved or renamed without breaking references. Paths are for humans and for the importer only.

An asset may produce sub-assets: a glTF file yields meshes, materials, textures, skins and animation clips, each with its own stable UUID derived from the parent's UUID and the sub-asset name (`core::Uuid::derive`, `mesh/0`, `material/2`, `image/1`, `skin/0`, `animation/1`) so re-import keeps them. The five built-in primitive meshes have fixed identities of the same kind (`assets::builtin::box()` and the others), so a scene made of primitives references them like any other mesh.

The sidecar is JSON: a `version`, the `uuid`, the asset `type`, the import `settings`, and for a glTF file a hash of the file's content and the list of its sub-assets with their names and, for a mesh, its default material per slot, so opening a project does not parse every model and a sidecar committed with the project stays valid in every checkout. Version 2 of the sidecar lists a model's skins and clips among the sub-assets; a glTF file whose sidecar is older is parsed again on the next scan and its list rewritten. Sidecars belong in version control next to their sources.

## Source and cooked

- **Source assets** are what the user edits: glTF, PNG, HDR, `.slang`, `.lua`, WAV and the other sound files, JSON scenes. They live in the project folder and are what the editor imports.
- **Cooked assets** are what the player loads: engine-native binary formats laid out for direct upload. They are produced by the cook step for a target platform and packed into a bundle. The editor cooks on demand into a cache folder so it uses the same loading path as the player.

## Database

`AssetDatabase` is constructed on a renderer and a `core::JobSystem`, which carries its asynchronous requests and has to outlive it; a pool with no workers imports on the thread that drains it, which is what `sonnet_cook` and the tests use. It maps UUIDs to metadata (`AssetInfo`: type, source path, name, parent, default materials) and to loaded renderer objects and data. `open` scans the project's asset roots, writes the sidecars that are missing and registers every file whose extension has an importer; `assets` lists them by type for the editor's browser. `open` closes the project that was open first, and `close` releases everything it loaded, materials included: a material kept across projects would still read the textures the close destroyed. Loading runs on the main thread, on first use: `mesh`, `texture`, `material` and `environment` import what they are asked for and return the renderer's handle, or an invalid handle when the import failed, which is logged once and not retried until a re-import. `script` returns a script's source text with a revision that changes on every read, so the scripting runtime knows when to reload ([scripting.md](scripting.md#errors-and-hot-reload)), and `sound` the still encoded bytes of a sound file with a revision of the same kind, which `audio` decodes ([audio.md](audio.md#miniaudio)); `skin` and `animation` return a model's skins and clips, loaded with the rest of their glTF file, with a revision that changes when the file is re-imported so the bindings made from them are rebuilt ([world.md](world.md#animation)). `model` returns a model's node hierarchy, and for a source glTF it reads that from the file's JSON alone, without loading a buffer or an image: every model is placed as a prefab when a project opens, and a prefab needs nothing else. The nodes are the full import's index for index, since both walk the file through one function, and the full import replaces the hierarchy with an identical one when it arrives. From a bundle the model is a payload of its own and was never tied to its meshes. `createScript` writes a new `.lua` file and registers it, as `createMaterial` does for materials. A glTF file's sub-assets are imported together the first time any of them is asked for. The draw list resolves mesh identities through the database every frame, so a swap under an identity reaches the next frame without anyone holding a stale handle.

`requestMesh`, `requestTexture`, `requestSkin` and `requestAnimation` are the asynchronous form ([ADR-0013](decisions/0013-job-system.md)). A loaded asset comes back at once; anything else returns an invalid handle and schedules the import on the job system, so the caller draws nothing for it this frame and asks again on the next. The import runs on a worker — parsing, decoding, mip generation, KTX2 cooking, none of which touches the renderer — and a main-thread job creates the renderer objects, since recording an upload is not thread-safe. The application publishes them by calling `core::JobSystem::runMainThreadJobs` once a frame, before the draw list resolves identities. Asking again while a request is in flight joins it rather than importing twice, and a glTF file is one request for the whole file, so a mesh and its textures arrive together exactly as the synchronous path loads them. `loading` says whether any request is outstanding, which is what a loading screen waits on, and `waitForLoads` runs them all to completion for a caller that wants the synchronous behaviour back; `close` calls it, so a database is never torn down under a job still writing into it.

Materials request their textures rather than load them, so neither `material` nor an edit through `setMaterialSource` imports an image on the main thread: the material is created at once and its slots fill in as the textures publish. A slot whose texture is still importing reads the renderer's fallback, white or a flat normal, which already means "no effect" — except the base colour, which reads a neutral grey placeholder the database creates on first use, since white would show the material's untextured colour as though it were finished. A texture that failed keeps the fallback, so pending and failed look different. The main-thread job that publishes a texture, or a glTF file's images, resolves every loaded material reading it again, which is also what replaces the placeholder with the fallback when an import fails. A glTF file's own materials never see the placeholder, since its images are published before its materials resolve. A synchronous load that meets a request in flight for the same file — `texture` or `mesh` while a draw list's request is importing, or a re-import — waits for it and publishes it rather than importing the file a second time.

`buildDrawList` uses the request form, so an entity whose mesh is not in memory yet draws nothing for a frame or two instead of stalling the one that asks ([world.md](world.md#draw-list)), and so do the animation systems for skins and clips ([world.md](world.md#animation)). Together with `model` reading only the hierarchy, opening a project or a scene imports no glTF file on the main thread: the prefabs exist at once, complete as entities, and their meshes, textures, skins and clips arrive on the frames after. On the basic sample, placing its three models went from 0.38 ms with a warm cache and 11.2 ms on a first open, which cooks the textures, to 0.023 ms either way; `assets_tests "[benchmark]"` measures it. The sample's files are small, and what the old path cost grew with a file's images while the new one grows only with its JSON.

## Importers

| Source | Importer | Produces |
|---|---|---|
| glTF 2.0 (`.gltf`, `.glb`) | fastgltf | Meshes (one submesh per triangle primitive, flat normals and tangents generated when missing), PBR materials, the images they use, skins, animation clips, and a `Model` with the node hierarchy that `world` turns into a prefab |
| PNG, JPEG, TGA, BMP | stb_image | Textures, decoded to RGBA8 with a box-filtered mip chain and cooked to KTX2 |
| `.hdr` | stb_image | Environments, decoded to RGBA16F for the renderer's cubes |
| KTX2 | KTX-Software | Textures, used directly; Basis Universal data is transcoded on load |
| `.material.json` | assets | Materials ([Materials](#materials)) |
| `.slang` | Slang, through `ShaderCompiler` | SPIR-V modules for the editor's shader hot reload ([rendering.md](rendering.md#shaders)) |
| `.lua` | assets | Script assets: the source text, run by `scripting` ([scripting.md](scripting.md)) |
| `.wav`, `.ogg`, `.mp3`, `.flac` | assets | Sound assets: the encoded file, decoded and played by `audio` ([audio.md](audio.md)) |
| `.scene.json`, `.prefab.json` | world | Scenes and prefabs |

Import settings live in the sidecar file and are edited in the inspector. A texture's are `TextureSettings`: sRGB or linear, mipmaps, compression; changing them rewrites the sidecar and re-imports at once. The importers are also plain functions in `Importers.h` (`importImage`, `importHdr`, `importGltf`, `readKtx2`, `cookKtx2`) for tools and tests.

## Textures

Import decodes to RGBA8, generates mipmaps, and cooks to KTX2 in the project's cache (`.sonnet/cache/<uuid>.ktx2`, ignored by git), which is rebuilt when the source or its sidecar is newer. A compressed texture is stored as Basis Universal UASTC with zstd supercompression, the portable form, and transcoded on load to BC7 where the device supports block compression (`DeviceInfo::blockCompressionSupported`: desktop GPUs and Lavapipe) and to RGBA8 otherwise; ASTC for mobile joins in M9. The UASTC encode runs on a thread pool of Basis Universal's own, one thread per core, created and torn down around each texture; the `ktx` port patches its shutdown, which could hang ([ports/README.md](../ports/README.md)). An uncompressed texture is stored as plain RGBA8. Colour textures are sRGB, data textures (normals, roughness, metallic, occlusion) are linear; the glTF importer decides per image from how the materials use it, and a file texture's sidecar says.

## Meshes

Meshes are uploaded in the vertex layout the renderer pulls from (position, normal, tangent, one UV set) with a 32-bit index buffer, bounds and a material slot list, straight from the glTF file. The database keeps the CPU copy of every loaded mesh, built-in or imported, which `meshData` returns for physics to build mesh colliders from ([physics.md](physics.md#components)).

`cookMesh` reshapes a mesh for the player: vertices equal to the bit are welded into one, each submesh's triangles are reordered for a sixteen-entry post-transform vertex cache with tipsify (Sander, Nehab and Barczak), and the vertices are renumbered by first use so the fetch runs forwards and vertices no index names are dropped. The result draws the same triangles wound the same way; only their order and numbering change, and a submesh stays one range of the index buffer. `averageCacheMissRatio` measures what that bought: an unwelded grid of quads goes from three misses per triangle, the worst there is, to about 0.65 against a floor of 0.5. A mesh the cook cannot reshape, one whose indices are not whole triangles or name vertices that are not there, comes back as it was.

## Skins and animations

A glTF skin becomes a `Skin`: the joints of a skinned mesh in the order its weights index, each named by its path of node names from the model's root (`Armature/Hips/Spine`), with the inverse bind matrix that takes the mesh's bind pose into the joint's space. A glTF animation becomes an `AnimationClip`: translation, rotation and scale channels with their keys, each naming its node by the same kind of path, linear (spherical for rotations), stepped or cubic-spline interpolated, and `assets::sample` reads one at a time. Paths, rather than indices or identities, are what survives prefab instantiation and re-import ([ADR-0010](decisions/0010-audio-and-animation.md)); `world` resolves them against the entities of a model's instance ([world.md](world.md#animation)). Morph target weights are not imported.

A mesh whose primitives carry `JOINTS_0` and `WEIGHTS_0` keeps them as one `SkinWeights` per vertex, which the renderer uploads alongside the vertices for its skinning pass ([rendering.md](rendering.md#skinning)).

## Materials

A material file (`<name>.material.json`) holds a `MaterialSource`: the renderer's metallic-roughness description with its textures named by UUID, versioned like the scene files:

```json
{
  "version": 1,
  "baseColor": [1.0, 1.0, 1.0, 1.0], "emissive": [0.0, 0.0, 0.0],
  "metallic": 0.0, "roughness": 0.5, "normalScale": 1.0, "occlusionStrength": 1.0, "alphaCutoff": 0.5,
  "alphaMode": "Opaque", "wrap": "Repeat", "doubleSided": false,
  "textures": { "baseColor": "6f1c...", "normal": "a3e9..." }
}
```

`AssetDatabase::materialSource` and `setMaterialSource` are what the editor edits; a change updates the renderer's material at once and `saveMaterial` writes the file. A glTF material is a sub-asset with the same source, editable in memory but not saved back into the glTF. A mesh from a glTF file carries its default material per slot; a `MeshRenderer` may override them all with one material.

## Hot reload

`AssetDatabase::pollChanges`, called by the editor every frame, compares the sources' modification times every half second. A changed file is re-imported: a texture gets a new renderer texture under the same identity and every loaded material that reads it is updated, a material file updates its material in place so handles stay valid, an environment or a glTF file is unloaded and loaded again with its sub-asset list refreshed, a script or a sound is read again under a new revision, which reloads the script and restarts the sources playing the sound. Entities resolve meshes through the database each frame and pick up the new objects. Shaders reuse the previous pipeline if compilation fails and surface the error in the editor.

## Project file

A project is a folder with a `project.json` at its root:

```json
{
  "name": "Basic",
  "engineVersion": "0.5.0",
  "startScene": "scenes/main.scene.json",
  "assetRoots": ["assets", "shaders", "scripts"]
}
```

Everything in the project is referenced relative to this folder so projects are portable. `assets::Project` reads and writes it and resolves paths against the folder, because the editor and the player both open projects ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)); creating one with its starter scene needs `world` and stays in the editor ([editor.md](editor.md#projects-and-scenes)).

## Scene file

Scenes are JSON produced by the `world` serializer: a list of entities with their UUID, name, parent, components with reflected fields, and prefab references. Prefab instances store only overrides. The format is versioned with a top-level `version` field; migrations are applied on load and old versions are never written. The format as built, with an example, is in [world.md](world.md#scenes).

## Reading JSON and CBOR

Every JSON text and CBOR document the engine reads arrives as bytes, from `core::readFile` or a bundle, and goes through `assets::parseJson` or `assets::parseCbor` (`sonnet/assets/Json.h`), never nlohmann's `parse` or `from_cbor` directly. Given `std::byte`, nlohmann reads through `std::char_traits<std::byte>`, which libc++ stopped defining in LLVM 19, since `std::byte` is not a character type. libstdc++ and the libc++ in Xcode 26 still accept it, so every CI job passed while an NDK newer than r27 would not compile the call. The two functions hand nlohmann `char` and `unsigned char` instead. Invalid input gives a discarded value (`is_discarded()`), never an exception, and a CBOR document with trailing bytes is invalid.

## Cooking and export

`assets::cook` writes a project into `<out>/game.sbundle`, the name the player looks for beside its own binary. `sonnet_cook <project> [--platform windows|linux|macos] [--out <dir>] [--scene <scene>]` is its command line, and the editor's export dialog is the other caller, which also copies a player next to the bundle ([editor.md](editor.md#export)). `--scene` accepts a project-relative scene path (or an absolute path within the project). It makes that scene the bundle's start scene and leaves other scenes out; all prefabs and assets remain available to scripts at run time. Without it, the project start scene and all scenes are cooked. Mobile targets join in M9. The tool returns 2 for invalid arguments and 1 for a failed cook or an exception, including during argument parsing; exceptions are reported to standard error.

The cook asks the open `AssetDatabase` for every asset, so the importers run in the code that already runs them and the texture cache is reused rather than rebuilt ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)). `sonnet_cook` opens the project in a database on a null device, so cooking needs no GPU and no window; the editor cooks from the database it already has open, which means an exported material is the one on screen. An asset that will not cook is a warning in the `CookReport` and is left out, the way a missing asset is logged and skipped at run time; only a database open on another project, an invalid selected scene, or a bundle that cannot be written, fails the cook outright. The built-in primitives are never written: every database registers them.

The three desktop platforms cook the same bytes today and differ only in which player binary export copies; the manifest records which one it was cooked for.

### The bundle

A bundle is one file, `<name>.sbundle` ([ADR-0011](decisions/0011-cooked-bundles-and-the-player.md)): a 32-byte header (the magic `SONNETBN`, the format version, and where the index is), the payload blobs back to back, and the index at the end. The index is CBOR, so it is a JSON document at both ends:

- `manifest`: the project's `name`, the `engineVersion` that cooked it, the `platform` it was cooked for, and the `startScene` as a path.
- `assets`: one entry per asset with its `uuid`, `type`, `name`, the `parent` it is a sub-asset of, a mesh's default `materials`, and the `offset` and `size` of its payload.
- `files`: the scene and prefab paths, each with an offset and size. Scenes keep their project-relative path because that is how `project.json` and the editor name them.

`Bundle::open` reads the index and nothing else; a payload is read when it is asked for. `BundleWriter` streams payloads out as they arrive, so a project's textures never all sit in memory at once, and `finish` writes the index and rewrites the header.

### What each payload is

| Type | Cooked as |
|---|---|
| Texture | The KTX2 file the editor already cooks into its cache ([Textures](#textures)) |
| Mesh | A binary block of the renderer's vertex layout, welded and reordered ([Meshes](#meshes)) |
| Environment | The equirectangular map's `TextureData`, uncompressed: it is RGBA16F, which the KTX2 path does not cook |
| Skin, Animation, Model | Binary: the joints and matrices, the channels and their keys, the node hierarchy |
| Material | CBOR of the `.material.json` document |
| Script | The Lua source, as it is |
| Sound | The encoded file, as it is; `audio` decodes it either way |
| Scene, prefab | CBOR of the scene document, so its `version` field and the migrations keep working |

Every payload begins with a four-character tag, and every decode is defensive: a payload of the wrong kind, one that is truncated, or one whose counts promise more than it holds, is an `Io` error rather than a read past the end.

## See also

- [Architecture](architecture.md)
- [Rendering](rendering.md)
