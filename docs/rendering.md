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

- Device-local buffers and images with a staging ring for uploads: `IDevice::uploadBuffer` and `uploadImage` copy the data into the frame slot's ring at once and record the transfer into an upload command buffer that is submitted ahead of the frame's commands, so a resource uploaded at any point of a frame, or between frames, is complete for that frame's draws. Data larger than the ring gets a dedicated staging buffer released with the slot.
- Per-frame linear allocators for transient uniform and storage data: `IDevice::allocateTransient` hands out slices of one host-visible buffer per frame slot, aligned for both binding types, valid until the slot is reused.
- Budget queries through `VK_EXT_memory_budget` for the resource overlay: `IDevice::memoryBudget` reports usage and budget per heap.
- Leak detection: every allocation carries a debug name, and the device reports live allocations at shutdown in Debug builds.

## Shaders

Slang is the only shading language ([ADR-0005](decisions/0005-slang.md)). Engine shaders live in `modules/renderer/shaders/`, project shaders in the project's `shaders/` folder.

- **Build time**: `sonnet_add_shaders(<target> SHADERS ...)` (in `cmake/SonnetShaders.cmake`) runs `slangc` on each `.slang` file, emitting one SPIR-V 1.6 module per file that holds every `[shader("...")]` entry point under its own name (`-fvk-use-entrypoint-name`), scalar block layout (`-fvk-use-scalar-layout`, so C++ and Slang share struct layouts byte for byte), column-major matrices, debug information in Debug and `-O2` otherwise. The module lands in `shaders/<name>.spv` next to the target's binary, where `platform::Platform::basePath()` finds it, and a depfile makes edits to imported modules rebuild their users. `sonnet_add_engine_shaders(<target>)` does the same for the engine's own shaders, for every executable and test that renders through `renderer`. Release builds ship only SPIR-V.
- **Runtime**: `assets::ShaderCompiler` drives the Slang library with the same options as the build rule, and `Renderer::reloadShader` rebuilds every pipeline made from a module with the new SPIR-V, building all of them before destroying any so a module or pipeline the driver rejects changes nothing. The editor watches the checkout's shader sources and recompiles a changed entry-point file, or every one when a shared module such as `sonnet.slang` changes; compile errors are logged with their `file:line:column` and the previous pipelines stay ([editor.md](editor.md#shader-hot-reload)). The player ships only the SPIR-V the build produced.
- **Reflection**: the descriptor layout is the fixed one below and shaders declare the matching bindings; Slang's program layout is the reflection source when a shader has to be checked against it, and there is no separate reflection library.
- **Conventions**: the shared `sonnet.slang` module declares the bindless arrays of set 0, the per-pass buffers of set 1, the per-draw push constants and the vertex layout. Scene shaders `import sonnet` and never declare their own descriptor sets. Vertices are pulled through the mesh's buffer device address carried in the push constants, indexed by `SV_VertexID` from the bound index buffer. Post passes over an image (`outline.slang`) are the exception: they draw one full-screen triangle, declare the pass image binding and their own push constants, and import nothing.

## The rhi module today

Public headers under `sonnet/rhi/`: `Types.h` (handles, formats, usages, layouts, synchronization, attachments, pipeline state), `Device.h` (`IDevice`, `DeviceDesc`, `createDevice`), `Swapchain.h` (`ISwapchain`), `CommandList.h` (`ICommandList`) and `NullDevice.h` (the GPU-less implementation for tests). The Vulkan implementation lives in `src/` and is reached only through `createDevice`, the one `#if`-switched site.

