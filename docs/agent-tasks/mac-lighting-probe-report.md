# Mac lighting probe — report

## Build information

| Field | Value |
|---|---|
| `git rev-parse HEAD` | `2e5f581c6639b258779e1092ad46dd6c97553021` |
| LunarG Vulkan SDK version | 1.4.341.1 |
| MoltenVK version | 1.4.1 |
| MoltenVK Vulkan API version | 1.4.334 |
| Vulkan loader version | 1.4.357 |
| CMake preset used | `macos-debug-local` (Xcode Apple Clang 17; `macos-debug` was specified in the task doc but that preset does not pin the compiler, and Homebrew LLVM 22 is first on PATH on this machine, which causes libc++ incompatibilities per `CLAUDE.local.md`) |

---

## Section 0 — Clean rebuild

Commands run:

```
rm -rf build/macos-debug-local
export VCPKG_ROOT=~/vcpkg
cmake --preset macos-debug-local
cmake --build --preset macos-debug-local
```

Build result: **succeeded**, 296/296 targets. Compiler: AppleClang 17.0.0.17000604.

### Editor launch (MoltenVK, default driver)

```
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
./build/macos-debug-local/apps/editor/sonnet_editor apps/samples/basic
```

Log (process ran for ~12 s then received SIGTERM):

