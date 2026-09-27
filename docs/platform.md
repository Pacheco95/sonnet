# platform

Window, input events, the application callback interface and file-system paths, behind interfaces owned by this module, with SDL3 as the only implementation ([ADR-0002](decisions/0002-sdl3-over-glfw.md)). Depends on `core`, SDL3 and the Vulkan headers.

| Header | Contents |
|---|---|
| `Platform.h` | `Platform`: owns the SDL video subsystem, creates windows, exposes paths, opens content and exposes the Vulkan loader |
| `Content.h` | `ContentStream`: a seekable, read-only stream over the game's content |
| `Window.h` | `IWindow` and `WindowDesc`; the SDL window handle for Dear ImGui's backend and relative mouse mode |
| `Input.h` | `Key` (physical positions), `MouseButton`, `Modifiers`, and their names both ways |
| `InputState.h` | `InputState` and `Touch`: the keyboard, mouse and touches as state, fed from events |
| `Event.h` | The event structs and the `Event` variant |
| `Application.h` | `IApplication` (`iterate`, `event`, `nativeEvent`), `AppResult`, the `createApplication` declaration every executable defines |
| `EntryPoint.h` | Included once per executable; provides `main()` through SDL's callbacks |

## Lifecycle

The engine does not own `main()`. `EntryPoint.h` defines SDL's `SDL_AppInit`, `SDL_AppIterate`, `SDL_AppEvent` and `SDL_AppQuit` and forwards them to `platform`, which:

