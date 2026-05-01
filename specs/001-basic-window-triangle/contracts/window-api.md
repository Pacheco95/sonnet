# Contract: Window API (`SonnetWindow`)

**Module**: `SonnetWindow` | **Date**: 2026-05-01 | **Plan**: [../plan.md](../plan.md)

---

## Purpose

`SonnetWindow` is a CMake static library module that provides an OS-independent abstraction over
native window management. For this feature, the sole implementation is `SDLWindow` (backed by SDL
3.4.4). Consumers interact exclusively through `IWindow` and `WindowFactory`; SDL3 is never
visible at the API boundary.

---

## Public Headers

| Header | Purpose |
|--------|---------|
| `sonnet/window/IWindow.hpp` | Abstract window interface |
| `sonnet/window/WindowFactory.hpp` | Factory functions returning `std::unique_ptr<IWindow>` |

---

## `IWindow` Interface

```cpp
namespace sonnet::window {

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual std::pair<int, int> getSize() const = 0;

    [[nodiscard]] virtual std::vector<std::string> getRequiredInstanceExtensions() const = 0;
    [[nodiscard]] virtual uint64_t createSurface(uint64_t instanceHandle) const = 0;
};

} // namespace sonnet::window
```

### Method Contracts

#### `bool init()`
- **Pre**: Object is in `Uninitialized` state.
- **Post (true)**: Native window is visible; OS event delivery is active; graphics surface
  integration methods are callable.
- **Post (false)**: A `SONNET_LOG_ERROR` message identifying the failure is emitted to stderr;
  all partially allocated resources are released; the object may be safely destroyed.
- **Thread safety**: Not thread-safe; call from the main thread only.

#### `void shutdown()`
- **Pre**: `init()` has previously returned `true`.
- **Post**: All OS window resources are released; the native window is no longer visible.
- **Idempotency**: A second call is a no-op (no crash, no log spam).
- **Thread safety**: Not thread-safe; call from the main thread only.

#### `bool shouldClose() const`
- **Returns**: `true` after the OS close gesture (window-X button, Alt-F4, etc.) has been
  delivered and processed; `false` otherwise.
- **Pre**: `init()` has returned `true`.

#### `std::pair<int, int> getSize() const`
- **Returns**: `{width, height}` in screen pixels.
- **Pre**: `init()` has returned `true`.
- **Note**: Values may change during the lifetime of the window (resize). Callers must not cache.

#### `std::vector<std::string> getRequiredInstanceExtensions() const`
- **Returns**: Names of extensions required by the underlying windowing system for graphics API
  instance creation (e.g., `"VK_KHR_surface"`, `"VK_KHR_xcb_surface"`). The renderer queries
  this before constructing its graphics instance.
- **Pre**: `init()` has returned `true`.
- **Ownership**: Returned strings are copies; their lifetime is independent of the window.

#### `uint64_t createSurface(uint64_t instanceHandle) const`
- **Returns**: An opaque surface handle (graphics API surface cast to `uint64_t`) on success;
  `0` on failure (with `SONNET_LOG_ERROR` emitted).
- **Pre**: `init()` has returned `true`; `instanceHandle` is a valid graphics API instance cast
  to `uint64_t`.
- **Post**: Caller owns the surface and is responsible for destroying it via the graphics API
  before destroying the instance.
- **Constraint**: Call at most once per graphics instance. The underlying implementation does
  not support multiple surfaces per window per instance.

---

## `WindowFactory` Interface

```cpp
namespace sonnet::window {

// Creates an SDLWindow with 800×600 resolution titled "Sonnet Engine".
std::unique_ptr<IWindow> createDefaultWindow();

// Creates an SDLWindow with the specified dimensions and title.
std::unique_ptr<IWindow> createWindow(int width, int height, std::string_view title);

} // namespace sonnet::window
```

### Guarantees
- Returns a non-null pointer; initialization is not performed by the factory.
- Callers must invoke `init()` on the returned object before use.
- The concrete type behind the returned pointer is an implementation detail; never `dynamic_cast`
  to `SDLWindow` in consumer code.

---

## CMake Usage

```cmake
target_link_libraries(MyTarget PRIVATE SonnetWindow)
# PUBLIC headers automatically become available via the transitive include path.
# SonnetWindow has no external PUBLIC CMake dependencies; its public headers use only the C++
# standard library (<cstdint>, <string>, <utility>, <vector>).
```

---

## Invariants

1. SDL3 types (`SDL_Window*`, `SDL_Event`, etc.) MUST NOT appear in any public header.
2. `<SDL3/SDL.h>` MUST NOT be transitively included by consumers of `SonnetWindow`.
3. Graphics API types (`VkInstance`, `VkSurfaceKHR`, etc.) MUST NOT appear in any public header.
   The `createSurface` / `getRequiredInstanceExtensions` methods use only `uint64_t` and
   `std::string` — standard C++ types with no external library dependency.
4. Public headers depend exclusively on `<cstdint>`, `<string>`, `<utility>`, and `<vector>`.
5. The module is buildable and testable without a running display server (headless) as long as
   no `init()` call is made; mock implementations must satisfy this constraint.