```
[15:24:50.691] [info] [platform] [SdlEntryPoint.cpp:44] Sonnet 0.10.0
[15:24:50.739] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "cocoa"
[15:24:51.081] [debug] [platform] [SdlWindow.cpp:28] window "Sonnet Editor" created: 1600x900 logical, 1600x900 pixels
[15:24:51.082] [debug] [platform] [Platform.cpp:68] Vulkan loader "/Users/michael/repositories/sonnet/build/macos-debug-local/vcpkg_installed/arm64-osx/lib/libvulkan.1.4.357.dylib" kept mapped for the process
[15:24:51.143] [debug] [rhi] [VulkanDevice.cpp:1232] WARNING-CreateInstance-status-message: vkCreateInstance(): Khronos Validation Layer Active:
    Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT.
    Current Disables: None.

[15:24:51.146] [debug] [rhi] [VulkanDevice.cpp:177] Vulkan loader 1.4.357
[15:24:51.160] [info] [rhi] [VulkanDevice.cpp:99] Vulkan 1.4.334 device "Apple M4 Max", driver MoltenVK 1.4.1, loader 1.4.357, validation on
[15:24:51.163] [debug] [rhi] [VulkanSwapchain.cpp:94] swapchain 1600x900, 3 images, B8G8R8A8Unorm, Fifo
[15:24:51.163] [debug] [core] [JobSystem.cpp:78] job system started with 13 workers
[15:24:51.184] [debug] [ui] [ImGuiLayer.cpp:115] Dear ImGui 1.92.9b with SDL3 and Vulkan backends, docking, viewports
[15:24:51.308] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "light clustering" from shader "cluster"
[15:24:51.309] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "cull" from shader "cull"
[15:24:51.310] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "clear draw counts" from shader "cull"
[15:24:51.311] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "debug lines" from shader "debug"
[15:24:51.464] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "depth" from shader "depth"
[15:24:51.537] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "shadow" from shader "depth"
[15:24:51.538] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "depth double sided" from shader "depth"
[15:24:51.538] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "shadow" from shader "depth"
[15:24:51.791] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "forward" from shader "forward"
[15:24:51.822] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "forward blend" from shader "forward"
[15:24:51.822] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "forward double sided" from shader "forward"
[15:24:51.823] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "forward blend double sided" from shader "forward"
[15:24:51.827] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "equirect to cube" from shader "ibl"
[15:24:51.828] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "cube mip" from shader "ibl"
[15:24:51.828] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "irradiance" from shader "ibl"
[15:24:51.829] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "prefilter" from shader "ibl"
[15:24:51.830] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "brdf lut" from shader "ibl"
[15:24:51.926] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "id" from shader "id"
[15:24:51.926] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "selection mask" from shader "id"
[15:24:51.927] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "id double sided" from shader "id"
[15:24:51.927] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "selection mask double sided" from shader "id"
[15:24:51.928] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "outline" from shader "outline"
[15:24:51.932] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "bloom downsample" from shader "post"
[15:24:51.933] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "bloom upsample" from shader "post"
[15:24:51.934] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "tonemap" from shader "post"
[15:24:51.935] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "fxaa" from shader "post"
[15:24:51.936] [debug] [rhi] [VulkanDevice.cpp:931] compute pipeline "skinning" from shader "skin"
[15:24:52.055] [debug] [rhi] [VulkanDevice.cpp:915] pipeline "skybox" from shader "skybox"
[15:24:52.057] [debug] [renderer] [Renderer.cpp:568] texture "white": 1x1, 1 levels
[15:24:52.057] [debug] [renderer] [Renderer.cpp:568] texture "flat normal": 1x1, 1 levels
[15:24:52.057] [debug] [renderer] [Renderer.cpp:319] renderer ready, shaders from /Users/michael/repositories/sonnet/build/macos-debug-local/apps/editor/shaders
[15:24:52.068] [info] [world] [World.cpp:46] flecs explorer enabled: https://www.flecs.dev/explorer
[15:24:52.070] [debug] [world] [World.cpp:103] world ready with 13 components
[15:24:52.072] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[15:24:52.073] [debug] [scripting] [LuaScriptRuntime.cpp:216] scripting ready, Lua 5.5.1
[15:24:52.073] [debug] [world] [Animation.cpp:59] animation ready
[15:24:52.133] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[15:24:52.143] [debug] [world] [World.cpp:103] world ready with 13 components
[15:24:52.145] [info] [editor] [Editor.cpp:61] editor ready
[15:24:52.145] [info] [assets] [Project.cpp:38] opened project "Basic" at /Users/michael/repositories/sonnet/apps/samples/basic
[15:24:52.149] [info] [assets] [AssetDatabase.cpp:209] asset database: 27 assets in /Users/michael/repositories/sonnet/apps/samples/basic
[15:24:52.150] [info] [world] [Scene.cpp:312] loaded prefab c0a7e4d2-5b1f-4c3e-9a8d-1f2e3d4c5b6a from /Users/michael/repositories/sonnet/apps/samples/basic/prefabs/crate.prefab.json
[15:24:52.150] [info] [world] [Scene.cpp:312] loaded prefab c0a7e4d2-5b1f-4c3e-9a8d-2f3e4d5c6b7a from /Users/michael/repositories/sonnet/apps/samples/basic/prefabs/physics-crate.prefab.json
[15:24:52.153] [info] [world] [Scene.cpp:254] loaded 15 entities from /Users/michael/repositories/sonnet/apps/samples/basic/scenes/main.scene.json
[15:24:52.185] [debug] [renderer] [RenderTarget.cpp:38] render target "viewport" 32x13
[15:24:52.186] [debug] [assets] [GltfImporter.cpp:436] /Users/michael/repositories/sonnet/apps/samples/basic/assets/models/reed.glb: 1 meshes, 1 materials, 0 images, 6 nodes, 1 skins, 1 animations
[15:24:52.187] [debug] [renderer] [Renderer.cpp:513] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[15:24:52.187] [debug] [renderer] [Renderer.cpp:513] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[15:24:52.188] [debug] [renderer] [Renderer.cpp:513] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[15:24:52.188] [debug] [renderer] [Renderer.cpp:513] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[15:24:52.188] [debug] [renderer] [Renderer.cpp:568] texture "pending": 1x1, 1 levels
[15:24:52.188] [debug] [renderer] [Renderer.cpp:513] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[15:24:52.189] [debug] [assets] [GltfImporter.cpp:436] /Users/michael/repositories/sonnet/apps/samples/basic/assets/models/beacon.glb: 2 meshes, 2 materials, 0 images, 3 nodes, 0 skins, 1 animations
[15:24:52.189] [debug] [assets] [GltfImporter.cpp:436] /Users/michael/repositories/sonnet/apps/samples/basic/assets/models/crate.glb: 2 meshes, 2 materials, 2 images, 2 nodes, 0 skins, 0 animations
[15:24:52.192] [debug] [renderer] [Renderer.cpp:677] environment "sky" from a 256x128 map
[15:24:52.194] [debug] [rhi] [VulkanSwapchain.cpp:94] swapchain 1600x900, 3 images, B8G8R8A8Unorm, Fifo
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "ids" 32x13 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 32x13 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 16x6 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 8x3 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 4x1 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 2x1 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 1x1 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 2x1 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 4x1 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 8x3 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 16x6 allocated
[15:24:52.194] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 32x13 allocated
[15:24:52.229] [debug] [renderer] [RenderTarget.cpp:38] render target "viewport" 954x662
[15:24:52.229] [debug] [renderer] [Renderer.cpp:513] mesh "reed/StemMesh": 91 vertices, 144 triangles, 1 submeshes
[15:24:52.229] [debug] [renderer] [Renderer.cpp:513] mesh "beacon/LampMesh": 24 vertices, 12 triangles, 1 submeshes
[15:24:52.229] [debug] [renderer] [Renderer.cpp:513] mesh "beacon/HaloMesh": 325 vertices, 528 triangles, 1 submeshes
[15:24:52.229] [debug] [renderer] [Renderer.cpp:568] texture "checker": 64x64, 7 levels
[15:24:52.229] [debug] [renderer] [Renderer.cpp:568] texture "crate/image 0": 128x128, 8 levels
[15:24:52.229] [debug] [renderer] [Renderer.cpp:568] texture "crate/image 1": 128x128, 8 levels
[15:24:52.229] [debug] [renderer] [Renderer.cpp:513] mesh "crate/CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[15:24:52.229] [debug] [renderer] [Renderer.cpp:513] mesh "crate/BallMesh": 561 vertices, 960 triangles, 1 submeshes
[15:24:52.230] [debug] [renderer] [Renderer.cpp:677] environment "sky" already loaded
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "ids" 954x662 allocated
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 954x662 allocated
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 477x331 allocated
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 238x165 allocated
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 119x82 allocated
[15:24:52.230] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 59x41 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 29x20 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 59x41 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 119x82 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 238x165 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 477x331 allocated
[15:24:52.231] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 954x662 allocated
[15:24:52.237] [debug] [renderer] [RenderTarget.cpp:38] render target "viewport" 968x662
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "ids" 968x662 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 968x662 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 484x331 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 242x165 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 121x82 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 60x41 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 30x20 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 60x41 allocated
[15:24:52.238] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 121x82 allocated
[15:24:52.239] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 242x165 allocated
[15:24:52.239] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 484x331 allocated
[15:24:52.239] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 968x662 allocated
[15:24:52.241] [debug] [renderer] [RenderTarget.cpp:38] render target "viewport" 954x662
[15:24:52.244] [debug] [renderer] [RenderTarget.cpp:38] render target "viewport" 968x662
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "ids" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene hdr" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 0" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 1" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 2" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 3" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 4" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 3" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 2" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 1" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 0" released
[15:24:52.245] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene ldr" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "ids" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene hdr" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 0" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 1" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 2" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 3" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 4" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 3" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 2" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 1" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 0" released
[15:24:52.295] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene ldr" released
[15:25:01.600] [info] [platform] [SdlEntryPoint.cpp:83] exit ok
```

