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

## 3. Round two: the BRDF lookup table

The screenshots showed the fault. The BRDF LUT view is yellow on the Mac, meaning bias ≈ scale ≈ 0.6. On Linux it is red: scale about 0.6, bias about 0. A new GPU test reads that view back under three skies of different colours.

1. `git pull`, then `cmake --build --preset macos-debug-local`.
2. `./build/macos-debug-local/modules/renderer/tests/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut view|passed|failed|FAILED"`: report the output verbatim. On the RTX 4090 it prints `lut view r 228 g 1 b 0` for every sky.
3. Run it again with MoltenVK's shader dump enabled (`MVK_CONFIG_SHADER_DUMP_DIR=<dir>`). Commit the dumped MSL of the `brdfLut` compute kernel (the one that writes `a / float(ibl.sampleCount), b / ...`) as `docs/agent-tasks/mvk-brdf-lut.msl`.
4. Append the results to the report, then commit and push. Raw output only.

## 4. Round three: the table's own texels

The test now also copies the table's texels out directly, without sampling, and prints six of them. This tells us whether the compute pass writes wrong values or the forward pass reads them wrongly. The table image also gained transfer-read usage, which the copy needs.

1. `git pull`, then `cmake --build --preset macos-debug-local`.
2. `./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut |passed|failed|FAILED" | sort -u`: report the output verbatim.
   On the RTX 4090 (Lavapipe matches to three decimals):
   ```
   lut texel (0, 0): 0.0726 0.8853 0.0000 1.0000
   lut texel (16, 16): 0.7334 0.0160 0.0000 1.0000
   lut texel (31, 16): 0.8794 0.0000 0.0000 1.0000
   lut texel (31, 2): 0.9995 0.0000 0.0000 1.0000
   lut texel (31, 30): 0.3569 0.0001 0.0000 1.0000
   lut texel (4, 16): 0.5747 0.0814 0.0000 1.0000
   sky (...): lut view r 228 g 1 b 0
   ```
3. Append the output to the report, then commit and push. Raw output only.

## 5. Round four: how the forward pass reads the table

Round three showed the table's texels are correct on the Mac, but the forward pass reads something else from them. Three new views read the same table in different ways, and the test prints the table's descriptor indices.

1. `git pull`, then `cmake --build --preset macos-debug-local`.
2. `./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut |index|passed|failed|FAILED" | sort -u`: report the output verbatim.
   On the RTX 4090 (Lavapipe matches):
   ```
   lut fixed view r 228 g 1 b 0
   lut load view r 227 g 1 b 0
   lut nearest view r 227 g 1 b 0
   lut sampled index 2 storage index 0
   sky (...): lut view r 228 g 1 b 0
   ```
3. Run it again with `MVK_CONFIG_SHADER_DUMP_DIR=<dir>`. Commit the dumped MSL of the **forward fragment** shader (the one containing `brdfLut` and `debugView`) as `docs/agent-tasks/mvk-forward-frag.msl`.
4. Append the output to the report, then commit and push. Raw output only.
