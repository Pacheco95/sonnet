# Editor

Two modules, both editor-only and never linked by the player ([architecture.md](architecture.md#editor-and-player)): `ui` wraps Dear ImGui, `editor` is the editor shell. `apps/editor` is the executable that owns the window, device and swapchain and calls the editor's steps in frame order.

## The ui module

`sonnet/ui/ImGuiLayer.h` is the whole public surface. `ImGuiLayer` creates the Dear ImGui context with docking, keyboard navigation and DPI-scaled fonts, initialises the SDL3 backend on the platform window and the Vulkan backend in dynamic-rendering mode on the device, with multi-viewport on when the platform is not headless.

Per frame, in this order:

1. `processEvent` for every raw SDL event, from `IApplication::nativeEvent`.
2. `beginFrame`, the application's ImGui calls, `endFrame`. `endFrame` runs `ImGui::Render`, once per frame whether or not anything is drawn.
3. `draw` inside a rendering scope on a swapchain image, from a render-graph pass.
4. `renderPlatformWindows` after the device's `endFrame`, which renders and presents the extra OS windows the Vulkan backend owns.

`registerImage` turns a sampled-capable `rhi::ImageHandle` into an `ImTextureID` for `ImGui::Image`; the image has to be in `ShaderReadOnly` when the draw data is recorded, which the graph guarantees when the ImGui pass declares it sampled. `unregisterImage` releases the descriptor after the frames in flight that may still reference it.

`ui` is the documented exception to the backend rule: it links `sonnet::rhi_vulkan` to reach the Vulkan objects behind the device, the command list and the images. Nothing else above `rhi` sees Vulkan. The vcpkg port compiles the backend against the Vulkan loader's prototypes; SDL loads the same library, so the process still has one loader.

`ui_tests` refuses a non-Vulkan device and, on Lavapipe, draws ImGui frames with a registered image into a headless swapchain and checks that validation stays silent.

## The editor module

| Header | Contents |
|---|---|
| `Editor.h` | `Editor`: owns the ImGui layer, the renderer, the render graph, the panels and the scene; `nativeEvent`, `event`, `update`, `render`, `afterPresent` in that order per frame |
| `ViewportPanel.h` | The dockable scene view: a `renderer::RenderTarget` sized to the panel, displayed with `ImGui::Image`, and the fly camera while the right mouse button is held over it |
| `FlyCamera.h` | Mouse look, W/A/S/D on the camera's plane, Q/E along the world's up, Shift for four times the speed, the wheel to scale it |
| `LogPanel.h` | `LogBuffer`, a spdlog sink registered with `core::Log` for the panel's lifetime, and the panel with a level threshold, text filter and auto-scroll |
| `StatisticsPanel.h` | Frame time history, per-pass CPU and GPU times from the graph, draw and triangle counts, VMA budget per heap; drawn as a window or as the overlay in the viewport's corner |
| `PrimitiveScene.h` | The M1 scene: every primitive on a ground plane, one box turning, one directional light. Replaced by `world` scenes in M2 |

The frame, as `apps/editor/main.cpp` orders it:

1. Events arrive through `nativeEvent` (to ImGui) and `event` (mouse deltas for the camera; the app itself handles quit and resize).
2. `update(dt)` advances the scene, opens the ImGui frame, lays out the dockspace (viewport in the centre, log below, statistics on the right, built once with the dock builder), draws the menu bar and the panels, and closes the ImGui frame. The viewport panel resizes its target to the panel and takes or releases relative mouse mode for the camera.
3. `render(commands, swapchainImage)` resets the graph, imports the viewport target and lets the renderer add the scene passes, then adds the ImGui pass into the swapchain image (cleared, with the viewport colour declared sampled) when there is an image, and executes the graph. It then records the frame's statistics.
4. `afterPresent` renders the platform windows.

Log entries show `time [level] [module] file:line message` coloured by level. The `file:line` becomes a link to the external editor once the preferences exist.

`editor_tests` covers the fly camera's maths, the log buffer through the `SONNET_LOG_*` macros, the primitive scene on the null device, and whole editor frames on Lavapipe including one without a swapchain image.

## Running it

```bash
./build/linux-debug/apps/editor/sonnet_editor
```

Right-drag in the viewport to look around, W/A/S/D/Q/E to move, Shift to go faster, the wheel to change the speed. The View menu toggles the panels and the overlay; Ctrl+Q quits.

## See also

- [Rendering](rendering.md), for the render graph and the viewport target
- [Roadmap](roadmap.md), M1 and M2