- `IDevice` creates buffers, images, samplers and swapchains, hands out generation-checked handles, and runs the frame: `beginFrame` waits for the slot's previous submission on the timeline semaphore, reads that submission's timestamps, frees the resources parked in the slot, resets the pools, the transient allocator and the staging ring and starts recording; `endFrame` submits the slot's upload command buffer ahead of the frame's and presents every swapchain image acquired since. An upload between frames prepares the coming slot the same way first. Destroying a resource is always deferred: while recording it is parked in the current slot, between frames in the slot of the frame that was just submitted, so a handle can be released at any point of the frame that still draws with it.
- Colour formats are 8-bit UNORM and sRGB, `R16G16Sfloat` and `R16G16B16A16Sfloat` for HDR data, the block-compressed `BC4`, `BC5` and `BC7` for cooked textures, and `R32Uint` for entity ids, whose clear colour is taken as integers; the one depth format is `D32Sfloat`, cleared to 0 for reversed-Z. `formatInfo` gives block dimensions and sizes, `levelByteSize` the bytes of one tightly packed level. An `ImageDesc` may carry a mip chain and may be a cube map of six layers; barriers always cover the whole image, uploads address one level and layer. `GraphicsPipelineDesc` names the colour and depth formats, the depth test (`GreaterOrEqual` by default), the cull mode and the blend mode (none, alpha, additive); an empty fragment entry makes a depth-only pipeline. `ComputePipelineDesc` names one compute entry; `bindPipeline` takes either kind and `dispatch` runs the compute one. Storage buffers get a device address (`bufferAddress`) for vertex pulling; index buffers bind with `bindIndexBuffer` and draw with `drawIndexed`. `memoryBarrier` orders a buffer written by one pass before the pass that reads it.
- Every image created with `Sampled` usage takes a slot in the bindless sampled-image array, or in the cube array for a cube map (`sampledImageIndex`); with `Storage` usage each mip level gets a slot in the storage-image array on first request (`storageImageIndex`), through a 2D-array view so one shader declaration covers plain images and cube maps. Samplers (`SamplerDesc`: filters, address mode, anisotropy) take a slot in the sampler array, or in the comparison-sampler array when they compare, with `GreaterOrEqual` for reversed-Z shadows. Slots are written into the set at creation and reused only after the deferred destruction of their resource, so a resource created mid-frame can be indexed by that frame's shaders.
- `ISwapchain::acquire` returns the image for this frame or nothing when the window is minimised; it recreates the swapchain when acquire or present reported out of date, or after `requestResize`. Swapchain images are registered as ordinary image handles. The swapchain prefers a UNORM format: what reaches it is already display-encoded, by the scene's output pass and by Dear ImGui's vertex colours, so an sRGB view would encode twice.
- `ICommandList` records whole-image barriers with explicit stages, accesses and layouts on both sides (`ImageBarrier`), dynamic rendering with colour and depth attachments, pipeline binds, the per-pass buffers and image as push descriptors (`bindBuffers`, `bindImages`), push constants, draws, image-to-buffer copies for readback, and timestamps. The render graph derives the barriers from how passes use their images; only tests spell them out.
- Every pipeline, graphics or compute, uses one shared layout: set 0 is the bindless set, bound again with every pipeline because Dear ImGui's own layout replaces it in between; set 1 holds a uniform buffer at binding 0, a storage buffer at binding 1 and a sampled image at binding 2 as push descriptors; `PushConstantSize` bytes of push constants are visible to every stage. The pass image is read with `Load`, without a sampler; it is what the outline pass reads the selection mask through. Viewport and scissor are dynamic, front faces are counter-clockwise, and there is no vertex input state.
- `writeTimestamp` records the GPU clock into one of `MaxTimestamps` slots per frame; `timestamps` returns, in nanoseconds, what the frame that last used the slot wrote, so results are `FramesInFlight` frames old and never stall.
- Validation messages are logged with their id and the debug names of the objects involved, and counted; the tests of every module fail when the count is non-zero. Loader messages go to `trace`.
- `DeviceInfo::blockCompressionSupported` says whether the BC formats can be used, which desktop GPUs and Lavapipe support and mobile GPUs do not; the feature is enabled where present rather than required, and the texture loader falls back to uncompressed data without it.
- Host-visible buffers are persistently mapped; `mappedRange` exposes them.
- `NullDevice` implements the same interface without a GPU: handles, descriptions, mapped memory, bindless slots, transient allocations and timestamp bookkeeping behave as on the Vulkan device, and recording produces a readable trace (`barrier "scene" ColorAttachment->ShaderReadOnly`, `drawIndexed 36 x1`, `uploadImage "albedo" level 0 layer 0 4194304 bytes`, `dispatch 4 2 1`) that the tests of the modules above assert on.

Not there yet: pass merging and transient aliasing in the graph.

## Frame structure

Two frames in flight. Each frame owns a command pool and an upload command pool, a timeline semaphore value, a timestamp query pool, a transient allocator and a staging ring. The swapchain uses mailbox where available and FIFO otherwise.

Descriptor layout:

