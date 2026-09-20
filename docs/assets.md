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

`AssetDatabase` maps UUIDs to metadata (`AssetInfo`: type, source path, name, parent, default materials) and to loaded renderer objects and data. `open` scans the project's asset roots, writes the sidecars that are missing and registers every file whose extension has an importer; `assets` lists them by type for the editor's browser. Loading runs on the main thread, on first use: `mesh`, `texture`, `material`, `environment` and `model` import what they are asked for and return the renderer's handle, or an invalid handle when the import failed, which is logged once and not retried until a re-import. `script` returns a script's source text with a revision that changes on every read, so the scripting runtime knows when to reload ([scripting.md](scripting.md#errors-and-hot-reload)), and `sound` the still encoded bytes of a sound file with a revision of the same kind, which `audio` decodes ([audio.md](audio.md#miniaudio)); `skin` and `animation` return a model's skins and clips, loaded with the rest of their glTF file, with a revision that changes when the file is re-imported so the bindings made from them are rebuilt ([world.md](world.md#animation)). `createScript` writes a new `.lua` file and registers it, as `createMaterial` does for materials. A glTF file's sub-assets are imported together the first time any of them is asked for. The draw list resolves mesh identities through the database every frame, so a swap under an identity reaches the next frame without anyone holding a stale handle.

The asynchronous-ready form of this API, a request that returns at once with a placeholder until a job finishes, arrives with the job system.

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

Import decodes to RGBA8, generates mipmaps, and cooks to KTX2 in the project's cache (`.sonnet/cache/<uuid>.ktx2`, ignored by git), which is rebuilt when the source or its sidecar is newer. A compressed texture is stored as Basis Universal UASTC with zstd supercompression, the portable form, and transcoded on load to BC7 where the device supports block compression (`DeviceInfo::blockCompressionSupported`: desktop GPUs and Lavapipe) and to RGBA8 otherwise; ASTC for mobile joins in M7. An uncompressed texture is stored as plain RGBA8. Colour textures are sRGB, data textures (normals, roughness, metallic, occlusion) are linear; the glTF importer decides per image from how the materials use it, and a file texture's sidecar says.

## Meshes

Meshes are uploaded in the vertex layout the renderer pulls from (position, normal, tangent, one UV set) with a 32-bit index buffer, bounds and a material slot list, straight from the glTF file. The database keeps the CPU copy of every loaded mesh, built-in or imported, which `meshData` returns for physics to build mesh colliders from ([physics.md](physics.md#components)); a cooked binary form of the same layout arrives with the cook tool in M6, together with welding and vertex-cache optimisation.

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

## Cooking and export

`sonnet_cook <project> --platform <windows|linux|macos|android|ios> --out <dir>` produces a bundle: cooked assets in an index-plus-blob format, scenes in a compact binary encoding, and a manifest. Export copies the player binary for the target next to the bundle and, for mobile, wraps both in the platform's application package. The editor's export dialog runs the same tool.

## See also

- [Architecture](architecture.md)
- [Rendering](rendering.md)
