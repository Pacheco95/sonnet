# Licences of the character sample's content

Every third-party file in this project is CC0 1.0 (public domain dedication): it may be copied, changed and redistributed, commercially too, with no attribution required. The sources are credited here anyway, with what was done to them. Everything else, the course's geometry (`assets/level/course.glb`), its materials, the scene and the scripts, is the engine's own, made by `tools/generate_character_level.py`.

## Level content (issue #490)

| File under `assets/level/` | Source | Author | Licence |
|---|---|---|---|
| `textures/Concrete032_*.jpg` (floors) | [Concrete 032](https://ambientcg.com/view?id=Concrete032), `Concrete032_1K-JPG.zip` | ambientCG (Lennart Demes) | [CC0 1.0](https://docs.ambientcg.com/license/) |
| `textures/Concrete031_*.jpg` (walls, steps, deck, ramps) | [Concrete 031](https://ambientcg.com/view?id=Concrete031), `Concrete031_1K-JPG.zip` | ambientCG (Lennart Demes) | [CC0 1.0](https://docs.ambientcg.com/license/) |
| `textures/PaintedMetal004_*.jpg` (columns, beam, platform) | [Painted Metal 004](https://ambientcg.com/view?id=PaintedMetal004), `PaintedMetal004_1K-JPG.zip` | ambientCG (Lennart Demes) | [CC0 1.0](https://docs.ambientcg.com/license/) |
| `textures/Planks037A_*.jpg` (crate) | [Planks 037 A](https://ambientcg.com/view?id=Planks037A), `Planks037A_1K-JPG.zip` | ambientCG (Lennart Demes) | [CC0 1.0](https://docs.ambientcg.com/license/) |
| `kloofendal_48d_partly_cloudy_2k.hdr` (sky and lighting) | [Kloofendal 48d Partly Cloudy](https://polyhaven.com/a/kloofendal_48d_partly_cloudy), 2K `.hdr` | Greg Zaal, Poly Haven | [CC0 1.0](https://polyhaven.com/license) |

Downloaded on 2026-11-06. SHA-256 of the files as downloaded:

| File | SHA-256 |
|---|---|
| `Concrete031_1K-JPG.zip` | `3068e551435d3cdae3942017799d8871b940c88ebfaf5c2b87376972ae448fd6` |
| `Concrete032_1K-JPG.zip` | `49ce4fe78bf562376a2b5bdd7ecc11a89a433b30194a74722316f880b07d2c2c` |
| `PaintedMetal004_1K-JPG.zip` | `02c243e8e8441f0ee24e68645f5d8c9bd6d8ee0a3b9bfa3d952840902653f978` |
| `Planks037A_1K-JPG.zip` | `df255df16475bad251f194eb173017c46d5354d572348d435808717c478cf144` |
| `kloofendal_48d_partly_cloudy_2k.hdr` | `3fbd33f279f29bd64925c1dd3214fd46c627493d21f1100248b6b1098da4d06e` |

### Preparation

`python3 tools/generate_character_level.py prepare <folder>` turns the downloads, extracted anywhere under `<folder>`, into the committed files. Only the image files are read; the archives' `.blend`, `.mtlx`, `.usdc`, `.tres`, displacement, DirectX normal and opacity maps are not used.

- **Colour** (`*_Color.jpg`): scaled in linear light so the map's mean luminance is a measured albedo for the surface (0.28 floor concrete, 0.30 wall concrete, 0.16 red primer, 0.10 weathered timber; the downloads average 0.06 to 0.26, darker than the surfaces they were shot from), with no texel clipped, then saved as JPEG quality 90.
- **Normal** (`*_NormalGL.jpg`): the archive's OpenGL-convention map, which is glTF's, saved as JPEG quality 90 with full chroma.
- **ORM** (`*_ORM.jpg`): the archive's ambient occlusion in red, roughness in green and metalness in blue, as glTF's occlusion and metallic-roughness slots read them; white occlusion where an archive has none (Painted Metal 004) and black metalness likewise (the concretes). JPEG quality 90, full chroma.
- **Sky**: the sun's disc (964 texels within 4° of the brightest one and more than four times the sky around it) is set to the median of the sky 4° to 6° from it, and the energy it lost, integrated over the texels' solid angle, becomes the scene's `Sun`: direction `(0.5435, 0.7417, 0.3931)`, 47.9° up, as the map's name says, and irradiance `(4.50, 4.54, 4.14)` in the map's units. Left in, the disc would light every surface a second time through the image-based lighting. The file is otherwise the download, written back as run-length encoded RGBE.

All textures are 1024 × 1024 and tile every 2 m, so the course has one texel density, 512 texels per metre.

## Humanoid (issue #494)

`assets/character/humanoid.glb` is made by `tools/generate_character_humanoid.py` from the files below, with MPFB 2, MakeHuman's Blender extension. MPFB's code is GPL 3.0 or later and is only run, not shipped; what it builds from MakeHuman's assets is CC0 ([MPFB FAQ](https://static.makehumancommunity.org/mpfb/faq/use_in_closed_source.html): "All core assets (the base mesh, targets, skins…) are shared under CC0"). Every asset used is marked CC0 in its pack's index (`packs/*.json`), and the MakeHuman system files carry the header "This asset was explicitly released as CC0 in september 2020" (copyright holders then: Data Collection AB, Joel Palmius, Jonas Hauquier). So the model, its textures and anything cooked from them may be redistributed in this repository and in bundles with no attribution required; it is credited here anyway.

| Part of the model | Source | Author | Licence |
|---|---|---|---|
| Body mesh, phenotype targets, `game_engine` rig and its weights | MPFB 2.0.18 (`data/3dobjs/base.obj`, `data/targets`, `data/rigs/standard/*.game_engine.json`), [extensions.blender.org/add-ons/mpfb](https://extensions.blender.org/add-ons/mpfb/) | MakeHuman team | CC0 1.0 (the code: GPL 3.0 or later, not redistributed) |
| Skin base colour (`young_caucasian_male`) | [MakeHuman system assets](https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html), `makehuman_system_assets_cc0.zip` | MakeHuman team | [CC0 1.0](https://static.makehumancommunity.org/about/license.html) |
| Skin normal and specular (`mindfront_aksel_skin`, body only) | [Skins 02](https://static.makehumancommunity.org/assets/assetpacks/skins02.html), `skins02_cc0.zip` | Mindfront | CC0 1.0 |
| Eyes (`high-poly`, `brown`), eyebrows (`eyebrow001`), eyelashes (`eyelashes01`), hair (`short02`) | MakeHuman system assets | MakeHuman team | CC0 1.0 |
| T-shirt and jeans (`male_casualsuit06`), shoes (`shoes01`) | MakeHuman system assets | MakeHuman team | CC0 1.0 |

Downloaded on 2026-11-07. SHA-256 of the files as downloaded:

| File | SHA-256 |
|---|---|
| `add-on-mpfb-v2.0.18.zip` | `c075b3e9b94b8e5556a5f03f4653602d02ea306de08126e8f55ed6dee28a2dad` |
| `makehuman_system_assets_cc0.zip` | `b542127a8e25547c7c29c19f2d1d2adb9a664c80396ecd694095dbc8028a0107` |
| `skins02_cc0.zip` | `1613f1ef3afca53094511d26620ed7cf1d2dedc29ed3d384d60bdebe250698ae` |

### Preparation

`python3 tools/generate_character_humanoid.py <folder>` (Blender 4.2 or later; Blender 5.2.2 here) turns the downloads, anywhere under `<folder>`, into the committed file; two runs wrote the same bytes. It installs MPFB into a temporary Blender user folder and unpacks the two packs into MPFB's library there, then, inside Blender:

- **Human**: MPFB builds a young adult man (gender 1, age 0.5, muscle 0.6, weight 0.5, proportions 0.7, height 0.55, caucasian 1: 1.82 m to the top of the hair) with the `game_engine` rig and the parts above, without subdivision. The targets are baked into the mesh, the helper geometry and the skin under the clothes are removed, and the parts are joined into one mesh, `Body`, with seven materials. 17,335 vertices (18,441 once split along UV seams), 33,688 triangles, at most four joints per vertex.
- **Ambient occlusion**: the body's own, against itself and the eyes, baked by Cycles in the rest pose into 2048 × 2048 texels, 128 samples, rays at most 0.12 m long, so creases, ears, nostrils, fingers and armpits darken and nothing larger; it is the red of the skin's ORM map.
- **Placement**: turned 180° about the vertical, so the character faces -Z in glTF as engine entities do, and lifted 1.9 mm so the shoe soles rest on y = 0; the `Root` bone stays at the origin.
- **`Stand`**: a static clip (two equal keys, 1/24 s apart, every bone) that lowers each upper arm 40° from the rig's A pose, so a placed model stands with its arms down. The bind pose is the A pose.

Then the textures, written into the binary glTF (embedded, so they are the model's sub-assets rather than textures of the project as well):

- **Skin**: base colour, the system skin's 2048 map as it is (JPEG quality 90). Normal: Aksel's map on the body; on the face and neck, whose Aksel map carries make-up injuries (a swollen eye, cuts) and deep tendons, a fine normal from the base colour's own detail (its luminance minus a 6-texel blur, as height), blended over 12 texels; normal scale 0.5. ORM: the baked occlusion, roughness 0.5 minus 0.8 times the detail of Aksel's specular map (clamped to 0.38 to 0.65; 0.48 on the face and neck), metalness 0.
- **T-shirt and jeans**: the suit's own 2048 base colour, normal and occlusion maps. The MakeHuman logo printed on the shirt's chest, back and sleeve is filled with the cotton around it, and the shirt's colour scaled by 0.8 in linear light (its mean albedo 0.61 flattened at the top of the tone curve under the course's sun). Roughness 0.88.
- **Shoes**: the source's 1024 base colour and normal maps (it has no larger ones, nor an occlusion map); roughness 0.55.
- **Hair, eyebrows, eyelashes**: alpha-masked cards (cutoff 0.35, double-sided), base colour with alpha as a palettised PNG; the hair has its 2048 normal map; roughness 0.62 for the hair, 0.7 for the brows and lashes.
- **Eyes**: the iris and sclera map, masked at 0.5: its alpha is zero only where the cornea's shell maps, which would otherwise hide the iris. Roughness 0.08.