- Set 0, the bindless set, one for the device: binding 0 the sampled 2D images, binding 1 the samplers, binding 2 the storage images (one 2D-array view per mip level), binding 3 the cube maps, binding 4 the comparison samplers, each a runtime array that is partially bound and updated after bind. The device hands out the indices; shaders receive them in the frame constants, the material and object data or the push constants.
- Set 1, push descriptors: per-pass buffers and one image. The frame constants at binding 0, the object array at binding 1 and, for the outline pass, the selection mask at binding 2.
- Push constants: per-draw indices into the bindless arrays and the draw's transform index. Today the mesh's vertex buffer address and the object index.

Per-object data lives in storage buffers addressed by index, so the draw loop is `bind pipeline, push constants, draw` and is ready for indirect submission.

## Render graph

The render graph is an engine feature this time; the previous iteration kept it in the demo and regretted it. Passes declare the resources they read and write; the graph allocates transient images, orders passes, emits synchronization2 barriers and image layout transitions, and merges compatible passes. Aliasing of transient memory is a later optimisation with no API change.

`renderer::RenderGraph` is rebuilt every frame between `reset` and `execute`:

- `importImage` registers an image the caller owns (the swapchain image, the viewport target), whose contents are undefined at the start of the frame, with an optional final layout the graph transitions to after the last pass (`Present` for the swapchain). `createImage` declares a transient image; its usage bits come from the passes that use it.
- `addPass` takes a setup callback that declares the pass's colour and depth attachments, sampled images and transfer sources or destinations, and an execute callback that records into the `ICommandList`. A pass with attachments is a graphics pass: the graph opens dynamic rendering on them around execute, with the declared load, store and clear values.
- `execute` runs the passes in declaration order. Before each pass it compares every declared use with the image's current state and emits one barrier per image whose layout changes or whose previous or next access writes; reads after reads in the same layout merge their stages instead. An image's first barrier of the frame waits for all earlier commands and memory writes, since the previous frame wrote the same target and may still be running. Transient images come from a pool keyed on their description, reused across frames and released after a few frames without use. Each pass is bracketed by two timestamps and timed on the CPU; `statistics` reports both per pass, plus barrier and transient-image counts, for the editor's overlay.

Topological ordering and pass merging are not there yet: passes run in the order they are added, which the renderer controls.

The main pipeline is clustered forward ([ADR-0008](decisions/0008-clustered-forward-rendering.md)). `Renderer::addScenePasses` declares it, in this order:

1. Precomputation that is pending: the BRDF lookup table on the first frame, an environment's cubes on the frame after its creation ([Image-based lighting](#image-based-lighting)).
2. Four shadow cascades for the sun, each a depth-only pass into its own transient `D32Sfloat` image of `shadowMapSize` pixels. The cascades split the view depth up to `shadowDistance` between logarithmic and uniform spacing; each fits a bounding sphere around its slice of the frustum, so its size stays put as the camera turns, and snaps that sphere's centre to shadow texels so edges do not shimmer as it moves. The projection is orthographic and reversed-Z, extended towards the light by the shadow distance so casters outside the view still cast.
3. The depth pre-pass over the opaque and masked draws into the target's depth image.
4. Light clustering: a compute pass over a 16 by 9 by 24 grid of clusters, tiles of the screen sliced exponentially in view depth, that tests every punctual light's sphere against each cluster's view-space box and writes up to 63 light indices per cluster into a device-local buffer, with the memory barriers around it.
5. The forward pass into a transient `R16G16B16A16Sfloat` image: opaque draws with an equal depth test, so each visible surface is shaded once; then the skybox, a full-screen triangle at the far plane that samples the environment cube by view direction; then the blended draws sorted back to front without depth writes. Shading is the metallic-roughness model: Cook-Torrance with GGX, Smith and Schlick for the sun and the cluster's lights, inverse-square falloff windowed at the light's range, spot cones by their cosines, the sun shadowed by the cascade its view depth selects with a 3x3 kernel of hardware comparisons, image-based lighting through the split-sum approximation, or a flat ambient term without an environment. Opaque draws are grouped by pipeline and mesh so bindings change rarely.
6. Bloom: a chain of half-resolution passes, a 13-tap downsample then a tent-filtered upsample that adds each level to the one above, all in HDR.
7. Tone mapping: exposure, the bloom blended in by `bloomStrength`, the ACES curve and the display encoding into the `R8G8B8A8Unorm` target, or into a transient copy of it when anti-aliasing is on.
8. FXAA into the target.

