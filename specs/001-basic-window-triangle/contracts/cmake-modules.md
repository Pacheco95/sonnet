# Contract: CMake Module Dependencies

**Date**: 2026-05-01 | **Plan**: [../plan.md](../plan.md)

---

## Module Inventory

| CMake Target | Type | Public Headers | Description |
|---|---|---|---|
| `SonnetLogging` | Static library | `sonnet/logging/Logger.hpp` | spdlog wrapper; `SONNET_LOG_*` macros |
| `SonnetWindow` | Static library | `sonnet/window/IWindow.hpp`, `sonnet/window/WindowFactory.hpp` | Abstract window interface; SDL3-backed impl |
| `SonnetRenderer` | Static library | `sonnet/renderer/IRenderer.hpp`, `sonnet/renderer/IRendererBackend.hpp`, `sonnet/renderer/RendererFactory.hpp` | **Frontend**: high-level rendering concepts (`renderFrame`); defines `IRendererBackend` contract; no graphics API knowledge |
| `SonnetRendererVulkan` | Static library | `sonnet/renderer/VulkanBackendFactory.hpp` | **Backend**: Vulkan implementation of `IRendererBackend`; isolated graphics API details |
| `SonnetApp` | Executable | — | Entry point; wires modules together via SDL3 callbacks |

---

## Dependency Matrix

```
Target                 PUBLIC deps          PRIVATE deps
──────────────────────────────────────────────────────────────────────────────────
SonnetLogging          (none)               spdlog::spdlog
SonnetWindow           (none)               SDL3::SDL3, SonnetLogging
SonnetRenderer         SonnetWindow         SonnetLogging
SonnetRendererVulkan   SonnetRenderer       Vulkan::Vulkan, VulkanHpp*, SonnetLogging
SonnetApp              (none)               SonnetRenderer, SonnetRendererVulkan,
                                            SonnetWindow, SonnetLogging, SDL3::SDL3
──────────────────────────────────────────────────────────────────────────────────
* VulkanHpp: added via target_include_directories(SonnetRendererVulkan SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR})
```

**Dependency chain** (no cycles):

```
SonnetApp
 ├── SonnetRendererVulkan
 │    ├── SonnetRenderer  ← PUBLIC (IRenderer, IRendererBackend visible transitively)
 │    │    ├── SonnetWindow  ← PUBLIC (IWindow visible transitively)
 │    │    │    ├── SDL3::SDL3  ← PRIVATE
 │    │    │    └── SonnetLogging  ← PRIVATE
 │    │    │         └── spdlog::spdlog  ← PRIVATE
 │    │    └── SonnetLogging  ← PRIVATE
 │    ├── Vulkan::Vulkan  ← PRIVATE
 │    ├── VulkanHpp headers  ← PRIVATE (SYSTEM include)
 │    └── SonnetLogging  ← PRIVATE
 ├── SonnetRenderer (explicit, for RendererFactory)
 ├── SonnetWindow (explicit, for SDL_AppEvent window access)
 ├── SonnetLogging (explicit, for SDL_AppInit log init)
 └── SDL3::SDL3 (explicit, for SDL_MAIN_USE_CALLBACKS and SDL_Event)
```

---

## Rules

### R1 — No Circular Dependencies

No target may directly or transitively depend on a target that depends on it. The dependency
graph must be a strict DAG. Violations are a **hard build error**.

### R2 — No Private Dependency Leakage

A module's PUBLIC headers MUST NOT include headers from any PRIVATE dependency. Specifically:
- `IWindow.hpp` MUST NOT include `<SDL3/SDL.h>`, any SDL3 header, `<vulkan/vulkan.h>`, or any
  other graphics API header. Its public surface uses only `<cstdint>`, `<string>`, `<utility>`,
  and `<vector>`.
- `IRenderer.hpp` and `IRendererBackend.hpp` MUST NOT include `<vulkan/vulkan.h>`,
  `<vulkan/vulkan.hpp>`, or any Vulkan-Hpp header.
- `VulkanBackendFactory.hpp` MUST NOT include `<vulkan/vulkan.h>` or any Vulkan header; it only
  needs `<memory>` and a forward declaration of `IRendererBackend`.
