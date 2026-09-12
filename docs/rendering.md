# Rendering

Two modules: `rhi` (render hardware interface plus the Vulkan 1.4 implementation) and `renderer` (render graph, materials, passes, engine shaders). Everything above `rhi` is API-agnostic; everything in `rhi` is deliberately shaped like Vulkan.

## Vulkan baseline

Vulkan 1.4 core is the minimum ([ADR-0001](decisions/0001-vulkan-1.4-only.md)). The device selector rejects anything lower. The engine relies on:

| Feature | Core since | Used for |
|---|---|---|
| Timeline semaphores | 1.2 | Frame and upload synchronization |
| Buffer device address | 1.2 | Vertex pulling, per-draw data without descriptor churn |
| Descriptor indexing: partially bound, update-after-bind, runtime arrays | 1.2 (features required by this engine) | Bindless textures and buffers |
| Scalar block layout | 1.2 | Shared struct layouts between C++ and Slang |
| Dynamic rendering | 1.3 | No render pass or framebuffer objects |
| Synchronization2 | 1.3 | Barriers emitted by the render graph |
| Extended dynamic state | 1.3 | Fewer pipeline permutations |
| Push descriptors | 1.4 | Per-pass uniform and storage buffers |
| Dynamic rendering local read | 1.4 | Tile-friendly subpass-style reads on mobile |
| Maintenance 5 and 6 | 1.4 | Simpler pipeline and descriptor handling |
| Host image copy | 1.4, optional feature | Faster texture uploads when present; falls back to staging |

Vulkan 1.4 also raises minimum limits (push constants, bound descriptor sets, image dimensions), which the design assumes.

Depth uses reversed-Z: `D32_SFLOAT`, cleared to 0, `GREATER` compare, infinite far plane. Projection uses `GLM_FORCE_DEPTH_ZERO_TO_ONE`. Clip-space Y is flipped by a negative viewport height (core since 1.1) so triangle winding stays counter-clockwise front-facing, matching glTF.

## Platform notes

- **Windows, Linux**: current vendor drivers and Mesa ship Vulkan 1.4. Lavapipe (Mesa CPU implementation) is used in CI.
- **macOS, iOS**: MoltenVK 1.4+ layers Vulkan 1.4 over Metal (macOS 12+, iOS 15+). It is a portability implementation: the instance must enable `VK_KHR_portability_enumeration` and the device must enable `VK_KHR_portability_subset` when present. Geometry shaders, triangle fans and some format and tessellation features are missing; the engine uses none of them. Descriptor indexing on MoltenVK requires Metal argument buffers, which MoltenVK enables by default in recent versions; the device selector verifies the required descriptor-indexing features rather than assuming them.
- **Android**: devices launching with Android 16 or later must support Vulkan 1.4. Older devices are unsupported, which is accepted in ADR-0001. Mobile GPUs are tile-based: the render graph keeps passes mergeable and uses dynamic rendering local read for G-buffer-style reads instead of round trips to memory. Textures are shipped as KTX2 with ASTC or ETC2.
- **Loader**: SDL3 loads the Vulkan library. `SDL_Vulkan_GetVkGetInstanceProcAddr` seeds both vk-bootstrap and the Vulkan-HPP dynamic dispatcher so there is one loader in the process.

## Object ownership

vk-bootstrap selects the physical device and creates the instance, debug messenger, device and swapchain. Every handle it returns is immediately adopted by a Vulkan-HPP RAII wrapper (`vk::raii::Instance`, `vk::raii::Device`, ...), which then owns it. `vkb::destroy_*` is never called. VMA is used through the Hpp bindings and destroyed before the device. RAII members are declared in reverse destruction order so lifetimes are correct by construction. Details in [ADR-0006](decisions/0006-vulkan-object-ownership.md).

## Memory

All GPU memory goes through VMA. `rhi` exposes:

- Device-local buffers and images with a staging ring for uploads.
- Per-frame linear allocators for transient uniform and storage data.
- Budget queries through `VK_EXT_memory_budget` for the resource overlay.
- Leak detection: every allocation carries a debug name, and the device reports live allocations at shutdown in Debug builds.

