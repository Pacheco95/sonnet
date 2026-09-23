# Mac lighting probe

Probe branch `agents/mac-lighting-probes`. It is not for merging. On the M4 Max, the basic sample renders washed out and without sun shadows (compare with Linux). This run decides whether the bug is in MoltenVK or in the engine. Report **raw observations and numbers**, not conclusions.

## 0. Rule out a stale build

1. `git fetch && git checkout agents/mac-lighting-probes`, then report `git rev-parse HEAD`.
2. Rebuild clean: `rm -rf build/macos-debug && cmake --preset macos-debug && cmake --build --preset macos-debug`. The shaders are compiled at build time, so old SPIR-V next to the binary would hide the shadow workaround.
3. Run `./build/macos-debug/apps/editor/sonnet_editor apps/samples/basic` and take a screenshot of the viewport (View > Shading term > Final). Report whether shadows appear.

## 1. The same build on KosmicKrisp

The Vulkan SDK ships LunarG's KosmicKrisp driver beside MoltenVK. It runs on Metal 4 and Apple Silicon only.

1. Find its ICD manifest in the SDK (`find ~/VulkanSDK -iname '*kosmickrisp*.json'`, or wherever the SDK lives). Report the path and the SDK version.
2. `VK_DRIVER_FILES=<that json> vulkaninfo --summary`: report `apiVersion`, `driverName` and `driverVersion`.
3. `VK_DRIVER_FILES=<that json> vulkaninfo | grep -iE 'drawIndirectCount|samplerFilterMinmax|descriptorIndexing|bufferDeviceAddress|dynamicRendering|synchronization2|maintenance5|pushDescriptor'`: report the output.
4. If the apiVersion is 1.4.x:
   - `VK_DRIVER_FILES=<that json> ./build/macos-debug/apps/editor/sonnet_editor apps/samples/basic`, then take a screenshot of the viewport, as in step 0.3.
   - `VK_DRIVER_FILES=<that json> ctest --preset macos-debug --output-on-failure`, then report the failing tests with their output.
   - If the editor fails to start, report the log. The device-selection error lists vk-bootstrap's per-device reasons.
5. If it is 1.3 only, the engine will refuse it (Vulkan 1.4 only). Report that and go on to section 2.

## 2. Shading terms on MoltenVK

Run the editor on MoltenVK (the default driver) with `apps/samples/basic`. Don't move the camera between shots. For each entry of View > Shading term, take a viewport screenshot:

Final, Albedo, Normal, Sun direct, Shadow factor, IBL diffuse, IBL specular, BRDF LUT.

The same eight will be taken on Linux from the same camera. Save the screenshots under `docs/agent-tasks/mac-lighting-probe/` as `<term>.png` (lower case, hyphens) and commit them to this branch together with a short report.
