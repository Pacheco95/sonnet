# MoltenVK integer-attachment warning

Investigation of [Sonnet issue 59](https://github.com/Pacheco95/sonnet/issues/59), 2026-09-29, at commit `3e033d3dddb68b92fca7398c6abd997692753ee1`.

## Finding

Sonnet disables blending for the id and selection-mask pipelines. `Renderer.cpp` leaves their blend mode at `BlendMode::None`; `VulkanTypes.cpp` translates that to `VK_FALSE`, and `VulkanDevice::createGraphicsPipeline` passes that state to Vulkan.

MoltenVK 1.4.2's `MVKGraphicsPipeline::addFragmentOutputToPipeline` warns for a non-blendable attachment format even when the pipeline disables blending. The condition does not depend on the colour write mask or an earlier rendering pass. A single pipeline created for `R32Uint` reproduces it. The warning occurs during pipeline creation, rather than once per rendered frame.

[Upstream issue 2811](https://github.com/KhronosGroup/MoltenVK/issues/2811) is closed by [commit f92b64c3a5084c6cfa78f581e0b863b40d2d16fa](https://github.com/KhronosGroup/MoltenVK/commit/f92b64c3a5084c6cfa78f581e0b863b40d2d16fa), which adds `pCA->blendEnable` to the warning condition. The latest published release on the investigation date is still 1.4.2. Sonnet's [overlay port](../../ports/moltenvk/portfile.cmake) installs prebuilt release archives, so that fix is not yet in the bundled player.

## Verification

Machine: Apple M4 Max, macOS 26.7 (25G229). Existing `macos-debug-local` Debug test binaries, with the SDK's Vulkan loader 1.4.341 and Khronos validation, including synchronization validation, enabled. No engine code or driver binaries were modified.

| Driver | Test | Result |
|---|---|---|
| SDK MoltenVK 1.4.1, Vulkan 1.4.334 | `rhi_tests "[id]"` | All 7 assertions pass; no warning |
| SDK MoltenVK 1.4.1 | `renderer_tests "[picking][gpu]"` | All 4,113 assertions in 3 cases pass; no warning |
| Homebrew MoltenVK 1.4.2, Vulkan 1.4.357, through the SDK loader | `rhi_tests "[id]"` | All 6 shader-load and pixel assertions pass; the fixture's seventh assertion fails because the diagnostic count is 1 |
| Homebrew MoltenVK 1.4.2, loaded directly without the validation layer | `rhi_tests "[id]"` | The same warning and assertion failure |

The existing test in [IdImageTests.cpp](../../modules/rhi/tests/IdImageTests.cpp) writes `0xABCDEF01` into an integer attachment, checks the clear value `7` outside the triangle, and checks that a later pass reads the id correctly. Its [fixture](../../modules/rhi/tests/TestDevice.h) requires zero validation messages, so it already detects this driver regression without weakening the assertion or adding a duplicate test.

To reproduce with validation, point SDL at the SDK loader and `VK_DRIVER_FILES` at a manifest for the driver version being tested:

```sh
env SDL_VULKAN_LIBRARY=/path/to/sdk/macOS/lib/libvulkan.1.dylib \
  VK_DRIVER_FILES=/path/to/MoltenVK_icd.json \
  VK_ADD_LAYER_PATH=/path/to/sdk/macOS/share/vulkan/explicit_layer.d \
  ./build/macos-debug-local/modules/rhi/rhi_tests "[id]"
```

For the 1.4.2 comparison, a temporary copy of the SDK's manifest used an absolute `ICD.library_path` of `/opt/homebrew/Cellar/molten-vk/1.4.2/lib/libMoltenVK.dylib`. Verify the logged driver version and `validation on`; otherwise the run may use a different driver or skip for lack of a device. macOS sandbox restrictions can prevent Metal access and cause a skip, which is not a passing GPU test.

No iPhone run, patched-driver build or fixed-release test was performed. Passing on 1.4.1 establishes the version difference, not verification of the upstream fix.

## Release follow-up

Keep Sonnet issue 59 open until a published MoltenVK release containing the upstream fix is adopted. Update the overlay's version and both macOS and iOS archive hashes, then run the id and picking GPU tests with that driver and validation enabled. Repeat the macOS and iPhone player captures required by [ADR-0018](../decisions/0018-mobile-export.md#device-checks-and-reports), confirming correct images and no integer-attachment warning. The editor and tests load an external driver, whereas the Apple player links the port's static library, so testing only the SDK driver does not verify the bundled one.
