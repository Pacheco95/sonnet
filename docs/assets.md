# Assets

The `assets` module owns asset identity, the database that resolves ids to loaded objects, the importers, hot reload, and cooking for export.

## Identity

Every asset has a UUID assigned at import and stored in a sidecar file next to the source (`hero.gltf` and `hero.gltf.meta`). Scenes and components reference assets by UUID, never by path, so files can be moved or renamed without breaking references. Paths are for humans and for the importer only.

An asset may produce sub-assets: a glTF file yields meshes, materials, textures and animation clips, each with its own stable UUID derived from the parent's UUID and the sub-asset name so re-import keeps them.

## Source and cooked

- **Source assets** are what the user edits: glTF, PNG, HDR, `.slang`, `.lua`, JSON scenes. They live in the project folder and are what the editor imports.
- **Cooked assets** are what the player loads: engine-native binary formats laid out for direct upload. They are produced by the cook step for a target platform and packed into a bundle. The editor cooks on demand into a cache folder so it uses the same loading path as the player.

## Database

`AssetDatabase` maps UUIDs to metadata (type, source path, dependencies, import settings) and to loaded instances. Loading is reference-counted and asynchronous-ready: a request returns a `Handle<Asset>` immediately and the object becomes available once loaded, with a placeholder (magenta material, unit cube) in between. Loading runs on the main thread until a job system exists.

## Importers

| Source | Importer | Produces |
|---|---|---|
| glTF 2.0 (`.gltf`, `.glb`) | fastgltf | Meshes, PBR materials, textures, skins and animation clips (M5), a prefab with the node hierarchy |
| PNG, JPEG, HDR | stb_image | Textures, converted to KTX2 on cook |
| KTX2 | KTX-Software | Textures, used directly |
| `.slang` | Slang | Shader programs and reflection data |
| `.lua` | Scripting | Script assets (M4) |
| `.scene.json`, `.prefab.json` | world | Scenes and prefabs |

Import settings (sRGB or linear, mipmaps, compression quality, mesh optimisation) live in the sidecar file and are edited in the inspector.

## Textures

Import decodes to RGBA8 or RGBA16F, generates mipmaps, and cooks to KTX2. Per-target compression: BC7 or BC5 on desktop, ASTC on mobile, with the Basis Universal UASTC format kept as the portable fallback. Colour textures are sRGB, data textures (normals, roughness, metallic, occlusion) are linear.

## Meshes

Cooked meshes are stored in the vertex layout the renderer pulls from (position, normal, tangent, UV sets, optional skin weights) with a 32-bit index buffer, per-submesh bounds, and a material slot list. Import can weld vertices and optimise for the vertex cache.

## Hot reload

The editor watches the project folder. When a source file changes, the importer re-runs, the database swaps the loaded object under the same UUID, and dependents (materials using a texture, entities using a mesh) pick up the new version on the next frame. Shaders reuse the previous pipeline if compilation fails and surface the error in the editor.

## Project file

A project is a folder with a `project.json` at its root:

```json
{
  "name": "Basic",
  "engineVersion": "0.3.0",
  "startScene": "scenes/main.scene.json",
  "assetRoots": ["assets", "shaders", "scripts"]
}
```

Everything in the project is referenced relative to this folder so projects are portable.

## Scene file

Scenes are JSON produced by the `world` serializer: a list of entities with their UUID, name, parent, components with reflected fields, and prefab references. Prefab instances store only overrides. The format is versioned with a top-level `version` field; migrations are applied on load and old versions are never written. The format as built, with an example, is in [world.md](world.md#scenes).

## Cooking and export

`sonnet_cook <project> --platform <windows|linux|macos|android|ios> --out <dir>` produces a bundle: cooked assets in an index-plus-blob format, scenes in a compact binary encoding, and a manifest. Export copies the player binary for the target next to the bundle and, for mobile, wraps both in the platform's application package. The editor's export dialog runs the same tool.

## See also

- [Architecture](architecture.md)
- [Rendering](rendering.md)