Result: **editor started successfully on MoltenVK**. All pipelines compiled and loaded without errors.

---

## Section 1 — KosmicKrisp driver

### ICD manifest location

Found at two paths:
- `/Users/michael/VulkanSDK/1.4.341.1/macOS/share/vulkan/icd.d/libkosmickrisp_icd.json` (SDK install)
- `/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json` (system-wide, same content)

ICD manifest contents:

```json
{
    "ICD": {
        "api_version": "1.3.0",
        "library_path": "../../../lib/libvulkan_kosmickrisp.dylib"
    },
    "file_format_version": "1.0.0"
}
```

SDK version (from path): **1.4.341.1**

### `vulkaninfo --summary` with KosmicKrisp

Command:
```
VK_DRIVER_FILES=/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
vulkaninfo --summary
```

Output:
```
==========
VULKANINFO
==========

Vulkan Instance Version: 1.4.341


Instance Extensions: count = 16
-------------------------------
VK_EXT_debug_report                    : extension revision 10
VK_EXT_debug_utils                     : extension revision 2
VK_EXT_metal_surface                   : extension revision 1
VK_EXT_surface_maintenance1            : extension revision 1
VK_EXT_swapchain_colorspace            : extension revision 5
VK_KHR_device_group_creation           : extension revision 1
VK_KHR_external_fence_capabilities     : extension revision 1
VK_KHR_external_memory_capabilities    : extension revision 1
VK_KHR_external_semaphore_capabilities : extension revision 1
VK_KHR_get_physical_device_properties2 : extension revision 2
VK_KHR_get_surface_capabilities2       : extension revision 1
VK_KHR_portability_enumeration         : extension revision 1
VK_KHR_surface                         : extension revision 25
VK_KHR_surface_maintenance1            : extension revision 1
VK_KHR_surface_protected_capabilities  : extension revision 1
VK_LUNARG_direct_driver_loading        : extension revision 1

Instance Layers: count = 7
--------------------------
VK_LAYER_KHRONOS_profiles         Khronos Profiles layer                     1.4.341  version 1
VK_LAYER_KHRONOS_shader_object    Khronos Shader object layer                1.4.341  version 1
VK_LAYER_KHRONOS_synchronization2 Khronos Synchronization2 layer             1.4.341  version 1
VK_LAYER_KHRONOS_validation       Khronos Validation Layer                   1.4.341  version 1
VK_LAYER_LUNARG_api_dump          LunarG API dump layer                      1.4.341  version 2
VK_LAYER_LUNARG_gfxreconstruct    GFXReconstruct Capture Layer Version 1.0.5 1.4.341  version 4194309
VK_LAYER_LUNARG_screenshot        LunarG image capture layer                 1.4.341  version 1

Devices:
========
GPU0:
	apiVersion         = 1.3.340
	driverVersion      = 26.0.99
	vendorID           = 0x106b
	deviceID           = 0x0064
	deviceType         = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
	deviceName         = Apple M4 Max
	driverID           = DRIVER_ID_MESA_KOSMICKRISP
	driverName         = KosmicKrisp
	driverInfo         = vulkan-sdk-1.4.341.1
	conformanceVersion = 1.4.3.2
	deviceUUID         = 7c040000-0100-0000-0000-000000000000
	driverUUID         = 258801e9-778a-3af1-b03f-46cef7603a2a
```

