# Rendering

Two modules: `rhi` (render hardware interface plus the Vulkan 1.4 implementation) and `renderer` (render graph, materials, passes, engine shaders). Everything above `rhi` is API-agnostic; everything in `rhi` is deliberately shaped like Vulkan.

## Vulkan baseline

Vulkan 1.4 core is the minimum ([ADR-0001](decisions/0001-vulkan-1.4-only.md)). The device selector rejects anything lower. The engine relies on:

| Feature | Core since | Used for |
|---|---|---|
| Shader draw parameters | 1.1 | Slang lowers `SV_VertexID` through `gl_BaseVertex` |
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
- **Loader**: SDL3 loads the Vulkan library. `SDL_Vulkan_GetVkGetInstanceProcAddr` seeds both vk-bootstrap and the Vulkan-HPP dynamic dispatcher so there is one loader in the process. The loader only has to be 1.1: the 1.4 requirement is on the physical device, because distributions ship older loaders in front of current drivers (Ubuntu 24.04 has 1.3) and the engine uses no 1.4 instance-level entry points.

## Object ownership

vk-bootstrap selects the physical device and creates the instance, debug messenger, device and swapchain. Every handle it returns is immediately adopted by a Vulkan-HPP RAII wrapper (`vk::raii::Instance`, `vk::raii::Device`, ...), which then owns it. `vkb::destroy_*` is never called. VMA is used through the Hpp bindings and destroyed before the device. RAII members are declared in reverse destruction order so lifetimes are correct by construction. Details in [ADR-0006](decisions/0006-vulkan-object-ownership.md).

## Memory

All GPU memory goes through VMA. `rhi` exposes:

- Device-local buffers and images with a staging ring for uploads (M3; until then mesh data lives in host-visible buffers).
- Per-frame linear allocators for transient uniform and storage data: `IDevice::allocateTransient` hands out slices of one host-visible buffer per frame slot, aligned for both binding types, valid until the slot is reused.
- Budget queries through `VK_EXT_memory_budget` for the resource overlay: `IDevice::memoryBudget` reports usage and budget per heap.
- Leak detection: every allocation carries a debug name, and the device reports live allocations at shutdown in Debug builds.

## Shaders

Slang is the only shading language ([ADR-0005](decisions/0005-slang.md)). Engine shaders live in `modules/renderer/shaders/`, project shaders in the project's `shaders/` folder.

- **Build time**: `sonnet_add_shaders(<target> SHADERS ...)` (in `cmake/SonnetShaders.cmake`) runs `slangc` on each `.slang` file, emitting one SPIR-V 1.6 module per file that holds every `[shader("...")]` entry point under its own name (`-fvk-use-entrypoint-name`), scalar block layout (`-fvk-use-scalar-layout`, so C++ and Slang share struct layouts byte for byte), column-major matrices, debug information in Debug and `-O2` otherwise. The module lands in `shaders/<name>.spv` next to the target's binary, where `platform::Platform::basePath()` finds it, and a depfile makes edits to imported modules rebuild their users. `sonnet_add_engine_shaders(<target>)` does the same for the engine's own shaders, for every executable and test that renders through `renderer`. Release builds ship only SPIR-V.
- **Runtime**: the editor links the Slang compiler library and recompiles a shader when its file changes, keeping the previous pipeline alive when compilation fails and showing the error in the editor. This arrives with asset hot reload in M3.
- **Reflection**: Slang's program layout drives descriptor set layouts and validates that a shader conforms to the bindless conventions. There is no separate reflection library. Until the runtime compiler arrives, the layout is the fixed one below and shaders declare the matching bindings.
- **Conventions**: the shared `sonnet.slang` module declares the per-pass buffers of set 1, the per-draw push constants and the vertex layout; the bindless arrays join it with textures in M3. Shaders `import sonnet` and never declare their own descriptor sets. Vertices are pulled through the mesh's buffer device address carried in the push constants, indexed by `SV_VertexID` from the bound index buffer.

## The rhi module today

Public headers under `sonnet/rhi/`: `Types.h` (handles, formats, usages, layouts, synchronization, attachments, pipeline state), `Device.h` (`IDevice`, `DeviceDesc`, `createDevice`), `Swapchain.h` (`ISwapchain`), `CommandList.h` (`ICommandList`) and `NullDevice.h` (the GPU-less implementation for tests). The Vulkan implementation lives in `src/` and is reached only through `createDevice`, the one `#if`-switched site.

