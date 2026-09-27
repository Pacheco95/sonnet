# Test data

Images the `assets` tests read from the source tree, found by walking up from the test binary.

| File | What it is | Source | Licence |
|---|---|---|---|
| `flight_helmet_base_color.png` | `FlightHelmet_Materials_GlassPlasticMat_BaseColor.png`, downscaled from 2048×2048 to 512×512 with a box filter | [Flight Helmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/FlightHelmet), Khronos glTF-Sample-Assets, by Gary Hsu (2018) | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) |
| `flight_helmet_normal.png` | `FlightHelmet_Materials_GlassPlasticMat_Normal.png`, downscaled the same way | The same | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) |

The ASTC quality test in `TextureTests.cpp` encodes the first as sRGB at 6×6 and the second as linear at 4×4 ([ADR-0018](../../../../docs/decisions/0018-mobile-export.md)), decodes both back and holds each above a PSNR floor. CC0 asks for no attribution; the credit is given anyway.
