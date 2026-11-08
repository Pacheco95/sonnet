# Licences of the character sample's content

Every third-party file in this project is CC0 1.0 (public domain dedication), which may be copied, changed and redistributed, commercially too, with no attribution required, except the humanoid's motion-captured clips, which are CC BY 3.0: they may be copied, changed and redistributed, commercially too, as long as the credit below goes with them ([Locomotion clips](#locomotion-clips-issue-505)). The sources are credited here anyway, with what was done to them. Everything else, the course's geometry (`assets/level/course.glb`), its materials, the scene and the scripts, is the engine's own, made by `tools/generate_character_level.py`.

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

## Locomotion clips (issue #505)

The humanoid's clips other than `Stand` (`Idle`, `Walk`, `Run`, `JumpStart`, `Fall`, `Land`, `HardLand`, in `assets/character/humanoid.glb`) are motion capture retargeted onto its rig. Their footstep and landing events are in the model's sidecar, `humanoid.glb.meta`.

| Clips | Source takes | Source | Author | Licence |
|---|---|---|---|---|
| All seven | `Male1_A1_Stand`, `Male1_B3_Walk`, `Male1_C03_Run`, `Male1_C20_Run2Jump2Walk`, `Male1_B17_Walk2Hop2Walk` from `Male1_bvh.zip` | [Open Motion Project, MoCap System and Data](https://accad.osu.edu/research/motion-lab/system-data): "Male 1 (69 files): bvh" | ACCAD, The Ohio State University | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |

Credit, as the source page words it: **"Open Motion Project by ACCAD/The Ohio State University is licensed under a Creative Commons Attribution 3.0 Unported License."** CC BY 3.0 allows copying, redistributing and adapting the takes, commercially too, with that credit, a link to the licence, and a note that they were changed (retargeted, cut and looped as below). So the clips may be in this public repository and in cooked bundles; a game exported with them must carry the credit too, in its credits or a licence file beside it, and its bundle must not be locked against what the licence allows (CC BY 3.0, section 4a, technological measures). The other candidates the issue names, CMU's motion-capture database and Mixamo (excluded unless its terms allow redistribution), were not used: ACCAD's takes cover every clip under a standard licence that allows it.

Downloaded on 2026-11-07. SHA-256 of the archive as downloaded and of the takes used from it:

| File | SHA-256 |
|---|---|
| `Male1_bvh.zip` | `1cd26aeac2ee7f20e3c0fe5082e633201bb136a714ae8f684a4f34c4421f4627` |
| `Male1_A1_Stand.bvh` | `0f820d765d21e9748accbadfdd967e13b0ce71e182d03dc3129fc4d60b9271e2` |
| `Male1_B3_Walk.bvh` | `5acbd06ab7f5f0dcaa5b70c1caeba3e972e1472050ab92f9b8846ac1c8cd034e` |
| `Male1_C03_Run.bvh` | `90bec698aea33584e4792d581946c1452d7cad0a29a4ee2656df0e04613bb95a` |
| `Male1_C20_Run2Jump2Walk.bvh` | `8c5c81c1bd1d78f0e0a34907e9e9a84b9f296c55a698fd619f006ca41ff8ef44` |
| `Male1_B17_Walk2Hop2Walk.bvh` | `32782470cc24c2c6b50102d3ef141ce4d476f8e50dd7c3a05627a4a92b15700d` |

All five takes are one performer (Male 1), captured at 30 Hz in centimetres with the same 22-joint skeleton, so one reference pose serves them all. The archive has no dedicated idle loop, fall or hard landing: the idle is a stretch of the standing take, the fall and the hard landing come from the running jump, and the soft landing from the walking hop.

### Preparation

`python3 tools/retarget_character_clips.py <folder>` (Blender 4.2 or later, 5.2.2 here, and numpy) turns `Male1_bvh.zip`, anywhere under `<folder>`, into the clips: it rewrites `humanoid.glb` with `Stand` and the seven clips (removing the clips of an earlier run first, so two runs write the same bytes; meshes, skin and textures are copied unchanged) and the sidecar's `animationEvents`. Opening the project in the editor afterwards rebuilds the sidecar's sub-asset list. The script's docstring has every step; in short:

- **Import**: Blender's own BVH importer reads each take (scale 0.01, so metres) and every joint's world position and orientation is sampled at each captured frame; the rest runs outside Blender.
- **Floor**: the capture floor tilts about 1 cm a metre along the walk (planted ankles rise from step to step), so a line fitted under both ankles' heights along each take's travel is taken out of every height.
- **Retargeting**: the standing take's first frame is the reference. The humanoid is put in a matching pose (its `Stand` pose, each mapped bone turned the least that points it as the subject's bone points; the pelvis and the chest by their up and left-to-right directions, the pelvis's up being the hip joints' midpoint to the chest, since the subject's `ToSpine` sits behind its hips and aiming at it tipped the pelvis forward), and at every frame each mapped bone's world orientation is the subject's bone's change from the reference applied to that pose. `Hips`, `ToSpine`, `Spine`, `Spine1`, `Neck`, `Head` map to `pelvis`, `spine_01..03`, `neck_01`, `head`; `LeftShoulder`, `LeftArm`, `LeftForeArm`, `LeftHand` to `clavicle_l`, `upperarm_l`, `lowerarm_l`, `hand_l`; `LeftUpLeg`, `LeftLeg`, `LeftFoot`, `LeftToeBase` to `thigh_l`, `calf_l`, `foot_l`, `ball_l`, and the right side alike. The fingers keep `Stand`'s pose (the takes have none). The hips travel as the subject's, scaled by the legs' length ratio, 0.98, and each leg is solved by two-bone inverse kinematics so the ankle follows the subject's (scaled), since the humanoid's thigh and shin are not in the subject's proportion.
- **Facing and metres**: each take is turned about the vertical so it travels (or, standing, faces) along -Z, the engine's forward, with the character's left on -X, as the model is.
- **Foot height**: the shoes are skinned from the model's mesh and weights at every frame. Each take is lifted so both shoes stand at y = 0 (the 10th percentile of each shoe's lowest vertex; the two shoes do not come out at the same height, 1 to 5 cm apart depending on the take, so each foot is levelled on its own), then a planted shoe is moved onto the ground and one below it out, through its ankle's goal, smoothed over five frames. No shoe goes more than 2 mm below the ground in any clip.
- **Contacts**: a shoe is planted while its lowest vertex is within 2.5 cm of the ground and its heel or toes move slower than 0.6 m/s; the frame it comes down is a footstep. These are the frames the events and cycles below use.
- **Clips**, all in place (the horizontal travel taken out, since the character's capsule moves the model, [ADR-0038](../../../docs/decisions/0038-character-controller.md)):

| Clip | Take, frames | Length | Loops | Events |
|---|---|---|---|---|
| `Idle` | Stand, 28 to 128: the 3.3 s stretch whose ends match best (0.6° at most apart), the rest spread over it | 3.333 s | yes | |
| `Walk` | Walk, 48 to 83: left footstep to left footstep, the right at frame 65 | 1.167 s | yes | `footstep` left 0, right 0.583 |
| `Run` | Run, 20 to 40: left to left, the right at frame 29 | 0.667 s | yes | `footstep` left 0, right 0.333 |
| `JumpStart` | Running jump, 18 to 35: the crouch, the take-off (the feet leave the ground at frame 33) and two frames after, the rise taken out | 0.567 s | no | |
| `Fall` | Running jump, 39 to 45 and back: the descent from the top, played forwards then backwards, its height taken out, the shoes 2 cm above the bottom | 0.4 s | yes | |
| `Land` | Walking hop, 110 to 134: from a frame before the right foot touches (111), the pelvis 11 cm down, until it is back nine tenths of the way to its walking height | 0.8 s | no | `landing` 0.033, `footstep` left 0.6 |
| `HardLand` | Running jump, 46 to 68: from a frame before the left foot touches (47), both feet down, the pelvis 24 cm down, until it is back nine tenths of the way | 0.733 s | no | `landing` (argument `hard`) 0.033, `footstep` left 0.333 |

- **Cycles**: the key times are warped piecewise linearly so the right footstep falls at half the cycle, as T4's phase synchronisation needs of walk and run alike (left down at 0, right at 0.5). The captured right footsteps fall at 0.514 of the walk's cycle and 0.45 of the run's, so the warp moves them by 1.4% and 5% of the cycle. What the last key differs from the first is spread over the cycle so the clip loops: at most 19.8° on one joint for the walk (the right shin, in swing) and 26.7° for the run (the right toes; the run take is two strides long and the two are not alike), and the pelvis's height.
- **Strides** (metres per cycle, the hips' travel over the cycle in the take before it is put in place; T9's speed-matched playback divides by them): `Walk` **1.505 m** per 1.167 s, 1.29 m/s; `Run` **3.235 m** per 0.667 s, 4.85 m/s. Measured from the feet instead (each foot's planted position a cycle apart) they are 1.44 and 1.44 m for the walk and 3.27 and 3.10 m for the run, the take's speed not being quite constant.
- **Sliding**: in the takes as retargeted (before they are put in place), a planted heel or toe moves at most 1.3 cm (walk) and 1.5 cm (run) over a contact.
- **Keys**: one per captured frame (30 Hz), linear; a rotation for each of the 53 joints and the pelvis's translation, about 290 KB for the seven clips.