- `IDevice` creates buffers, images and swapchains, hands out generation-checked handles, and runs the frame: `beginFrame` waits for the slot's previous submission on the timeline semaphore, reads that submission's timestamps, frees the resources parked in the slot, resets the pool and the transient allocator and starts recording; `endFrame` submits and presents every swapchain image acquired since. Destroying a resource is always deferred: while recording it is parked in the current slot, between frames in the slot of the frame that was just submitted, so a handle can be released at any point of the frame that still draws with it.
- Colour formats are 8-bit UNORM and sRGB; the one depth format is `D32Sfloat`, cleared to 0 for reversed-Z. `GraphicsPipelineDesc` names the colour and depth formats, the depth test (`GreaterOrEqual` by default) and the cull mode. Storage buffers get a device address (`bufferAddress`) for vertex pulling; index buffers bind with `bindIndexBuffer` and draw with `drawIndexed`.
- `ISwapchain::acquire` returns the image for this frame or nothing when the window is minimised; it recreates the swapchain when acquire or present reported out of date, or after `requestResize`. Swapchain images are registered as ordinary image handles. The swapchain prefers a UNORM format: what reaches it is already display-encoded, by the scene's output pass and by Dear ImGui's vertex colours, so an sRGB view would encode twice.
- `ICommandList` records whole-image barriers with explicit stages, accesses and layouts on both sides (`ImageBarrier`), dynamic rendering with colour and depth attachments, pipeline binds, the per-pass buffers as push descriptors (`bindBuffers`), push constants, draws, image-to-buffer copies for readback, and timestamps. The render graph derives the barriers from how passes use their images; only tests spell them out.
- Every pipeline uses one shared layout: an empty set 0 reserved for the bindless arrays, set 1 with a uniform buffer at binding 0 and a storage buffer at binding 1 as push descriptors, and `PushConstantSize` bytes of push constants. Viewport and scissor are dynamic, front faces are counter-clockwise, and there is no vertex input state.
- `writeTimestamp` records the GPU clock into one of `MaxTimestamps` slots per frame; `timestamps` returns, in nanoseconds, what the frame that last used the slot wrote, so results are `FramesInFlight` frames old and never stall.
- Validation messages are logged with their id and the debug names of the objects involved, and counted; the tests of every module fail when the count is non-zero. Loader messages go to `trace`.
- Host-visible buffers are persistently mapped; `mappedRange` exposes them.
- `NullDevice` implements the same interface without a GPU: handles, descriptions, mapped memory, transient allocations and timestamp bookkeeping behave as on the Vulkan device, and recording produces a readable trace (`barrier "scene" ColorAttachment->ShaderReadOnly`, `drawIndexed 36 x1`) that the tests of the modules above assert on.

Not there yet, in milestone order: samplers and the bindless set 0 (M3, with textures), uploads through a staging ring (M3), pass merging and transient aliasing in the graph.

## Frame structure

Two frames in flight. Each frame owns a command pool, a timeline semaphore value, a timestamp query pool and a transient allocator; the descriptor allocator joins with the bindless set. The swapchain uses mailbox where available and FIFO otherwise.

Descriptor layout:

- Set 0, bound once per frame: the bindless arrays (sampled images, storage images, storage buffers, samplers) and the per-frame constants. Empty until M3.
- Set 1, push descriptors: per-pass buffers. Today the frame constants at binding 0 and the object array at binding 1.
- Push constants: per-draw indices into the bindless arrays and the draw's transform index. Today the mesh's vertex buffer address and the object index.

Per-object data lives in storage buffers addressed by index, so the draw loop is `bind pipeline, push constants, draw` and is ready for indirect submission.

## Render graph

The render graph is an engine feature this time; the previous iteration kept it in the demo and regretted it. Passes declare the resources they read and write; the graph allocates transient images, orders passes, emits synchronization2 barriers and image layout transitions, and merges compatible passes. Aliasing of transient memory is a later optimisation with no API change.

`renderer::RenderGraph` is rebuilt every frame between `reset` and `execute`:

- `importImage` registers an image the caller owns (the swapchain image, the viewport target), whose contents are undefined at the start of the frame, with an optional final layout the graph transitions to after the last pass (`Present` for the swapchain). `createImage` declares a transient image; its usage bits come from the passes that use it.
- `addPass` takes a setup callback that declares the pass's colour and depth attachments, sampled images and transfer sources or destinations, and an execute callback that records into the `ICommandList`. A pass with attachments is a graphics pass: the graph opens dynamic rendering on them around execute, with the declared load, store and clear values.
- `execute` runs the passes in declaration order. Before each pass it compares every declared use with the image's current state and emits one barrier per image whose layout changes or whose previous or next access writes; reads after reads in the same layout merge their stages instead. Transient images come from a pool keyed on their description, reused across frames and released after a few frames without use. Each pass is bracketed by two timestamps and timed on the CPU; `statistics` reports both per pass, plus barrier and transient-image counts, for the editor's overlay.

Topological ordering and pass merging are not there yet: passes run in the order they are added, which the renderer controls.

