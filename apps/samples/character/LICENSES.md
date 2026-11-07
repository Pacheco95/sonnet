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

## Footstep and landing foley (issue #506)

Recorded foley, each file CC0 1.0 as its OpenGameArt page states; the uploaders recorded them (Fantozzi's steps come from their own CC0 [Freesound pack 10338](https://freesound.org/people/Fantozzi/packs/10338/), Owlish Media's from their Freesound account, owlstorm).

| Files under `assets/audio/footsteps/` | Source files | Source | Author | Licence |
|---|---|---|---|---|
| `footstep_concrete_{left,right}_0{1,2,3}.flac`, `land_concrete.flac` | `Fantozzi-Stone{L,R}{1,2,3}.flac` | [Fantozzi's Footsteps (Grass/Sand & Stone)](https://opengameart.org/content/fantozzis-footsteps-grasssand-stone), `Fantozzi-footsteps.7z` | Fantozzi | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `footstep_metal_{left,right}_0{1,2}.flac`, `land_metal.flac` | `Impacts/alt-toasterstep{1,2,3,4}.wav`, `Impacts/toasterstep2.wav` (named toaster steps; thin sheet metal ringing at 0.4 to 1.2 kHz, the closest CC0 recording of steps on metal found) | [Sound Effects Pack](https://opengameart.org/content/sound-effects-pack), `Owlish Media Sound Effects.zip` | Owlish Media | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| `footstep_wood_{left,right}_0{1,2}.flac`, `land_wood.flac` | `sfx100v2_footstep_wood_0{1,2,3,4}.ogg`, `sfx100v2_wood_hit_01.ogg` | [100 CC0 SFX #2](https://opengameart.org/content/100-cc0-sfx-2), `sfx_100_v2.zip` | rubberduck | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Downloaded on 2026-11-07. SHA-256 of the archives as downloaded, and of the files used from them:

| File | SHA-256 |
|---|---|
| `Fantozzi-footsteps.7z` | `415d360e0911c1c1c5355ee94023b772e6f4044f59b9b580610e7dbffd1840fd` |
| `Owlish Media Sound Effects.zip` | `57d7059fe2c0ea5f47c7554ace22a538ea22ab40bfc0906b71aa259afffd3d18` |
| `sfx_100_v2.zip` | `0fc61b4494e2e893c0c015ced4877b3f689c7d84a48cb61daecd7ddb52db797b` |
| `Fantozzi-StoneL1.flac` | `7cfa4a2f500ae2bd19a961022296611d837c34ca2e556fe68deb692388f49971` |
| `Fantozzi-StoneL2.flac` | `dcbd6be44af4b3206daa36c5398035ca527bb057c51ce00482b9ca6356be2468` |
| `Fantozzi-StoneL3.flac` | `37a25f4254cf8f2e56d271912b0569f3bb15c57bf3fde9de492466eb23853b0d` |
| `Fantozzi-StoneR1.flac` | `7073bddd2e3ba432e15a83844180c0ebc52835c2c6bab0fa902ee5952499c729` |
| `Fantozzi-StoneR2.flac` | `d66330b4a32e93b0dfdcb8c53667654de8702a0fa48b2d06c48d94dc66b3e1cc` |
| `Fantozzi-StoneR3.flac` | `b98f2c4b932242739ce8687206f7eb3313ce30afd4af7b7e148507c5cbd26787` |
| `alt-toasterstep1.wav` | `dae163ed3423ea9516159ea6ec8c4f039141dcf1ac8f78419b068e8abca751a2` |
| `alt-toasterstep2.wav` | `f013bf8d903d405231569942402f46554cbb58d8dbde9a51aecf36e4eb7d6f29` |
| `alt-toasterstep3.wav` | `b59db485a1d06e89cede2e956d83f50e7440c6a165e55d26845fa81705dd21b0` |
| `alt-toasterstep4.wav` | `81220bbab2cfa40b99648b014126a131f5faae90e1441d6784a9d8da42b9ba87` |
| `toasterstep2.wav` | `db96ade4930851d17ab1eda8a9f4a97e6ce403beb99758bd6a4444b828ed8c07` |
| `sfx100v2_footstep_wood_01.ogg` | `52465eeb853a8fa3cd9f4e218dc0b28bdcb3bb52da72f5bece5d47cae2523a20` |
| `sfx100v2_footstep_wood_02.ogg` | `e6c35ceaabd50775ba222d8c9566e807b876debf066443626a0a4906849982d4` |
| `sfx100v2_footstep_wood_03.ogg` | `c02fd155ca74ece4e5c9992f1d48c615bc768b0a8c712423d7091bc0f1207d4c` |
| `sfx100v2_footstep_wood_04.ogg` | `d229d313dc89c5c1d337f2f348d75822695530832fd10469595a3d37c53118ab` |
| `sfx100v2_wood_hit_01.ogg` | `84abc80d93011d86f29bc55c10a023265ece0851d0784d1bf07c5990f08daeab` |

Freesound needs an account to download and BigSoundBank refuses automated downloads, so neither was used directly. Thimras's [Metal footsteps on concrete](https://opengameart.org/content/metal-footsteps-on-concrete) (CC0) was not used either: it is metal shoes on concrete, not shoes on metal.

### Names

`footstep_<surface>_<foot>_<nn>.flac` and `land_<surface>.flac`, with `<surface>` one of `concrete` (the Concrete 031 and 032 floors, walls, steps, deck and ramps), `metal` (the Painted Metal 004 columns, beam and platform) and `wood` (the Planks 037 A crate), `<foot>` `left` or `right`, and `<nn>` from `01`. Concrete has three steps per foot, metal and wood two. Fantozzi's steps were recorded as left and right; the others have no side and were assigned one alternately, so a player may draw from both feet's steps of a surface. Each sidecar's identity is derived from the file's path, as the course's are, so it does not change when the files are prepared again.

### Preparation

`python3 tools/prepare_character_foley.py <folder>` turns the downloads, extracted anywhere under `<folder>`, into the committed files; it needs numpy, scipy, soundfile and pyloudnorm. Only the files listed above are read. For each one:

- **Mono**: the mean of the two channels (their correlation is 0.93 to 1.00 in every source).
- **Filter**: DC removed, then a second-order 40 Hz high-pass, run forwards and backwards.
- **Trim**: from 3 ms before the step first passes -30 dB of its peak envelope (2 ms RMS) to 10 ms after it last passes -50 dB or twice the recording's noise floor, with a 2 ms fade in and a 30 ms fade out.
- **Landing on concrete**: no CC0 recording of one could be downloaded, so it is both feet, `Fantozzi-StoneL1` and `Fantozzi-StoneR2` trimmed as above and summed 12 ms apart. The metal and wood landings are single heavier impacts recorded on the same material.
- **Loudness**: ITU-R BS.1770 (K-weighted, gated) over 100 ms blocks, as a step is shorter than one 400 ms block: -22 LUFS for the footsteps and -18 LUFS for the landings, with peaks under -1 dBFS. Three clips' first transients passed that ceiling at their loudness and were limited (3 ms look-ahead): `footstep_wood_right_02` by 2.7 dB, `land_concrete` by 1.4 dB and `footstep_wood_left_02` by 1.3 dB; no other was touched.
- **Format**: 16-bit mono FLAC with triangular dither at the source's own rate, 44.1 kHz (concrete, metal) or 48 kHz (wood), which the engine resamples as it mixes. Nothing is pitch-shifted or time-stretched: the character varies pitch and volume per step.

| File | Length | Loudness | Peak | RMS |
|---|---|---|---|---|
| `footstep_concrete_left_01.flac` | 333 ms | -22.0 LUFS | -2.2 dBFS | -22.0 dBFS |
| `footstep_concrete_left_02.flac` | 296 ms | -22.0 LUFS | -5.1 dBFS | -24.2 dBFS |
| `footstep_concrete_left_03.flac` | 300 ms | -22.0 LUFS | -6.6 dBFS | -25.2 dBFS |
| `footstep_concrete_right_01.flac` | 339 ms | -22.0 LUFS | -2.7 dBFS | -23.7 dBFS |
| `footstep_concrete_right_02.flac` | 295 ms | -22.0 LUFS | -4.6 dBFS | -23.3 dBFS |
| `footstep_concrete_right_03.flac` | 322 ms | -22.0 LUFS | -5.9 dBFS | -24.9 dBFS |
| `footstep_metal_left_01.flac` | 244 ms | -22.0 LUFS | -4.6 dBFS | -23.6 dBFS |
| `footstep_metal_left_02.flac` | 231 ms | -22.0 LUFS | -3.1 dBFS | -22.0 dBFS |
| `footstep_metal_right_01.flac` | 211 ms | -22.0 LUFS | -2.2 dBFS | -21.7 dBFS |
| `footstep_metal_right_02.flac` | 233 ms | -22.0 LUFS | -3.2 dBFS | -22.2 dBFS |
| `footstep_wood_left_01.flac` | 189 ms | -22.0 LUFS | -9.7 dBFS | -21.9 dBFS |
| `footstep_wood_left_02.flac` | 101 ms | -22.1 LUFS | -1.0 dBFS | -21.6 dBFS |
| `footstep_wood_right_01.flac` | 196 ms | -22.0 LUFS | -5.6 dBFS | -21.5 dBFS |
| `footstep_wood_right_02.flac` | 224 ms | -22.0 LUFS | -1.0 dBFS | -20.9 dBFS |
| `land_concrete.flac` | 333 ms | -18.0 LUFS | -1.0 dBFS | -19.2 dBFS |
| `land_metal.flac` | 435 ms | -18.0 LUFS | -1.7 dBFS | -21.3 dBFS |
| `land_wood.flac` | 219 ms | -18.0 LUFS | -1.8 dBFS | -17.4 dBFS |

The seventeen files take 200 KiB.
