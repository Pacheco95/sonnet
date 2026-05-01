# Data Model: Basic Window with Triangle Rendering

**Phase**: 1 | **Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

---

## Entities

### Window

The native OS window surface managed by the engine.

| Attribute | Type | Description |
|-----------|------|-------------|
| title | `std::string` | Window title bar text (`"Sonnet Engine"` for this feature) |
| width | `int` | Window width in pixels (default: 800) |
| height | `int` | Window height in pixels (default: 600) |
| closeRequested | `bool` | Set to `true` when the OS close gesture is received |

**Identity**: Single instance per engine process for this feature. No identity key needed.

**Lifecycle / State Transitions**:

```
[Uninitialized]
      │
      ▼  init() → true
  [Open] ──────────────────────────────────────────────► [Error]
      │                                                      ▲
      │  OS close event / SDL_APP_SUCCESS                   │
      ▼                                              init() → false
  [Close Requested]
      │
      ▼  shutdown()
 [Destroyed]
```

- `init()` transitions from `Uninitialized` to `Open` (returns `true`) or `Error` (returns `false`).
- `shouldClose()` returns `true` once in `Close Requested` state.
- `shutdown()` must be called in `Open` or `Close Requested` states; no-op after `Destroyed`.

---

### Frontend Renderer

The high-level rendering coordinator. Knows what to draw (e.g., a triangle) but not how to
draw it with any specific graphics API. Delegates all frame execution to the backend.

| Attribute | Type | Description |
|-----------|------|-------------|
| initialized | `bool` | Whether the backend has been successfully initialized |
| frameCount | `uint64_t` | Total frames rendered since init (for diagnostics) |

**Lifecycle / State Transitions**: identical to Backend Renderer below; the frontend's state
mirrors its backend's state.

- `renderFrame()` calls `backend.beginFrame()` → `backend.drawPrimitive()` → `backend.endFrame()`.
- `shutdown()` delegates to the backend; the frontend owns and destroys the backend.

---

### Backend Renderer

The graphics API implementation. For this feature, backed by Vulkan. Knows how to initialize
the graphics device, manage the swapchain, and execute draw commands.

| Attribute | Type | Description |
|-----------|------|-------------|
| initialized | `bool` | Whether Vulkan resources have been allocated |
| swapchainValid | `bool` | Whether the swapchain matches current window dimensions |

**Lifecycle / State Transitions**:

```
[Uninitialized]
      │
      ▼  init(window) → true
  [Ready] ──────────────────────────────────────────────► [Error]
      │                                                      ▲
      │  beginFrame/drawPrimitive/endFrame() (repeated)     │
      ▼                                              init() → false
  [Rendering ...]
      │
      ▼  shutdown()
 [Destroyed]
```

- `init(IWindow&)` uses the window's agnostic surface methods (`getRequiredInstanceExtensions`,
  `createSurface`) to set up Vulkan without depending on SDL3 types.
- `beginFrame()` handles swapchain rebuilds transparently on resize.
- `shutdown()` releases all Vulkan resources in reverse-allocation order.

---

### Triangle

A geometric primitive with three vertices and a solid fill color. **Not a runtime entity** — all
values are hardcoded in the GLSL vertex shader. There is no C++ Triangle class or data structure.

| Attribute | Value | Description |
|-----------|-------|-------------|
| vertex[0] | NDC (0.0, −0.5) | Top vertex |
| vertex[1] | NDC (0.5, 0.5) | Bottom-right vertex |
| vertex[2] | NDC (−0.5, 0.5) | Bottom-left vertex |
| color[0] | RGB (1.0, 0.0, 0.0) | Red |
| color[1] | RGB (0.0, 1.0, 0.0) | Green |
| color[2] | RGB (0.0, 0.0, 1.0) | Blue |

**Rationale**: FR-007 forbids additional geometry; hardcoding in the shader is the minimal
implementation and avoids any vertex buffer, descriptor set, or uniform buffer complexity.

---

### AppState

Internal runtime coupling struct owned by the SDL3 callback `appstate` pointer. This is not a
public entity — it lives entirely within `src/app/main.cpp`.