`apiVersion = 1.3.340` — not 1.4.x. ctest suite was not run per task doc section 1.5.

### Feature grep with KosmicKrisp

Command:
```
VK_DRIVER_FILES=/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
vulkaninfo | grep -iE 'drawIndirectCount|samplerFilterMinmax|descriptorIndexing|bufferDeviceAddress|dynamicRendering|synchronization2|maintenance5|pushDescriptor'
```

Output:
```
VK_LAYER_KHRONOS_synchronization2 (Khronos Synchronization2 layer) Vulkan version 1.4.341, layer version 1:
			VK_KHR_synchronization2 : extension revision 1
	maxDrawIndirectCount                            = 4294967295
VkPhysicalDevicePushDescriptorPropertiesKHR:
	maxPushDescriptors = 32
	VK_KHR_synchronization2                     : extension revision 1
	drawIndirectCount                                  = false
	descriptorIndexing                                 = true
	samplerFilterMinmax                                = false
	bufferDeviceAddress                                = true
	bufferDeviceAddressCaptureReplay                   = false
	bufferDeviceAddressMultiDevice                     = false
	synchronization2                                   = true
	dynamicRendering                                   = true
```

### Editor launch with KosmicKrisp

Command:
```
VK_DRIVER_FILES=/usr/local/share/vulkan/icd.d/libkosmickrisp_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
./build/macos-debug-local/apps/editor/sonnet_editor apps/samples/basic
```

