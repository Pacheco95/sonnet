# platform

Window, input events, the application callback interface and file-system paths, behind interfaces owned by this module, with SDL3 as the only implementation ([ADR-0002](decisions/0002-sdl3-over-glfw.md)). Depends on `core`, SDL3 and the Vulkan headers.

| Header | Contents |
|---|---|
| `Platform.h` | `Platform`: owns the SDL video subsystem, creates windows, exposes paths and the Vulkan loader |
| `Window.h` | `IWindow` and `WindowDesc` |
| `Input.h` | `Key` (physical positions), `MouseButton`, `Modifiers` |
| `Event.h` | The event structs and the `Event` variant |
| `Application.h` | `IApplication`, `AppResult`, the `createApplication` declaration every executable defines |
| `EntryPoint.h` | Included once per executable; provides `main()` through SDL's callbacks |

## Lifecycle

The engine does not own `main()`. `EntryPoint.h` defines SDL's `SDL_AppInit`, `SDL_AppIterate`, `SDL_AppEvent` and `SDL_AppQuit` and forwards them to `platform`, which:

1. On init, initialises logging, constructs `Platform` (SDL video subsystem only), converts the arguments and calls the executable's `createApplication`. An exception here is logged at `critical` and ends the process with a failure code.
2. On iterate, calls `IApplication::iterate` and emits the Tracy frame mark.
3. On event, translates the SDL event and calls `IApplication::event` when the engine has a type for it; unknown SDL events are dropped in `platform`.
4. On quit, destroys the application, then `Platform`.

`IApplication::iterate` and `event` return `AppResult`: `Continue`, or `Success` and `Failure` to end the loop. On desktop SDL calls iterate in a loop; on iOS and Android the OS calls it, which is why frame ordering lives in the application and not in a loop the engine writes.

`EntryPoint.h` is the one public header that includes SDL, because `SDL_main.h` has to be compiled into the executable's translation unit. It is the reason `SDL3::SDL3` is a public dependency of the module; no other public header includes SDL.

## Window

`Platform::createWindow` returns an `IWindow`, which reports its logical and pixel sizes, its title and minimised state, and creates a `VkSurfaceKHR` for a given instance. The surface belongs to the caller (`rhi` wraps it in a RAII object) and must be destroyed before the window. Windows are created with `SDL_WINDOW_VULKAN` and `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so the pixel size is what the swapchain has to match.

The Vulkan loader is reached through `Platform::vulkanGetInstanceProcAddr` and `Platform::vulkanInstanceExtensions`, which load the library through SDL on first use. `rhi` seeds vk-bootstrap and the Vulkan-HPP dispatcher from that pointer, so one loader is used in the process ([ADR-0006](decisions/0006-vulkan-object-ownership.md)).

## Events

Events are values of the `Event` variant: window resize (pixel size), minimise and restore, focus, close request, quit request, key press and release with modifiers and repeat, UTF-8 text input, mouse motion with delta, mouse buttons with click count, and wheel. `Key` names physical positions with US-layout names, so game bindings survive keyboard layouts; text input carries the layout-aware characters. There is no window id on events until the engine has more than one window that receives input.

## Headless

`PlatformDesc::headless` selects SDL's offscreen video driver: windows exist and report sizes, nothing is displayed, and the Vulkan library still loads, so the same code paths run in tests and CI. `platform_tests` uses it for every test that needs the subsystem.

## Paths

`Platform::basePath` is the directory next to the executable on desktop and the bundle on mobile; `Platform::prefPath` is the per-user writable directory, created on demand. Both come from SDL so the mobile milestone does not change them.

## Tests

`platform_tests` covers the headless platform, window creation and sizes, paths, the Vulkan loader hook (skipped where no loader is installed) and, white-box through `src/SdlEvents.h`, the scancode and modifier mapping and the translation of every event type.