The editor adds the id, selection mask and outline passes after these. `RendererSettings` holds the sizes and sample counts, which the tests turn down and which the editor keeps at their defaults.

## The renderer module today

Public headers under `sonnet/renderer/`: `RenderGraph.h`, `Mesh.h` (`Vertex`, `Submesh`, `MeshData`, `MeshHandle`, `generateTangents`), `Primitives.h` (box, sphere, plane, cylinder and capsule generators for scenes without assets), `Texture.h` (`TextureData`, `TextureHandle`, `generateMipChain`), `Material.h` (`MaterialDesc`, `MaterialHandle`), `Camera.h` (`Camera` and the reversed-Z infinite projection), `SceneView.h` (`DrawItem`, `DirectionalLight`, `Light`, `EnvironmentHandle`, `SceneView`), `Renderer.h`, `RenderTarget.h` and `Picker.h`. Engine shaders are `shaders/sonnet.slang` (the shared module), the scene passes `depth.slang`, `cluster.slang`, `forward.slang` and `skybox.slang`, the precomputation `ibl.slang`, the post passes `post.slang`, and the editor's `id.slang` and `outline.slang`.

- `Renderer` owns meshes, textures, materials, environments and the engine pipelines. `createMesh` uploads `MeshData` into device-local buffers: a storage buffer for vertex pulling and an index buffer, with the submeshes and bounds kept on the CPU. `createTexture` uploads every level and layer of a `TextureData` and `textureIndex` gives the bindless index shaders read it through. `createMaterial` keeps a `MaterialDesc`; `createEnvironment` takes an equirectangular map. `addScenePasses` declares the passes above for a `SceneView`; the first pass to record fills the frame constants, one `ObjectData` per draw, every material and every light from the transient allocator, and each pass pushes the constants and objects as the per-pass buffers and reaches the rest through buffer device addresses in the constants. Per draw it records `push constants, bind index buffer, draw indexed` over the submesh's index range. `statistics` counts scene draws and triangles, shadow draws and lights.
- Every `DrawItem` carries a 32-bit id, 0 for none. `addIdPass` draws the ids into an `R32Uint` image (`IdFormat`), depth-tested without writing against the depth the forward pass left, so only the visible surface of each entity remains; it is what picking reads. `addSelectionMaskPass` draws only the items whose id is listed into a second `R32Uint` image without a depth test, so the mask holds each selected entity's whole silhouette, occluded or not. `addOutlinePass` is the post pass over the colour image: it reads the mask through the pass image binding and colours the pixels within two of a masked silhouette, discarding the rest so the scene shows through. Because the mask ignores depth, an unselected object in front of a selected one never gets an outline of its own, and a selected object stays outlined through what hides it. None of the three passes is counted in the statistics.
- `Picker` answers "which id is under this pixel" without a stall: a request makes `addPass` copy this frame's id image into the frame slot's readback buffer, and `poll`, called once per frame after the device's `beginFrame`, returns the answer once the slot comes round `FramesInFlight` frames later. The editor turns the id back into an entity.
- `RenderTarget` is the colour and depth pair a viewport draws into, in the renderer's fixed formats (`R8G8B8A8Unorm`, `D32Sfloat`), kept across frames so Dear ImGui can display the colour image and recreated on resize with the device's deferred destruction.
- The draw list is handed down by the layer above (`world`); the renderer never asks where it came from.

## Resources and materials

