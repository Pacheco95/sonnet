# Architecture

Sonnet is organised as small modules with strict one-way dependencies. Every module lives under `modules/<name>/`, exports its public headers under `sonnet/<name>/...`, uses the namespace `sonnet::<name>`, and carries its own Catch2 tests under `modules/<name>/tests/`.

## Module map

Modules are listed in dependency order. A module may depend only on modules earlier in that order.

| Module | Responsibility | Depends on |
|---|---|---|
| `core` | Fundamental types, `Handle<Tag>`, logging (spdlog), assertions, UUIDs, hashing, GLM configuration macros, Tracy zones | GLM, spdlog, Tracy |
| `platform` | `IWindow`, input event types, the application callback interface, file-system paths. Contains the SDL3 implementation | `core`, SDL3, Vulkan headers (surface handle types only) |
| `rhi` | Render hardware interface: device, swapchain, buffers, images, samplers, pipelines, command recording. Contains the Vulkan 1.4 implementation | `platform`, Vulkan-HPP, vk-bootstrap, VMA, Slang (runtime compile) |
| `renderer` | Render graph, frame resources, materials, meshes, cameras, lights, the rendering passes, engine shaders | `rhi` |
| `assets` | Asset identity, database, importers (glTF, images, KTX2, Slang), cooking, hot reload | `renderer`, fastgltf, stb, KTX, nlohmann-json |
| `world` | ECS world wrapper (flecs), core components, systems scheduling, scene load and save ([world.md](world.md)) | `assets`, flecs, nlohmann-json |
| `physics` | Rigid bodies, colliders, raycasts and debug outlines behind `IPhysicsWorld`, with the Jolt implementation ([physics.md](physics.md)) | `world`, Jolt |
| `scripting` | `IScriptRuntime`. Lua/sol2 implementation (M4) | `world`, Lua, sol2 |
| `audio` | `IAudioDevice`, sources and listeners. miniaudio or SDL3 audio implementation (M5) | `world` |
| `ui` | Dear ImGui layer: context, SDL3 and Vulkan backends, texture display, fonts. Editor and debug builds only | `rhi`, `platform`, ImGui |
| `editor` | Editor framework: panels, selection, commands with undo/redo, gizmos, picking, play mode, project handling. Desktop only | everything above |

Applications live under `apps/`:

- `apps/editor` links `editor` and is built only on desktop platforms.
- `apps/player` links the runtime modules (`world` and the subsystems, never `ui` in release or `editor`) and runs a project folder.
- `apps/samples/<name>` are project folders, not executables. They are opened by the editor and run by the player.

## Dependency rule

Compile-time dependencies point one way: a module includes headers and links targets only from modules earlier in the dependency order. CMake enforces this because each module's target only links its declared dependencies; a cycle fails to configure.

Upward communication is still needed, for example the platform layer must tell the engine about a resize, and the world must tell the renderer what to draw. It happens without a dependency, through mechanisms owned by the lower layer:

- **Interfaces** the lower layer declares and the upper layer implements. `platform` declares the application callback interface; the application implements it, receiving translated events and, for Dear ImGui's backend, the raw SDL ones.
- **Callbacks and events** the lower layer emits. Input events are values pushed into a queue the upper layer drains.
- **Data** the upper layer hands down. The renderer receives a list of draw items built by `world`; it never queries the ECS.

The ordering of a frame lives in one place, the application loop in `apps/`, so no module has to know who runs before or after it. `apps/editor/main.cpp` is that place for the editor: it calls the `editor::Editor` steps in order and owns the window, device and swapchain ([editor.md](editor.md)).

## What "agnostic" means

Two seams are abstracted, because both had to be swapped in previous iterations and both are large:

- **Platform**: `platform::IWindow`, the input event types and the application callback interface. SDL3 is the only implementation. It also provides the platform-owned main loop on mobile, see below.
- **GPU**: the `rhi` interfaces. Vulkan 1.4 is the only implementation and the interface is shaped by it (explicit synchronization, dynamic rendering, bindless resources). A future implementation would be another explicit API such as Direct3D 12 or Metal, not OpenGL.

Everything else is a fixed choice and is used directly: GLM types appear in public headers, flecs types appear in `world`, Slang is the only shader language. Wrapping these would cost more than the flexibility is worth.

