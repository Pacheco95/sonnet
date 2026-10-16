# Per-slot material samplers

Issue [282](https://github.com/Pacheco95/sonnet/issues/282) imports the sampler of
each glTF texture independently. Material files are version 5, bundles version 15.

## Reference model

The [Khronos TextureSettingsTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TextureSettingsTest)
was imported from its GLB into a temporary project and captured with the player
on Linux using Lavapipe, SDL's offscreen driver, and `--shading-term albedo`.
The test column matches the sample column: six checkmarks and two solid green
clamp boxes. In particular, clamping, repeating and mirroring on T work alongside S.
Tone mapping and display encoding account for the colours differing from the
reference screenshot.

![TextureSettingsTest rendered by Sonnet on Lavapipe](images/texture-settings-test.png)

Model and textures: Ed Mackey / Analytical Graphics, Inc., copyright 2017,
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

## Automated coverage

- `assets_tests "[sampler]"`: all six glTF min filters and undefined, both mag
  filters and undefined, different S/T wraps, independent slots, material version
  5 round trip and migration of version 1–4 wraps to every slot's two axes.
- `renderer_tests "[sampler]"`: cache reuse and capacity; an in-memory quad with an
  8×8 checker remains black or white with nearest filtering, while linear
  filtering produces intermediate values. The same test exercises the emissive
  slot independently and samples through the default after exhausting the cache
  on a real GPU device. Unit white is approximately 231 after ACES and display
  encoding, rather than 255.
- `rhi_tests`: sampling an explicit nonzero LOD with `mipmaps = false` still reads
  level zero, while the same image with mipmaps enabled reads its grey mip.

The Vulkan checks run on Lavapipe; Windows, MoltenVK and mobile drivers were not
run locally.

## Real pixel-art asset

[Kenney Tiny Dungeon](https://kenney.nl/assets/tiny-dungeon), version 1.0,
provides the unmodified 16×16 character sprite `Tiles/tile_0096.png` used here.
The pack is CC0. Its PNG SHA-256 is
`b8a0a9376c1c3398a059a1e3226e8f8d6839c1d73ac53ea37425340d762055fd`.

A temporary glTF contains two front-facing quads sharing that PNG. The left
texture uses `magFilter = 9728`, `minFilter = 9728` (NEAREST without mipmaps);
the right uses `magFilter = 9729`, `minFilter = 9729` (LINEAR without mipmaps).
Both clamp S and T. Both materials are unlit, and alpha is opaque so the comparison
isolates RGB filtering rather than transparency. The camera's bloom strength is
zero. This exercises glTF import, texture loading and actual material sampling,
not a resized reference image.

The player captured the project on Lavapipe with SDL's offscreen driver and
`--shading-term albedo`. The left sprite retains hard texel steps; the right is
visibly blurred. Equal 430×430 interior regions contain 18 distinct RGB colours
with nearest versus 18,249 with linear. The output still passes through the
engine's texture processing, ACES and display encoding; this is not a claim of
byte equality to the source PNG's palette.

![Original Kenney sprite imported through glTF: nearest left, linear right](images/pixel-art-samplers.png)