Log:
```
[15:26:38.856] [info] [platform] [SdlEntryPoint.cpp:44] Sonnet 0.10.0
[15:26:38.893] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "cocoa"
[15:26:39.100] [debug] [platform] [SdlWindow.cpp:28] window "Sonnet Editor" created: 1600x900 logical, 1600x900 pixels
[15:26:39.100] [debug] [platform] [Platform.cpp:68] Vulkan loader "/Users/michael/repositories/sonnet/build/macos-debug-local/vcpkg_installed/arm64-osx/lib/libvulkan.1.4.357.dylib" kept mapped for the process
[15:26:39.118] [debug] [rhi] [VulkanDevice.cpp:1232] WARNING-CreateInstance-status-message: vkCreateInstance(): Khronos Validation Layer Active:
    Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT.
    Current Disables: None.

[15:26:39.118] [debug] [rhi] [VulkanDevice.cpp:177] Vulkan loader 1.4.357
[15:26:39.140] [critical] [platform] [SdlEntryPoint.cpp:54] startup failed: selecting a Vulkan 1.4 device: no_suitable_device
  Physical Device Apple M4 Max not selected due to: VkPhysicalDeviceProperties::apiVersion 1.3 lower than required version 1.4 (Graphics, VulkanDevice.cpp:57)
[15:26:39.140] [info] [platform] [SdlEntryPoint.cpp:83] exit with failure
```

Result: **editor refused to start**. vk-bootstrap rejected Apple M4 Max because KosmicKrisp reports `apiVersion = 1.3`, below the engine's Vulkan 1.4 requirement.

---

## MoltenVK vulkaninfo --summary (reference)

Command:
```
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
vulkaninfo --summary
```

Output:
```
==========
VULKANINFO
==========

Vulkan Instance Version: 1.4.341


Instance Extensions: count = 19
-------------------------------
VK_EXT_debug_report                    : extension revision 10
VK_EXT_debug_utils                     : extension revision 2
VK_EXT_headless_surface                : extension revision 1
VK_EXT_layer_settings                  : extension revision 2
VK_EXT_metal_surface                   : extension revision 1
VK_EXT_surface_maintenance1            : extension revision 1
VK_EXT_swapchain_colorspace            : extension revision 5
VK_KHR_device_group_creation           : extension revision 1
VK_KHR_external_fence_capabilities     : extension revision 1
VK_KHR_external_memory_capabilities    : extension revision 1
VK_KHR_external_semaphore_capabilities : extension revision 1
VK_KHR_get_physical_device_properties2 : extension revision 2
VK_KHR_get_surface_capabilities2       : extension revision 1
VK_KHR_portability_enumeration         : extension revision 1
VK_KHR_surface                         : extension revision 25
VK_KHR_surface_maintenance1            : extension revision 1
VK_KHR_surface_protected_capabilities  : extension revision 1
VK_LUNARG_direct_driver_loading        : extension revision 1
VK_MVK_macos_surface                   : extension revision 3

Instance Layers: count = 7
--------------------------
VK_LAYER_KHRONOS_profiles         Khronos Profiles layer                     1.4.341  version 1
VK_LAYER_KHRONOS_shader_object    Khronos Shader object layer                1.4.341  version 1
VK_LAYER_KHRONOS_synchronization2 Khronos Synchronization2 layer             1.4.341  version 1
VK_LAYER_KHRONOS_validation       Khronos Validation Layer                   1.4.341  version 1
VK_LAYER_LUNARG_api_dump          LunarG API dump layer                      1.4.341  version 2
VK_LAYER_LUNARG_gfxreconstruct    GFXReconstruct Capture Layer Version 1.0.5 1.4.341  version 4194309
VK_LAYER_LUNARG_screenshot        LunarG image capture layer                 1.4.341  version 1

Devices:
========
GPU0:
	apiVersion         = 1.4.334
	driverVersion      = 0.2.2209
	vendorID           = 0x106b
	deviceID           = 0x1a060209
	deviceType         = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
	deviceName         = Apple M4 Max
	driverID           = DRIVER_ID_MOLTENVK
	driverName         = MoltenVK
	driverInfo         = 1.4.1
	conformanceVersion = 1.4.4.0
	deviceUUID         = 0000106b-1a06-0209-0000-000000000000
	driverUUID         = 4d564b00-0000-28a1-1a06-020900000000
```

