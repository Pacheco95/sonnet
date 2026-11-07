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