- `Logger.hpp` MUST NOT include `<spdlog/spdlog.h>` in a way that exposes spdlog types in the
  public interface (macros are permitted; type exposure is not).

Enforcement: CI build with a consumer target that links only to the public interface and compiles
successfully without the private dependencies on the include path.

### R3 — One-Way Dependency Direction

Dependencies must flow in one direction only:

```
SonnetLogging ← SonnetWindow ← SonnetRenderer ← SonnetRendererVulkan ← SonnetApp
```

`SonnetLogging` must not depend on any renderer or window module. `SonnetWindow` must not
depend on any renderer module. `SonnetRenderer` must not depend on `SonnetRendererVulkan` or
`SonnetApp`. `SonnetRendererVulkan` must not depend on `SonnetApp`.

### R4 — External Dependencies via FetchContent or find_package Only

All third-party dependencies must be declared in `cmake/FetchDependencies.cmake`. Ad-hoc
`find_library` or `include_directories` in module `CMakeLists.txt` files are forbidden, except
for the `VulkanHpp` SYSTEM include which is explicitly documented here as an exception to
standard target-based linking (see Dependency Matrix note).

### R5 — Compiler Options Applied per Target

`sonnet_set_compile_options(TARGET)` from `cmake/CompilerOptions.cmake` MUST be called for
every project target (`SonnetLogging`, `SonnetWindow`, `SonnetRenderer`, `SonnetRendererVulkan`,
`SonnetApp`, and all test executables). It MUST NOT be called for FetchContent targets or their
aliases.

### R6 — SYSTEM Includes for External Headers

External library headers included via `target_include_directories` MUST use the `SYSTEM`
keyword to suppress compiler warnings originating from those headers. This applies to VulkanHpp.

### R7 — Renderer Frontend/Backend Boundary

The renderer is split into a **frontend** (`SonnetRenderer`) and one or more **backends**
(`SonnetRendererVulkan`, and any future Metal/DirectX/WebGPU backends). The following
constraints enforce the boundary:

1. **Frontend contains no graphics API code.** `SonnetRenderer` MUST NOT link to or include
   any graphics library (`Vulkan::Vulkan`, `VulkanHpp`, Metal, DirectX, etc.) in any form —
   neither as a PUBLIC nor PRIVATE CMake dependency.

2. **Frontend communicates with the backend exclusively through `IRendererBackend`.** The
   concrete `Renderer` class MUST only call `IRendererBackend` virtual methods. It MUST NOT
   `dynamic_cast`, `static_cast`, or otherwise access backend implementation types.

3. **Each backend is self-contained.** A backend module (`SonnetRendererVulkan`, etc.) MUST
   satisfy the full `IRendererBackend` contract independently. No shared state or coupling
   between backends is permitted.

4. **`IRendererBackend` is the sole extension point.** Adding a new graphics backend requires
   creating a new CMake module that depends on `SonnetRenderer` (for the interface) and
   implements `IRendererBackend`. No changes to `SonnetRenderer` or `SonnetApp` source code
   should be required.

5. **Backend modules MUST NOT be PUBLIC dependencies of `SonnetRenderer`.** The frontend does
   not know which backend is in use at compile time. The app (`SonnetApp`) links to both the
   frontend and the chosen backend explicitly.

---

## CMake Options Exposed by the Project

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `SONNET_LOG_LEVEL` | STRING | `info` | Compile-time log level; valid values: `trace debug info warn error critical off` |
| `SONNET_ENABLE_VALIDATION_LAYERS` | BOOL | `ON` (Debug), `OFF` (Release) | Enable Vulkan `VK_LAYER_KHRONOS_validation` |
| `SONNET_BUILD_TESTS` | BOOL | `ON` | Build and register Catch2 tests |

---

## Minimum Versions

| Tool / Library | Version |
|---|---|
| CMake | 4.2.1 |
| SDL | 3.4.4 |
| Vulkan SDK | 1.3+ (system install) |
| Vulkan-Hpp | v1.4.334 |
| spdlog | v1.17.0 |
| Catch2 | v3.14.0 |
| C++ Standard | C++23 |