---

## Observations

- KosmicKrisp `driverInfo = vulkan-sdk-1.4.341.1` and both ICD manifest files declare `api_version: 1.3.0`, confirming the driver is Vulkan 1.3 only regardless of the SDK version in the filename.
- KosmicKrisp `drawIndirectCount = false`. MoltenVK also lacks `drawIndirectCount` on this device (per ADR-0014, the engine already uses `drawIndexedIndirect` with zero-instance slots instead of `drawIndirectCount` on MoltenVK).
- KosmicKrisp `samplerFilterMinmax = false`; MoltenVK status on this feature is not shown in the summary above (full grep not run for MoltenVK in this session).
- Section 2 (shading-term screenshots on MoltenVK) was not run; it is to be done by the user.

---

## Section 3 — Round two: the BRDF lookup table

### Build

```
cmake --build --preset macos-debug-local
```

3 targets rebuilt (new `RendererTests.cpp.o` + relinked `renderer_tests`).

### Test run 1 — filtered output

Command:
```
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut view|passed|failed|FAILED"
```

Output:
```
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
modules/renderer/tests/RendererTests.cpp:1147: FAILED:
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
modules/renderer/tests/RendererTests.cpp:1147: FAILED:
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
modules/renderer/tests/RendererTests.cpp:1147: FAILED:
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
test cases: 1 | 1 failed
assertions: 6 | 3 passed | 3 failed
```

All three skies: `r 221 g 221 b 0`. Reference (RTX 4090): `r 228 g 1 b 0`.

### Test run 2 — MSL shader dump

Command:
```
mkdir -p /tmp/mvk-shader-dump
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
MVK_CONFIG_SHADER_DUMP_DIR=/tmp/mvk-shader-dump \
./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s
```

Dumped compute shaders: `shader-cs-038cb1fef6a959b1.metal`, `shader-cs-3baee2ef358b806d.metal`, `shader-cs-76944937dda8d7bb.metal`, `shader-cs-ce2e4773e6632aa3.metal`.

The `brdfLut` kernel was identified by the presence of `ibl.sampleCount` and the write `float4(a / float(ibl.sampleCount), b / float(ibl.sampleCount), ...)`: file `shader-cs-ce2e4773e6632aa3.metal`. Committed as `docs/agent-tasks/mvk-brdf-lut.msl`.

---

## Section 4 — Round three: the table's own texels

### Build

```
cmake --build --preset macos-debug-local
```

56 targets processed (renderer_tests and downstream targets rebuilt).

### Test run — texel readback and filtered output

Command:
```
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut |passed|failed|FAILED" | sort -u
```

Output:
```
  lut texel (0, 0): 0.0737 0.8989 0.0000 1.0000
  lut texel (16, 16): 0.7334 0.0160 0.0000 1.0000
  lut texel (31, 16): 0.8794 0.0000 0.0000 1.0000
  lut texel (31, 2): 0.9995 0.0000 0.0000 1.0000
  lut texel (31, 30): 0.3569 0.0001 0.0000 1.0000
  lut texel (4, 16): 0.5747 0.0814 0.0000 1.0000
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
assertions: 6 | 3 passed | 3 failed
modules/renderer/tests/RendererTests.cpp:1175: FAILED:
test cases: 1 | 1 failed
```

