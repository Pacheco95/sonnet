# macOS compiler fix and occlusion validation

Date: 2026-10-08. Tested local merge `a65e67e` of occlusion branch `ac533cd` and compiler fix `2e20d2b`, in a fresh worktree. The test merge was not pushed. This report branch contains documentation only; no engine source or build configuration was edited during validation.

Apple Clang 17, macOS Debug build, MoltenVK 1.4.1, Vulkan loader 1.4.341, validation enabled. Device identity is redacted.

## Results

| Check | Result |
|---|---|
| Fresh default macOS configure | PASS; system Vulkan headers 341 still present |
| Full macOS build | PASS |
| iOS configure only | PASS; no compiler-selection message |
| Explicit Homebrew compiler | PASS; cache preserved the exported compiler |
| CTest | PASS, 14/14; validation errors logged below |
| Renderer `[occlusion]` | PASS, 40 assertions in 2 cases |
| Renderer `[gpu]` | PASS, 16617 assertions in 32 cases |
| Four-case benchmark | PASS, 16 assertions |
| Optional screenshots and pixel comparison | Skipped |

The test "occlusion culling draws the same pixels as frustum culling while the camera moves" passed its byte-for-byte comparison.

The session initially exported Homebrew `CC` and `CXX`; the first fresh configure preserved them. That build directory was retained as explicit-compiler evidence, then default configuration ran in a new directory with both variables unset. No engine build with Homebrew is claimed.

Default configure output:

```text
-- Using the toolchain's compiler /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++, not the /usr/bin shim
CMAKE_CXX_COMPILER:STRING=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
CMAKE_C_COMPILER:STRING=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang
```

Explicit-compiler cache:

```text
CMAKE_CXX_COMPILER:FILEPATH=/opt/homebrew/opt/llvm/bin/clang++
CMAKE_C_COMPILER:FILEPATH=/opt/homebrew/opt/llvm/bin/clang
```

The first CTest invocation passed but skipped GPU cases because SDL could not load Vulkan. The reported GPU results are from reruns outside the sandbox with `SDL_VULKAN_LIBRARY=/usr/local/lib/libvulkan.1.dylib`. Commands were:

```sh
ctest --preset macos-debug --output-on-failure
./build/macos-debug/modules/renderer/renderer_tests '[occlusion]'
./build/macos-debug/modules/renderer/renderer_tests '[gpu]'
./build/macos-debug/modules/renderer/renderer_tests 'ten thousand draws and a hundred lights at 1080p'
```

## Verbatim validation errors

CTest logged the following error 11 times: six in runtime tests and five in editor tests. Both suites nevertheless passed. Handles are redacted. No validation errors appeared in the direct renderer runs or benchmark. No SPIRV-Cross or Metal shader compilation errors were observed. The cause of the runtime/editor messages was not investigated.

```text
[14:12:12.644] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:12.776] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:13.506] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:13.772] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:14.137] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:14.458] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:26.579] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:26.740] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:26.799] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:26.969] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
[14:12:27.208] [error] [rhi] [VulkanDevice.cpp:1327] VUID-vkBeginCommandBuffer-commandBuffer-00050 [frame 0 upload pool]: vkBeginCommandBuffer(): VkCommandBuffer <device> attempts to implicitly reset cmdBuffer created from VkCommandPool <device>[frame 0 upload pool] that does NOT have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT bit set.
The Vulkan spec states: If commandBuffer was allocated from a VkCommandPool which did not have the VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, commandBuffer must be in the initial state (https://vulkan.lunarg.com/doc/view/1.4.341.0/mac/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkBeginCommandBuffer-commandBuffer-00050)
```

## Benchmark numbers

Debug build, 1080p; all timings are milliseconds. Submission is the median of 25 frames. GPU pass timings describe the completed frame reported by the benchmark, rather than an average over the whole run.

| Occlusion | Wall | Survivors | GPU/frame | CPU recording | CPU submitting | Worst submission |
|---|---|---:|---:|---:|---:|---:|
| Off | No | 5396 / 10000 | 3.896 | 0.878 | 0.339 | 0.494 |
| On | No | 5396 / 10000 | 10.099 | 0.765 | 0.390 | 0.450 |
| Off | Yes | 7344 / 10001 | 2.649 | 0.666 | 0.299 | 0.350 |
| On | Yes | 61 / 10001 | 5.161 | 0.881 | 0.365 | 0.516 |

| Occlusion | Wall | Depth | Depth pyramid | Occlusion cull | Depth late | Forward |
|---|---|---:|---:|---:|---:|---:|
| Off | No | 0.254 | — | — | — | 2.359 |
| On | No | 0.612 | 0.310 | 0.023 | 0.007 | 5.890 |
| Off | Yes | 0.362 | — | — | — | 0.707 |
| On | Yes | 0.136 | 0.311 | 0.029 | 0.008 | 1.126 |

Survivor output:

```text
5396 of 10000 draws survived culling (638636 triangles), 12469 shadow draws,
occlusion off, wall no

5396 of 10000 draws survived culling (638636 triangles), 12469 shadow draws,
occlusion on, wall no

7344 of 10001 draws survived culling (866592 triangles), 12216 shadow draws,
occlusion off, wall yes

61 of 10001 draws survived culling (7728 triangles), 12216 shadow draws,
occlusion on, wall yes
```

The wall counts match the supplied reference of 7344 without occlusion and 61 with it. Submission rose 0.051 ms without the wall and 0.066 ms with the wall; indirect calls rose from 12 to 14. GPU time increased in both measured pairs. The cause was not investigated, and these single-run Debug measurements do not establish a general performance result.