1. On init, initialises logging ([Logging](#logging)), constructs `Platform` (SDL video subsystem only), converts the arguments (without the program name) and calls the executable's `createApplication`. An exception here is logged at `critical` and ends the process with a failure code.
2. On iterate, calls `IApplication::iterate` and emits the Tracy frame mark.
3. On event, hands the raw SDL event to `IApplication::nativeEvent` (Dear ImGui's SDL3 backend consumes it in `ui`), then translates it and calls `IApplication::event` when the engine has a type for it; SDL events without an engine type go no further.
4. On quit, destroys the application, then `Platform`.

`IApplication::iterate` and `event` return `AppResult`: `Continue`, or `Success` and `Failure` to end the loop. On desktop SDL calls iterate in a loop; on iOS and Android the OS calls it, which is why frame ordering lives in the application and not in a loop the engine writes.

`EntryPoint.h` is the one public header that includes SDL, because `SDL_main.h` has to be compiled into the executable's translation unit. It is the reason `SDL3::SDL3` is a public dependency of the module; no other public header includes SDL. `Application.h` and `Window.h` forward-declare `SDL_Event` and `SDL_Window` so that `ui` can hand both to Dear ImGui's SDL3 backend without the headers.

## Logging

The log itself is `core`'s ([core.md](core.md#logging)); `platform` only adds to where it goes. On Android the entry point adds spdlog's `android_sink_mt` through `core::Log::addSink` before the first line is logged, with the tag `Sonnet`, the one `SonnetActivity` logs its arguments under, so `adb logcat -s Sonnet` shows the engine's log from startup. Android discards a process's stdout, which is why the sink is needed. The console sink stays. `platform` links `liblog` for it on Android. Nothing changes on desktop.

## Window

`Platform::createWindow` returns an `IWindow`, which reports its logical and pixel sizes, its title and minimised state, toggles relative mouse mode (the cursor hidden and motion reported as deltas, for the editor's fly camera; it reports failure, which SDL's X11 driver does without XInput2 and the offscreen driver always), exposes the `SDL_Window` for Dear ImGui, and creates a `VkSurfaceKHR` for a given instance. The surface belongs to the caller (`rhi` wraps it in a RAII object) and must be destroyed before the window. Windows are created with `SDL_WINDOW_VULKAN` and `SDL_WINDOW_HIGH_PIXEL_DENSITY`, so the pixel size is what the swapchain has to match.

The Vulkan loader is reached through `Platform::vulkanGetInstanceProcAddr` and `Platform::vulkanInstanceExtensions`, which load the library through SDL on first use. `rhi` seeds vk-bootstrap and the Vulkan-HPP dispatcher from that pointer, so one loader is used in the process ([ADR-0006](decisions/0006-vulkan-object-ownership.md)). The library is then kept mapped for the life of the process: `Platform` takes a reference of its own to whichever library the loader pointer belongs to, through `dladdr` and `dlopen` or, on Windows, a pinned module handle, and never releases it. SDL unloads the loader when its video subsystem quits, while vk-bootstrap caches the entry points in a table it initialises once and never refreshes, so a second `Platform` in one process would otherwise leave those pointers in an unmapped library: a crash on macOS, and elsewhere a crash whenever the library does not land at its old address. `rhi_tests` covers it with a device created after an earlier device and its platform are gone.

## Events

Events are values of the `Event` variant: window resize (pixel size), minimise and restore, focus, close request, quit request, key press and release with modifiers and repeat, UTF-8 text input, mouse motion with delta, mouse buttons with click count, wheel, and a finger's `TouchDown`, `TouchMotion` with delta and `TouchUp` ([ADR-0018](decisions/0018-mobile-export.md#where-it-lives)). `Key` names physical positions with US-layout names, so game bindings survive keyboard layouts; text input carries the layout-aware characters. There is no window id on events until the engine has more than one window that receives input.

Positions, the mouse's and the touches' alike, are in window coordinates: SDL's for the mouse, and for a finger SDL's fraction of the window times the window's size, which is how SDL places the mouse it synthesises from the same finger. On Android the window's size is its surface's in pixels, so these are pixels there. On a desktop with a high-density display they are the logical coordinates, smaller than the pixel size. A touch carries SDL's finger id, which stays the same from down to up. A cancelled finger, which Android sends when a system gesture takes it, ends with a `TouchUp`.

**Touches and the mouse.** SDL's synthesised mouse events stay on (`SDL_HINT_TOUCH_MOUSE_EVENTS`), so one finger works as the left button for scripts written against the mouse. A finger therefore arrives twice, once as a touch and once as the mouse, and the rules keep each report whole:

- **Only the first finger drives the mouse.** SDL tracks one finger at a time, so a second finger is a touch and nothing else.
- **A finger's landing is not motion.** Before pressing the left button, SDL moves its mouse to where the finger lands. That move (`which` is `SDL_TOUCH_MOUSEID`, the left button not yet held) keeps its position with a zero delta, since it is a jump from wherever the last finger lifted. Its moves while held are motion like a mouse's.
- **A mouse or a pen is never a touch.** On Android and iOS SDL reports a mouse as a touch too (`SDL_HINT_MOUSE_TOUCH_EVENTS` defaults on there, under `SDL_MOUSE_TOUCHID`), and a pen as a touch everywhere (`SDL_PEN_TOUCHID`). Both already arrive as the mouse, so their finger events are dropped.

`InputState` keeps the two apart: the mouse's buttons and position, and the touches. A script reading the left button sees the first finger, and a script reading the touches sees every finger. A script that reads both sees the first finger in each and should use one or the other.

## Input state

`InputState` turns the event stream into what a game asks for: which keys and buttons are held, which went down or up this frame, where the pointer and the fingers are and how far they and the wheel moved this frame. The application hands it the events the game should see and calls `beginFrame` once its frame has consumed them; a key's repeats are not new presses, and losing the window's focus, or `releaseAll`, releases everything held, since those releases would never arrive. The touches are the fingers down now, in the order they went down, each with its id, position and this frame's motion (`touches()`, a span of `Touch`). `beginFrame` zeroes the motion as it does the mouse's, and a `TouchUp` removes the finger. A finger whose down the state never saw, because it landed before the game was listening, is ignored until it lifts, and losing the focus or `releaseAll` ends every touch. A tap shorter than a frame shows as the left button's press and release, not as a touch. At most `InputState::MaxTouches` (10) fingers are kept, in a fixed array, so handling an event never allocates. `toString` and `keyFromName` convert keys to and from their enumerator names (`"A"`, `"Digit1"`, `"LeftShift"`), and likewise for mouse buttons, which is how scripts name them ([scripting.md](scripting.md#input)).

## Headless

`PlatformDesc::headless` selects SDL's offscreen video driver: windows exist and report sizes, nothing is displayed, and the Vulkan library still loads, so the same code paths run in tests and CI. SDL assertions abort instead of opening their dialog. `platform_tests` uses it for every test that needs the subsystem.

## Paths

`Platform::basePath` is the directory next to the executable on desktop and the bundle on mobile; `Platform::prefPath` is the per-user writable directory, created on demand. Both come from SDL so the mobile milestone does not change them.

`Platform::openContent(path)` opens the game's content read-only, as [ADR-0018](decisions/0018-mobile-export.md#packaging) decides. It returns a `ContentStream` over an `SDL_IOStream`, which `Content.h` forward-declares so SDL stays out of the header. The stream has `read` (up to a buffer's size, short only at the end), `readExactly`, `seek` (from the start; past the end is an error), `tell`, `size` and `readAll`. Errors are `Io` errors naming the path. A stream has one position, so it is not shared between threads. The rules for the path:

- **A relative path** resolves against the content root. On Android that is the APK's `assets/`, which SDL reads in place, since the APK stores the content uncompressed. SDL first tries the relative path in the app's internal storage, so a file of that name there takes precedence. On every other platform the content root is `basePath()`.
- **An absolute path** is an ordinary file on every platform. On a phone, this is how a bundle pushed into the app's data directory runs.

`openContent` is static because it needs nothing that SDL initialises, and so `Bundle` and the renderer, which are handed a path rather than the `Platform`, can reach it. What reads through it: the bundle ([assets.md](assets.md#the-bundle)), the renderer's shaders ([rendering.md](rendering.md#shaders)) and the player's default `game.sbundle` ([player.md](player.md)). The desktop tools, project folders and the tests stay on `core::readFile`.

## Tests

`platform_tests` covers `openContent` over plain files (relative and absolute paths, a missing file, seek, tell, size, a read at an offset, a short read, a move), the input state's held keys and one-frame edges, motion and wheel, the release on focus loss, touches appearing, moving with a motion that resets each frame and ending, and the key names, the headless platform, window creation and sizes, paths, the Vulkan loader hook (skipped where no loader is installed) and, white-box through `src/SdlEvents.h`, the scancode and modifier mapping and the translation of every event type. The finger events go through the same translation in a headless window of known size: two fingers down at once, motion, up and cancel, scaled from SDL's fractions to window coordinates. So do a mouse and a pen reported as touches, which are dropped, and the landing of a finger's synthesised mouse.