| Field | Type | Description |
|-------|------|-------------|
| window | `std::unique_ptr<IWindow>` | Active window instance |
| renderer | `std::unique_ptr<IRenderer>` | Frontend renderer (owns the backend internally) |

`AppState` is heap-allocated in `SDL_AppInit` (written to `*appstate`) and deleted in
`SDL_AppQuit`. Both members are shut down before deletion:

```cpp
// SDL_AppQuit
auto* state = static_cast<AppState*>(appstate);
if (state) {
    state->renderer->shutdown();
    state->window->shutdown();
    delete state;
}
```

---

## Interfaces

### `IWindow` (`src/window/include/sonnet/window/IWindow.hpp`)

Public abstract interface for the window module. Consumers depend on this type; the SDL3
implementation (`SDLWindow`) is a private detail of `SonnetWindow`.

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sonnet::window {

class IWindow {
public:
    virtual ~IWindow() = default;

    // Lifecycle
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // State queries
    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual std::pair<int, int> getSize() const = 0;

    // Graphics surface integration (framework-agnostic)
    [[nodiscard]] virtual std::vector<std::string> getRequiredInstanceExtensions() const = 0;
    [[nodiscard]] virtual uint64_t createSurface(uint64_t instanceHandle) const = 0;
};

} // namespace sonnet::window
```

**Preconditions**:
- `shutdown()` must not be called before `init()` returns `true`.
- `getRequiredInstanceExtensions()` and `createSurface()` require `init()` to have succeeded.
- `createSurface()` may only be called once per graphics instance handle.

**Postconditions**:
- After `shutdown()`, all OS window resources are released; the object may be destroyed safely.
- `shouldClose()` returns `true` after the OS close gesture is delivered.

---

### `IRenderer` (`src/renderer/include/sonnet/renderer/IRenderer.hpp`)

Public abstract interface of the frontend renderer (`SonnetRenderer`). Consumers depend on
this type; the concrete `Renderer` class is a private implementation detail.

```cpp
#pragma once

namespace sonnet::window { class IWindow; }

namespace sonnet::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;
    virtual void renderFrame() = 0;
};

} // namespace sonnet::renderer
```

**Preconditions**:
- `window` passed to `init()` must be in `Open` state.
- `renderFrame()` must only be called after `init()` returns `true`.
- `shutdown()` must not be called before `init()` returns `true`.

**Postconditions**:
- After `shutdown()`, the backend is shut down and all graphics resources are released; no GPU
  work is in flight.

---

### `IRendererBackend` (`src/renderer/include/sonnet/renderer/IRendererBackend.hpp`)

Backend contract defined by the frontend module (`SonnetRenderer`). Backend implementations
(e.g., `VulkanBackend` in `SonnetRendererVulkan`) satisfy this interface.

```cpp
#pragma once

namespace sonnet::window { class IWindow; }

namespace sonnet::renderer {

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame() = 0;
    virtual void drawPrimitive() = 0;
    virtual void endFrame() = 0;
};

} // namespace sonnet::renderer
```

**Preconditions**:
- `window` passed to `init()` must be in `Open` state.
- `beginFrame()`, `drawPrimitive()`, `endFrame()` must only be called after `init()` returns `true`.
- `drawPrimitive()` must only be called between a `beginFrame()` / `endFrame()` pair.

**Postconditions**:
- After `shutdown()`, all graphics resources are released in reverse-allocation order.

---

## Validation Rules

| Rule | Scope | Enforcement |
|------|-------|-------------|
| `init()` must be called before any other method | IWindow, IRenderer, IRendererBackend | Assertion in debug builds via `SONNET_LOG_ERROR` + return `false` |
| `shutdown()` is idempotent | IWindow, IRenderer, IRendererBackend | Implementation tracks initialized state; second call is a no-op |
| `createSurface()` called once per graphics instance | IWindow | Caller responsibility; documented in contract |
| `drawPrimitive()` called only between `beginFrame` / `endFrame` | IRendererBackend | Enforced by `Renderer::renderFrame()` call order |
| Triangle vertices are fixed | Triangle (shader) | No runtime configuration interface exists |
| Window default size is 800×600 | SDLWindow | Enforced in `WindowFactory::createDefaultWindow()` |