The main pipeline is clustered forward ([ADR-0008](decisions/0008-clustered-forward-rendering.md), proposed): depth pre-pass, light clustering in compute, one forward shading pass, then post-processing. Planned passes in milestone order: depth pre-pass, forward PBR, skybox, cascaded shadow maps, light clustering, image-based lighting, bloom, tone mapping, FXAA or TAA, editor outline and picking.

## The renderer module today

Public headers under `sonnet/renderer/`: `RenderGraph.h`, `Mesh.h` (`Vertex`, `MeshData`, `MeshHandle`), `Primitives.h` (box, sphere, plane, cylinder and capsule generators for scenes without assets), `Camera.h` (`Camera` and the reversed-Z infinite projection), `SceneView.h` (`DrawItem`, `DirectionalLight`, `SceneView`), `Renderer.h` and `RenderTarget.h`. Engine shaders are `shaders/sonnet.slang` (the shared module) and `shaders/forward.slang`.

- `Renderer` owns meshes and the engine pipelines. `createMesh` uploads `MeshData` into a storage buffer for vertex pulling and an index buffer. `addScenePasses` declares the forward pass drawing a `SceneView` into colour and depth images of the graph: it fills the frame constants and one `ObjectData` per draw from the transient allocator, pushes both as the per-pass buffers, then records `push constants, bind index buffer, draw indexed` per item. `statistics` counts draws and triangles.
- The forward shader is one directional light with Lambert diffuse, a Blinn-Phong highlight and constant ambient, writing a Reinhard-mapped, gamma-encoded colour into the UNORM target. PBR and the tone-mapping pass replace it in M3.
- `RenderTarget` is the colour and depth pair a viewport draws into, in the renderer's fixed formats (`R8G8B8A8Unorm`, `D32Sfloat`), kept across frames so Dear ImGui can display the colour image and recreated on resize with the device's deferred destruction.
- The draw list is handed down by the layer above (the editor's primitive scene now, `world` from M2); the renderer never asks where it came from.

## Resources and materials

- Meshes: vertex and index data in device-local buffers, referenced by buffer device address. A `Mesh` asset is a set of submeshes with bounds.
- Materials: a `MaterialTemplate` (shader, pipeline state, parameter layout) plus `MaterialInstance` (parameter values, texture handles) stored in a storage buffer and referenced by index from the draw.
- Textures: KTX2 files uploaded as-is when the GPU supports the compressed format, transcoded by the Basis Universal transcoder otherwise.
- Every resource is reached through a `Handle<Tag>` with generation checks ([architecture.md](architecture.md#resource-handles)).

## Editor viewport and ImGui

The scene renders into an offscreen colour image that the viewport panel displays with `ImGui::Image`. Picking is a small pass that writes entity ids into an image and reads back the pixel under the cursor. The selection outline is a post pass over a mask image.

ImGui uses its SDL3 and Vulkan backends, initialised in dynamic-rendering mode, drawing directly to the swapchain image after the scene. Multi-viewport is enabled on desktop. The `ui` module wraps both in `ImGuiLayer` ([editor.md](editor.md#the-ui-module)); the editor declares the ImGui draw as the last graph pass, rendering into the swapchain image and sampling the viewport target, so the graph orders it after the scene.

## Debugging

- Debug builds request `VK_LAYER_KHRONOS_validation` with synchronization validation enabled, and route messages to the log with the object names attached.
- Every Vulkan object gets a `VK_EXT_debug_utils` name from its handle's debug name, so RenderDoc and validation output are readable.
- GPU timestamps per render-graph pass feed the on-screen overlay and Tracy's GPU zones.

## Testing

- `rhi` interfaces have a null implementation used by unit tests of `renderer`, `assets` and `world`, so those modules are tested without a GPU. `renderer_tests` checks the graph's barriers, pooling and timings and the renderer's draw recording against the null device's trace.
- The Vulkan implementation is tested on Lavapipe in CI: device creation, resource lifetime including deferred destruction, a triangle drawn into an offscreen image and read back, vertex pulling with the per-pass buffers and the reversed-Z depth test, timestamps and the transient allocator, and later golden-image comparisons of sample scenes with a tolerance. `renderer_tests`, `ui_tests` and `editor_tests` each also run one GPU case on Lavapipe: a lit box read back from the viewport target, ImGui frames drawn into a headless swapchain, and whole editor frames. Swapchain tests use SDL's offscreen video driver and `VK_EXT_headless_surface`, which Lavapipe supports; on drivers without headless surfaces those tests skip. Loaders before 1.4 emulate that extension for every driver and some drivers crash inside the surface queries, so on such loaders the tests run only on Lavapipe; `DeviceInfo` reports loader version and driver name for this. The test shader is compiled by the same `sonnet_add_shaders` rule the applications use.
- Validation-layer messages fail tests when they occur: `IDevice::validationMessageCount` is checked by the test fixture.

## See also

- [Architecture](architecture.md)
- [Assets](assets.md)
- [Build system](build.md)