## Shaders

Slang is the only shading language ([ADR-0005](decisions/0005-slang.md)). Engine shaders live in `modules/renderer/shaders/`, project shaders in the project's `shaders/` folder.

- **Build time**: a CMake custom command runs `slangc` on each `.slang` file, emitting SPIR-V 1.6 for every `[shader("...")]` entry point with `-fvk-use-entrypoint-name`. Release builds ship only SPIR-V.
- **Runtime**: the editor links the Slang compiler library and recompiles a shader when its file changes, keeping the previous pipeline alive when compilation fails and showing the error in the editor.
- **Reflection**: Slang's program layout drives descriptor set layouts and validates that a shader conforms to the bindless conventions. There is no separate reflection library.
- **Conventions**: a shared `sonnet.slang` module declares the bindless resource arrays, the per-frame and per-pass parameter blocks, and the vertex-pulling helpers. Shaders `import` it and never declare their own descriptor sets.

## Frame structure

Two frames in flight. Each frame owns a command pool, a timeline semaphore value, a descriptor allocator that is reset per frame, and a transient allocator. The swapchain uses mailbox where available and FIFO otherwise.

Descriptor layout:

- Set 0, bound once per frame: the bindless arrays (sampled images, storage images, storage buffers, samplers) and the per-frame constants.
- Set 1, push descriptors: per-pass buffers.
- Push constants: per-draw indices into the bindless arrays and the draw's transform index.

Per-object data lives in storage buffers addressed by index, so the draw loop is `bind pipeline, push constants, draw` and is ready for indirect submission.

## Render graph

The render graph is an engine feature this time; the previous iteration kept it in the demo and regretted it. Passes declare the resources they read and write; the graph allocates transient images, orders passes, emits synchronization2 barriers and image layout transitions, and merges compatible passes. Aliasing of transient memory is a later optimisation with no API change.

The main pipeline is clustered forward ([ADR-0008](decisions/0008-clustered-forward-rendering.md), proposed): depth pre-pass, light clustering in compute, one forward shading pass, then post-processing. Planned passes in milestone order: depth pre-pass, forward PBR, skybox, cascaded shadow maps, light clustering, image-based lighting, bloom, tone mapping, FXAA or TAA, editor outline and picking.

## Resources and materials

- Meshes: vertex and index data in device-local buffers, referenced by buffer device address. A `Mesh` asset is a set of submeshes with bounds.
- Materials: a `MaterialTemplate` (shader, pipeline state, parameter layout) plus `MaterialInstance` (parameter values, texture handles) stored in a storage buffer and referenced by index from the draw.
- Textures: KTX2 files uploaded as-is when the GPU supports the compressed format, transcoded by the Basis Universal transcoder otherwise.
- Every resource is reached through a `Handle<Tag>` with generation checks ([architecture.md](architecture.md#resource-handles)).

## Editor viewport and ImGui

The scene renders into an offscreen colour image that the viewport panel displays with `ImGui::Image`. Picking is a small pass that writes entity ids into an image and reads back the pixel under the cursor. The selection outline is a post pass over a mask image.

ImGui uses its SDL3 and Vulkan backends, initialised in dynamic-rendering mode, drawing directly to the swapchain image after the scene. Multi-viewport is enabled on desktop.

## Debugging

- Debug builds request `VK_LAYER_KHRONOS_validation` with synchronization validation enabled, and route messages to the log with the object names attached.
- Every Vulkan object gets a `VK_EXT_debug_utils` name from its handle's debug name, so RenderDoc and validation output are readable.
- GPU timestamps per render-graph pass feed the on-screen overlay and Tracy's GPU zones.

## Testing

- `rhi` interfaces have a null implementation used by unit tests of `renderer`, `assets` and `world`, so those modules are tested without a GPU.
- The Vulkan implementation is tested on Lavapipe in CI: device creation, resource lifetime, a triangle, and later golden-image comparisons of sample scenes with a tolerance.
- Validation-layer messages fail tests when they occur.

## See also

- [Architecture](architecture.md)
- [Assets](assets.md)
- [Build system](build.md)
