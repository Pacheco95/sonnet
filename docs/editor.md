# Editor

Two modules, both editor-only and never linked by the player ([architecture.md](architecture.md#editor-and-player)): `ui` wraps Dear ImGui, `editor` is the editor. `apps/editor` is the executable that owns the window, device and swapchain and calls the editor's steps in frame order.

## The ui module

`sonnet/ui/ImGuiLayer.h` is the whole public surface. `ImGuiLayer` creates the Dear ImGui context with docking, keyboard navigation and DPI-scaled fonts, initialises the SDL3 backend on the platform window and the Vulkan backend in dynamic-rendering mode on the device, with multi-viewport on when the platform is not headless.

Per frame, in this order:

1. `processEvent` for every raw SDL event, from `IApplication::nativeEvent`.
2. `beginFrame`, the application's ImGui calls, `endFrame`. `endFrame` runs `ImGui::Render`, once per frame whether or not anything is drawn.
3. `draw` inside a rendering scope on a swapchain image, from a render-graph pass.
4. `renderPlatformWindows` after the device's `endFrame`, which renders and presents the extra OS windows the Vulkan backend owns.

`registerImage` turns a sampled-capable `rhi::ImageHandle` into an `ImTextureID` for `ImGui::Image`; the image has to be in `ShaderReadOnly` when the draw data is recorded, which the graph guarantees when the ImGui pass declares it sampled. `unregisterImage` releases the descriptor after the frames in flight that may still reference it.

`ui` is the documented exception to the backend rule: it links `sonnet::rhi_vulkan` to reach the Vulkan objects behind the device, the command list and the images. Nothing else above `rhi` sees Vulkan. The overlay port builds the backend without Vulkan prototypes and without linking a loader; the layer hands it the instance's `vkGetInstanceProcAddr` through `ImGui_ImplVulkan_LoadFunctions`, so the loader SDL opened stays the only one in the process ([ADR-0006](decisions/0006-vulkan-object-ownership.md)). Linking vcpkg's loader instead put it ahead of the system one on Linux, and it is built without X11 or Wayland surface support.

`ui_tests` refuses a non-Vulkan device and, on Lavapipe, draws ImGui frames with a registered image into a headless swapchain and checks that validation stays silent.

## The editor module

| Header | Contents |
|---|---|
| `Editor.h` | `Editor`: the world being edited, the panels, the undo history, play mode and the project; `nativeEvent`, `event`, `update`, `render`, `afterPresent` in that order per frame |
| `ViewportPanel.h` | The dockable scene view: a `renderer::RenderTarget` sized to the panel, displayed with `ImGui::Image`, the fly camera while the right mouse button is held over it, and `ViewportInput`, what the mouse did over the image this frame, for the gizmo and picking |
| `FlyCamera.h` | Mouse look, W/A/S/D on the camera's plane, Q/E along the world's up, Shift for four times the speed, the wheel to scale it |
| `HierarchyPanel.h` | The scene tree with selection, drag-and-drop reparenting and the context menu that creates, duplicates and deletes |
| `InspectorPanel.h` | The primary selection's name and components, with widgets generated from reflection |
| `Gizmo.h` | Translate, rotate and scale handles drawn over the viewport |
| `Selection.h` | The selected entities by identity; the last one is primary |
| `CommandStack.h`, `EntityCommands.h` | `ICommand`, the undo history, and the commands every edit goes through |
| `Project.h`, `Preferences.h` | The project folder with its `project.json`, and the per-user settings |
| `LogPanel.h` | `LogBuffer`, a spdlog sink registered with `core::Log` for the panel's lifetime, and the panel with a level threshold, text filter, auto-scroll and `file:line` links |
| `StatisticsPanel.h` | Frame time history, per-pass CPU and GPU times from the graph, draw and triangle counts, VMA budget per heap; drawn as a window or as the overlay in the viewport's corner |

The frame, as `apps/editor/main.cpp` orders it:

1. Events arrive through `nativeEvent` (to ImGui) and `event` (mouse deltas for the camera; the app itself handles quit and resize).
2. `update(dt)` opens the ImGui frame, lays out the dockspace (hierarchy left, viewport centre, inspector over statistics right, log bottom, built once with the dock builder), draws the menu bar, the shortcuts and the panels, and closes the ImGui frame. The panels edit the world; then the world's frame runs (`World::progress`: the simulation in play mode, the transform system always), the draw list is built from it ([world.md](world.md#draw-list)), and the outline list from the selection and everything under it.
3. `render(commands, swapchainImage)` first polls the picker, then resets the graph, imports the viewport target and declares the forward pass, the id pass into a transient id image, the selection mask pass into another, the outline pass over the colour and the pick readback, then the ImGui pass into the swapchain image (cleared, with the viewport colour declared sampled) when there is an image, and executes the graph. It then records the frame's statistics.
4. `afterPresent` renders the platform windows.

The Debug build of the application turns on the flecs explorer through `World`.

## Selection and picking

The selection holds UUIDs, so it survives undo, play mode and reloads; the last selected entity is the primary one, what the inspector shows and the gizmo moves. Clicking in the hierarchy selects, Ctrl toggles, Shift adds. Clicking in the viewport, away from a gizmo handle and with the camera idle, asks the `renderer::Picker` for the id under the cursor; the answer comes two frames later, when the readback of that frame has completed, and the selection changes then, with the same modifiers. Selected entities and their descendants are drawn into the selection mask and outlined from it ([rendering.md](rendering.md#the-renderer-module-today)), so an object in front of a selected one is never outlined itself. Escape clears the selection, F turns the camera towards it.

## Gizmos

W, E and R over the viewport choose translate, rotate and scale (the same keys fly the camera while the right button is held). Translation and rotation work along the world axes, scale along the entity's own. A drag starts on the handle under the cursor and resolves every mouse position against the axis line or the rotation plane from the drag's fixed start position, never from the object's current one, so the maths never chases what it moves; the handles themselves are drawn at the object's current position, taken from its local transform in the same frame, so they follow it as it moves. The live edit writes the local transform directly; when the button is released, one command with the transform before and after goes on the stack. The maths (`project`, `rayDirection`, `axisRayParam`, `planeRayHit`) is public for the tests, which drive drags through `GizmoView` without Dear ImGui.

## Inspector

The inspector walks the selected entity's registered components ([world.md](world.md#components)) and generates a widget per reflected member: drag fields for floats and integers, degrees for members with the radians unit and for the rotation quaternion, colour pickers for `vec3` and `vec4` members named `color`, a combo for enums, a checkbox for booleans, nested structs recursively. A component header has a close button that removes it, except `Transform`; "Add component" lists what the entity lacks. A prefab instance shows the component as inherited until a widget touches it, which gives the instance its own copy ([world.md](world.md#prefabs)).

Widgets change the world live. The value before the first widget of a component is activated is kept, and when a widget is released after an edit one command with the values before and after is pushed, so a long drag is one undo step. The name field works the same way with a rename command.

## Undo and redo

Every edit is an `ICommand` with `apply` and `revert`, pushed on the `CommandStack`, which applies it, drops the redo list and keeps the last 256. Commands refer to entities by UUID, never by flecs id, and a deleted subtree comes back with the identities it had, so later commands keep working after undo and redo. The commands: create, delete, duplicate (fresh identities, decided once so redo makes the same copy), reparent (keeping the world transform, restoring the exact local one on undo), rename, a component set, add or remove holding both values, prefab instantiation, and a composite for a multi-selection delete or duplicate. Ctrl+Z and Ctrl+Y (or Ctrl+Shift+Z) and the Edit menu, which names what they would do. The stack's revision compared with the one at the last save is the dirty flag in the window title.

## Play mode

Play (Ctrl+P, the Play menu, the button on the menu bar) serialises the scene to an in-memory snapshot and switches the world to the simulation pipeline; the editor camera, gizmos, inspector and hierarchy keep working on the live entities. Stop restores the snapshot, discards the edits made while playing and their undo history, and keeps the selection by identity. Saving while playing writes the snapshot, not the running state. The rotating object of the roadmap's M2 criterion is the `Spin` component's system.

## Projects and scenes

A project is a folder with a `project.json` ([assets.md](assets.md#project-file)), opened from the command line (`sonnet_editor <folder>`), the File menu, or the recent list in the preferences. Opening a project loads every `.prefab.json` under it and then the start scene; New project writes the manifest and a starter scene (a ground plane, a box, a sun and a camera), which is also the scene an editor without a project starts from. Open scene lists the project's `.scene.json` files; Ctrl+S saves, Save as asks for a path. File paths are typed into a modal for now; native dialogs are a later refinement. Failures (a missing prefab, a newer file version, an unreadable file) are logged with their origin and the editor keeps a usable scene.

Preferences live in `preferences.json` under `Platform::prefPath("sonnet", "editor")`: the recent projects and the external editor command. A `file:line` in the log panel is a link that runs that command with the placeholders filled in (`code --goto {file}:{line}` by default). Log records name repository-relative files and binaries carry no build-machine paths, so the editor finds the file at click time: under `sourceRoot` when set, otherwise in the directories from the executable's upwards, which finds the checkout a build directory lives in. A file found nowhere is a warning, not an empty document in the external editor.

`editor_tests` covers the selection, every command through undo and redo, the gizmo's maths and headless drags, projects and preferences through the temporary directory, the fly camera, the log buffer, and on Lavapipe whole editor frames: the starter scene, a created project, an edit, play and stop, save and reopen, with picking and the outline in the frames and validation silent.

## Running it

```bash
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic
```

Right-drag in the viewport to look around, W/A/S/D/Q/E to move, Shift to go faster, the wheel to change the speed. Left-click to select, W/E/R for the gizmo mode, F to focus, Delete, Ctrl+D, Ctrl+Z and Ctrl+Y as usual, Ctrl+S to save, Ctrl+P to play and stop. The View menu toggles the panels and the overlay; Ctrl+Q quits.

## See also

- [Rendering](rendering.md), for the render graph, the viewport target, the id and outline passes and the picker
- [World](world.md), for the components, the scene format and prefabs
- [Roadmap](roadmap.md), M1 and M2