- Meshes: `Vertex` is position, normal, a tangent with the bitangent sign in `w`, and one uv set, 48 bytes under scalar layout; `generateTangents` derives tangents from the uv derivatives for meshes without them, the primitives included. A mesh is a list of submeshes, each an index range with a material slot, plus bounds; a `DrawItem` names one submesh and one material.
- Materials: the glTF metallic-roughness model in `MaterialDesc`: base colour, emissive, metallic, roughness, normal scale, occlusion strength, the five textures, an alpha mode (opaque, masked with a cutoff, blended), the texture wrap and double-sidedness. The renderer keeps the descriptions and writes them all into a transient storage buffer every frame, with the default material in slot 0, so a change shows on the next frame and a draw with an invalid handle uses the default. An invalid texture is the white default, or the flat normal for the normal map. Double-sided materials use pipelines that cull nothing and flip back-facing normals.
- Textures: `TextureData` holds every level tightly packed, level-major with a cube map's six faces inside each level, as KTX2 lays them out; `generateMipChain` box-filters an RGBA8 image, averaging sRGB data in linear space. Every material texture is sampled through one of three anisotropic samplers by wrap mode.
- Lights: the sun is the `DirectionalLight` of the view; `Light` is a point or spot light with a range, clustered by the renderer. Up to 1024 per view.
- Every resource is reached through a `Handle<Tag>` with generation checks ([architecture.md](architecture.md#resource-handles)).

## Image-based lighting

An environment is created from an equirectangular map, typically an RGBA16F HDR image, and precomputed by compute passes the next `addScenePasses` declares, in the render graph like everything else: the map is converted into a cube (`environmentSize` per face, RGBA16F), the cube's mip chain is built one level per pass, the diffuse irradiance cube is integrated with cosine-weighted samples of a blurred level, and the specular cube is prefiltered with GGX importance sampling at one roughness per level, reading the source at a lod chosen from the sample's solid angle so bright texels do not become fireflies. The split-sum BRDF lookup table is computed once per renderer on the first frame. All of these are storage images and stay in the General layout; the graph orders the forward pass's reads after the writes, so an environment lights the frame that computes it. The skybox pass samples the environment cube by view direction; `SceneView::environmentIntensity` scales both the lighting and the background, and `exposure` the tone mapping.

## Editor viewport and ImGui

The scene renders into an offscreen colour image that the viewport panel displays with `ImGui::Image`. Picking is a small pass that writes entity ids into an image and reads back the pixel under the cursor (`Picker`). The selection outline is a post pass over a mask image holding the selected entities' silhouettes.

ImGui uses its SDL3 and Vulkan backends, initialised in dynamic-rendering mode, drawing directly to the swapchain image after the scene. Multi-viewport is enabled on desktop. The `ui` module wraps both in `ImGuiLayer` ([editor.md](editor.md#the-ui-module)); the editor declares the ImGui draw as the last graph pass, rendering into the swapchain image and sampling the viewport target, so the graph orders it after the scene.

## Debugging

- Debug builds request `VK_LAYER_KHRONOS_validation` with synchronization validation enabled, and route messages to the log with the object names attached.
- Every Vulkan object gets a `VK_EXT_debug_utils` name from its handle's debug name, so RenderDoc and validation output are readable.
- GPU timestamps per render-graph pass feed the on-screen overlay and Tracy's GPU zones.

## Testing

- `rhi` interfaces have a null implementation used by unit tests of `renderer`, `assets` and `world`, so those modules are tested without a GPU. `renderer_tests` checks the graph's barriers, pooling and timings and, against the null device's trace, the renderer's pass list and draw recording, the sorting of blended draws, texture uploads and material indices, an environment's precomputation, the id, mask and outline passes and the picker's readback timing.
- The Vulkan implementation is tested on Lavapipe in CI: device creation, resource lifetime including deferred destruction, a triangle drawn into an offscreen image and read back, vertex pulling with the per-pass buffers and the reversed-Z depth test, an `R32Uint` id image written by one pass and read by the next through the pass image binding, a mipmapped texture and a cube map uploaded through the staging ring and sampled through the bindless set, an upload larger than the ring, a compute pass filling a storage image, timestamps and the transient allocator, and later golden-image comparisons of sample scenes with a tolerance. `renderer_tests`, `ui_tests` and `editor_tests` also run GPU cases on Lavapipe: a lit box read back from the viewport target, the sun's shadow beside a box, a clustered point light that reaches only the ground within its range, an environment that fills the background and lights a sphere, a box picked by id and outlined over several frames, a selected surface whose occluder stays unoutlined, ImGui frames drawn into a headless swapchain, and whole editor frames. Swapchain tests use SDL's offscreen video driver and `VK_EXT_headless_surface`, which Lavapipe supports; on drivers without headless surfaces those tests skip. Loaders before 1.4 emulate that extension for every driver and some drivers crash inside the surface queries, so on such loaders the tests run only on Lavapipe; `DeviceInfo` reports loader version and driver name for this. The test shader is compiled by the same `sonnet_add_shaders` rule the applications use.
- Validation-layer messages fail tests when they occur: `IDevice::validationMessageCount` is checked by the test fixture.

## See also

- [Architecture](architecture.md)
- [Assets](assets.md)
- [Build system](build.md)