Backend selection is compile-time. The CMake option `SONNET_RHI` accepts only `Vulkan` today. The only `#if`-switched site allowed in engine code is the factory that creates the device, so everything above `rhi` is build-flavour agnostic. The `ui` module is the one documented exception: Dear ImGui's Vulkan backend needs raw Vulkan objects, so `ui` may include the Vulkan implementation headers.

## Application lifecycle

The engine does not own `main()`. It implements the SDL3 callback model (`SDL_MAIN_USE_CALLBACKS`): init, iterate, event and quit callbacks. On desktop SDL calls iterate in a loop; on iOS and Android the OS owns the loop and calls back. Designing for callbacks from day one is what makes the mobile milestone a packaging job rather than a rewrite. An executable includes `sonnet/platform/EntryPoint.h` once and defines `platform::createApplication`; the mechanics are in [platform.md](platform.md#lifecycle).

Per iteration:

1. Drain platform events into the input state and the editor.
2. Run the simulation with a fixed timestep, catching up if the frame was long, up to a limit. Simulation systems live in `world` and the subsystems, which register them in the world's phases ([world.md](world.md#phases-and-play-mode), [ADR-0009](decisions/0009-physics-and-scripting.md)).
3. Interpolate transforms for rendering, build the frame's draw list and light list.
4. Execute the render graph and present.

Everything runs on the main thread until a job system exists. The render graph is designed so that recording can move to worker threads later without changing its API.

## Entity model

`world` wraps a flecs world. Entities are ids; components are plain structs registered with flecs reflection so the editor inspector, the scene serializer and the scripting layer can enumerate fields generically.

Core components, all in `world`:

- `Name`: display name.
- `Transform`: local position, rotation (quaternion), scale. World matrices are computed by a system from the flecs `ChildOf` hierarchy.
- `MeshRenderer`: mesh asset identity, an optional material override, a colour, visibility.
- `Camera`, `DirectionalLight`, `PointLight`, `SpotLight`, `Environment`.
- `Tags`: `Static`, `EditorOnly`, `Disabled`.

Hierarchy uses flecs `ChildOf` relationships, prefabs use `IsA`, so nested scene instances (a scene placed inside another scene) become prefab instantiation rather than a custom feature. Systems are flecs systems grouped in pipeline phases: `Input`, `FixedUpdate`, `Update`, `PostUpdate`, `PreRender`.

The decision, and EnTT as the alternative, is recorded in [ADR-0003](decisions/0003-ecs-library.md); the module as built is described in [world.md](world.md).

## Resource handles

GPU resources and assets are referenced by typed opaque handles from `core`: `Handle<Tag>` packs a 32-bit index and a 32-bit generation. The tag prevents cross-type assignment at compile time; the generation makes a stale handle detectable at runtime instead of silently aliasing a new resource. Handles are cheap to copy and are what components store. The owning store (`rhi` device for GPU objects, the asset database for assets) resolves them.

## Editor and player

- The **editor** is a desktop application built on the `editor` module. It opens a project folder, edits scenes and assets, and can play the current scene in place.
- The **player** is the generic runtime. It opens a project folder (or a cooked bundle), loads the start scene and runs it. It has no editor code and, in release builds, no ImGui. An exported game is the player binary plus the cooked project ([ADR-0007](decisions/0007-data-driven-game-structure.md)).

Play mode in the editor:

1. On play, the edited world is serialized to an in-memory snapshot and the runtime systems are enabled.
2. While playing, the editor camera and gizmos keep working on a separate editor-only entity set, and the inspector shows live values.
3. On stop, the world is restored from the snapshot. Changes made during play are discarded, matching what Unity and Godot do and what users expect.

Editor-only data (editor camera, selection, gizmo state) is tagged `EditorOnly` and never serialized into scenes.

## Error handling summary

Initialization and resource creation may throw; the hot loop never throws; recoverable operations such as loading an asset or compiling a shader return `std::expected` so the editor can show the error and continue. The full policy is in [conventions.md](conventions.md#error-handling).

## See also

- [Build system](build.md)
- [Rendering](rendering.md)
- [World](world.md)
- [Assets](assets.md)
- [Physics](physics.md)
- [Roadmap](roadmap.md)
