# Contract: Renderer APIs (`SonnetRenderer` + `SonnetRendererVulkan`)

**Date**: 2026-05-01 | **Plan**: [../plan.md](../plan.md)

---

## Overview

The rendering system is split into two CMake modules with a strict boundary:

- **`SonnetRenderer`** (frontend) — high-level rendering concepts; exposes `IRenderer` to the
  app and `IRendererBackend` as the contract backend implementations must satisfy. Has no
  knowledge of any graphics API.
- **`SonnetRendererVulkan`** (backend) — Vulkan implementation of `IRendererBackend`; all
  graphics API details are isolated here.

```
SonnetApp → SonnetRendererVulkan → SonnetRenderer → SonnetWindow
```

---

## Part 1: Frontend (`SonnetRenderer`)

### Public Headers

| Header | Purpose |
|--------|---------|
| `sonnet/renderer/IRenderer.hpp` | High-level renderer interface (what the app calls) |
| `sonnet/renderer/IRendererBackend.hpp` | Backend contract (what backend impls must satisfy) |
| `sonnet/renderer/RendererFactory.hpp` | Factory: wraps a backend in a frontend `IRenderer` |

---

### `IRenderer` Interface

```cpp
// sonnet/renderer/IRenderer.hpp
#pragma once

namespace sonnet::window { class IWindow; }

namespace sonnet::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;

    // Render one frame; called every SDL_AppIterate.
    virtual void renderFrame() = 0;
};

} // namespace sonnet::renderer
```

#### Method Contracts

##### `bool init(sonnet::window::IWindow& window)`
- **Pre**: `window.init()` has returned `true` (window is in `Open` state).
- **Post (true)**: The backend is initialized; `renderFrame()` is callable.
- **Post (false)**: A `SONNET_LOG_ERROR` message is emitted; all partially allocated resources
  are released; the object may be safely destroyed.
- **Thread safety**: Not thread-safe; call from the main thread only.

##### `void shutdown()`
- **Pre**: `init()` has previously returned `true`.
- **Post**: The backend is shut down; all graphics resources are released; no GPU work is in
  flight.
- **Idempotency**: A second call is a no-op.
- **Thread safety**: Not thread-safe; call from the main thread only.

##### `void renderFrame()`
- **Pre**: `init()` has returned `true`.
- **Post**: The backend has recorded and submitted one frame; the result is presented to the
  window surface.
- **Thread safety**: Not thread-safe; call from the main thread only.

---

### `IRendererBackend` Interface

The backend contract. The frontend calls these methods; implementations (e.g., `VulkanBackend`)
fulfill them without exposing graphics API types to the frontend.

```cpp
// sonnet/renderer/IRendererBackend.hpp
#pragma once

namespace sonnet::window { class IWindow; }

namespace sonnet::renderer {

class IRendererBackend {
public:
    virtual ~IRendererBackend() = default;

    virtual bool init(sonnet::window::IWindow& window) = 0;
    virtual void shutdown() = 0;

    // Frame lifecycle — called in order each frame by the frontend.
    virtual void beginFrame() = 0;
    virtual void drawPrimitive() = 0;
    virtual void endFrame() = 0;
};

} // namespace sonnet::renderer
```

#### Method Contracts

##### `bool init(sonnet::window::IWindow& window)`
- **Pre**: `window.init()` has returned `true`.
- **Post (true)**: All backend resources (device, surface, swapchain, pipeline, sync objects)
  are allocated; `beginFrame()` / `endFrame()` are callable.
- **Post (false)**: A `SONNET_LOG_ERROR` is emitted; partial resources released.

##### `void shutdown()`
- **Pre**: `init()` returned `true`.
- **Post**: All backend resources released in reverse-allocation order; device idle before
  destruction (no in-flight GPU work).
- **Idempotency**: A second call is a no-op.

##### `void beginFrame()`
- **Pre**: `init()` returned `true`; no frame currently in progress.
- **Post**: Backend is ready to accept draw commands for this frame. On swapchain
  out-of-date (resize), the backend rebuilds the swapchain and skips the frame.

##### `void drawPrimitive()`
- **Pre**: `beginFrame()` has been called for the current frame.
- **Post**: A draw command for the hardcoded triangle has been recorded.
- **Note**: For this feature the primitive is fixed (3 hardcoded vertices in the shader). Future
  features will extend this to accept geometry data.

##### `void endFrame()`
- **Pre**: `beginFrame()` was called; draw commands have been recorded.
- **Post**: The frame is submitted to the GPU and the result is queued for presentation.

---

### `RendererFactory` Interface