Reference (RTX 4090):
```
lut texel (0, 0): 0.0726 0.8853 0.0000 1.0000
lut texel (16, 16): 0.7334 0.0160 0.0000 1.0000
lut texel (31, 16): 0.8794 0.0000 0.0000 1.0000
lut texel (31, 2): 0.9995 0.0000 0.0000 1.0000
lut texel (31, 30): 0.3569 0.0001 0.0000 1.0000
lut texel (4, 16): 0.5747 0.0814 0.0000 1.0000
sky (...): lut view r 228 g 1 b 0
```

---

## Section 5 — Round four: how the forward pass reads the table

### Build

```
cmake --build --preset macos-debug-local
```

67 targets processed.

### Test run 1 — new views and descriptor indices

Command:
```
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s 2>&1 | grep -E "lut |index|passed|failed|FAILED" | sort -u
```

Output:
```
  lut fixed view r 228 g 228 b 0
  lut load view r 221 g 221 b 0
  lut nearest view r 221 g 221 b 0
  lut sampled index 2 storage index 0
  lut texel (0, 0): 0.0737 0.8989 0.0000 1.0000
  lut texel (16, 16): 0.7334 0.0160 0.0000 1.0000
  lut texel (31, 16): 0.8794 0.0000 0.0000 1.0000
  lut texel (31, 2): 0.9995 0.0000 0.0000 1.0000
  lut texel (31, 30): 0.3569 0.0001 0.0000 1.0000
  lut texel (4, 16): 0.5747 0.0814 0.0000 1.0000
  sky (0.1, 0.3, 0.9): lut view r 221 g 221 b 0
  sky (0.1, 0.9, 0.1): lut view r 221 g 221 b 0
  sky (0.9, 0.1, 0.1): lut view r 221 g 221 b 0
assertions: 6 | 3 passed | 3 failed
modules/renderer/tests/RendererTests.cpp:1188: FAILED:
test cases: 1 | 1 failed
```

Reference (RTX 4090):
```
lut fixed view r 228 g 1 b 0
lut load view r 227 g 1 b 0
lut nearest view r 227 g 1 b 0
lut sampled index 2 storage index 0
sky (...): lut view r 228 g 1 b 0
```

### Test run 2 — MSL shader dump

Command:
```
rm -rf /tmp/mvk-shader-dump-s5 && mkdir /tmp/mvk-shader-dump-s5
VK_ICD_FILENAMES=/usr/local/share/vulkan/icd.d/MoltenVK_icd.json \
DYLD_LIBRARY_PATH=/usr/local/lib \
MVK_CONFIG_SHADER_DUMP_DIR=/tmp/mvk-shader-dump-s5 \
./build/macos-debug-local/modules/renderer/renderer_tests "the BRDF lookup table*" -s
```

Fragment shaders dumped: `shader-fs-255bfa41f191509a.metal`, `shader-fs-537054d3cb91558b.metal`, `shader-fs-621d54a05dfd9a6e.metal`, `shader-fs-6facbdd4c5458d49.metal`, `shader-fs-71960bedae75471f.metal`, `shader-fs-ab6f7c1f53311b47.metal`, `shader-fs-f03305b190530a52.metal`.

The forward fragment shader was identified by the highest count of `brdfLut` and `debugView` references (8 hits): file `shader-fs-6facbdd4c5458d49.metal`. It contains `frame.brdfLut` (sampled index), all three debug read paths (`sample` with `linearSampler`, `read` integer coords, `sample` with `shadowNearestSampler`), and the fixed-coord sample at `float2(0.99, 0.5)`. Committed as `docs/agent-tasks/mvk-forward-frag.msl`.