```cpp
// sonnet/renderer/RendererFactory.hpp
#pragma once
#include <memory>

namespace sonnet::renderer {

class IRenderer;
class IRendererBackend;

// Creates a frontend IRenderer that owns and delegates to the given backend.
// backend must be non-null; the returned IRenderer takes ownership.
std::unique_ptr<IRenderer> createRenderer(std::unique_ptr<IRendererBackend> backend);

} // namespace sonnet::renderer
```

#### Guarantees
- Returns a non-null pointer; `init()` is not called by the factory.
- The frontend takes exclusive ownership of `backend`; the caller must not retain a pointer to
  it after this call.

---

### CMake Usage

```cmake
target_link_libraries(MyTarget PRIVATE SonnetRenderer)
# IWindow is transitively available via SonnetWindow (PUBLIC dep of SonnetRenderer).
# No graphics API headers are exposed.
```

### Frontend Invariants

1. `IRenderer.hpp` and `IRendererBackend.hpp` MUST NOT include any graphics API header
   (`<vulkan/vulkan.h>`, `<vulkan/vulkan.hpp>`, etc.).
2. `SonnetRenderer` has no knowledge of Vulkan, Metal, DirectX, or any rendering library.
3. The frontend's `renderFrame()` delegates entirely to the backend frame lifecycle methods;
   no rendering logic is duplicated between layers.
4. `SonnetWindow` is a PUBLIC CMake dependency (for `IWindow&` in `IRendererBackend::init`);
   its header is visible to consumers of `SonnetRenderer`.

---

## Part 2: Backend (`SonnetRendererVulkan`)

### Public Headers

| Header | Purpose |
|--------|---------|
| `sonnet/renderer/VulkanBackendFactory.hpp` | Factory: creates the Vulkan `IRendererBackend` |

---

### `VulkanBackendFactory` Interface

```cpp
// sonnet/renderer/VulkanBackendFactory.hpp
#pragma once
#include <memory>

namespace sonnet::renderer {

class IRendererBackend;

// Creates a VulkanBackend. Does not call init(); caller must do so.
std::unique_ptr<IRendererBackend> createVulkanBackend();

} // namespace sonnet::renderer
```

#### Guarantees
- Returns a non-null pointer.
- The concrete type is an implementation detail; never `dynamic_cast` in consumer code.

---

### CMake Usage

```cmake
target_link_libraries(MyTarget PRIVATE SonnetRendererVulkan)
# SonnetRenderer (and transitively SonnetWindow) is a PUBLIC dep of SonnetRendererVulkan
# and is inherited automatically.
```

---

### Vulkan Resource Allocation Order

The following order is enforced inside `VulkanBackend` to enable clean reverse-order teardown:

1. `VkInstance` (+ debug messenger in debug builds)
2. `VkSurfaceKHR` (via `IWindow::createSurface`)
3. Physical device selection (`VkPhysicalDevice` — no cleanup needed)
4. `VkDevice` + queue handles
5. `VkSwapchainKHR` + swapchain images/views
6. `VkRenderPass`
7. `VkPipelineLayout` + `VkPipeline`
8. `VkFramebuffer` (per swapchain image)
9. `VkCommandPool` + `VkCommandBuffer` (per swapchain image)
10. Synchronization objects (`VkSemaphore` × 2, `VkFence` × 1 per in-flight frame)
11. Shader modules (`VkShaderModule` — may be destroyed after pipeline creation)

Teardown proceeds in reverse order (10 → 1).

---

### Validation Layers (Debug Only)

- `VK_LAYER_KHRONOS_validation` is requested when `SONNET_ENABLE_VALIDATION_LAYERS=ON` (default
  for `Debug` builds).
- The debug messenger callback routes Vulkan diagnostics to `SONNET_LOG_WARN` (warning) and
  `SONNET_LOG_ERROR` (error).
- If the layer is unavailable, a `SONNET_LOG_WARN` is emitted and init proceeds without it.

---

### Backend Invariants

1. Vulkan types (`VkInstance`, `vk::Device`, etc.) MUST NOT appear in any public header of
   `SonnetRendererVulkan`.
2. `<vulkan/vulkan.h>` and `<vulkan/vulkan.hpp>` are confined to PRIVATE source files.
3. `SonnetRendererVulkan` depends on `SonnetRenderer` (PUBLIC) to access `IRendererBackend`;
   it must not bypass the interface to call frontend internals.
4. The backend does not own the `IWindow` reference; the frontend (and ultimately the app)
   retains ownership and must keep the window alive until `shutdown()` returns.
