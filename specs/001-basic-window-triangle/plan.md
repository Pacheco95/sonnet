# Implementation Plan: Basic Window with Triangle Rendering

**Branch**: `001-basic-window-triangle` | **Date**: 2026-05-01 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/001-basic-window-triangle/spec.md`

## Summary

Build the first runnable milestone of the Sonnet game engine: a native desktop window that renders a
single solid-colored triangle using Vulkan, driven by SDL3's callback-based main loop. The
implementation establishes the foundational CMake module architecture — `SonnetWindow`,
`SonnetRenderer` (frontend), `SonnetRendererVulkan` (backend), and `SonnetLogging` as four
independent static library modules — that all future engine features will extend. SDL3 and Vulkan
remain private implementation details; the public API is expressed through abstract C++ interfaces
and factory functions.

## Technical Context

**Language/Version**: C++23 (`-std=c++23`, enforced via `CMAKE_CXX_STANDARD_REQUIRED ON`)  
**Primary Dependencies**:
- SDL 3.4.4 — window management, event loop (PRIVATE to `SonnetWindow`)
- Vulkan SDK (system install) — graphics backend; provides `Vulkan::Vulkan`, `Vulkan::Headers`, `Vulkan::glslc` (PRIVATE to `SonnetRendererVulkan`)
- Vulkan-Hpp v1.4.334 — header-only C++ bindings for Vulkan (PRIVATE to `SonnetRendererVulkan`)
- spdlog v1.17.0 — structured logging with source location (PRIVATE to `SonnetLogging`)
- Catch2 v3.14.0 — unit and integration testing framework

**Build System**: CMake 4.2.1 minimum; SDL3 / spdlog / Catch2 / Vulkan-Hpp via `FetchContent`; Vulkan SDK via `find_package(Vulkan REQUIRED)`  
**Storage**: N/A  
**Testing**: Catch2 v3.14.0 (`Catch2::Catch2WithMain`); `catch_discover_tests` for CTest integration  
**Target Platform**: Desktop (Linux, macOS, Windows); Vulkan 1.0-capable GPU and installed Vulkan SDK required  
**Project Type**: Game engine executable (first milestone)  
**Performance Goals**: ≥60 FPS stable for ≥60 seconds (SC-002)  
**Constraints**: ≤100 MB RAM, ≤100 MB VRAM (SC-003); ≤5% CPU steady-state (SC-004, 3 s warm-up allowed); clean exit ≤1 s (SC-001); no memory leaks (SC-005)  
**Scale/Scope**: Single window (800×600 default), single static triangle (hardcoded in vertex shader), no user input beyond window close

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Modularity | ✅ PASS | Four CMake static library modules (`SonnetWindow`, `SonnetRenderer`, `SonnetRendererVulkan`, `SonnetLogging`); SDL3 and Vulkan are always PRIVATE; `SonnetWindow` and `SonnetRenderer` public headers have no graphics API dependencies; one-way dependency chain with no cycles |
| II. Usability | ✅ PASS | FR-008 mandates human-readable error on init failure; spdlog emits file/line on every log line; `README.md` documents build, run, and test |
| III. Stability | ✅ PASS | All dependency versions pinned (SDL 3.4.4, Vulkan-Hpp v1.4.334, spdlog v1.17.0, Catch2 v3.14.0); FetchContent locks exact `GIT_TAG` values; CMake minimum version declared |
| IV. Predictability | ✅ PASS | Vulkan validation layers enabled in debug builds via `VK_LAYER_KHRONOS_validation`; `-Werror` applied to all project targets; SDL3 callback API provides deterministic lifecycle (`AppInit → AppIterate → AppEvent → AppQuit`) |
| V. Testability | ✅ PASS | `IWindow` and `IRenderer` abstract classes enable mock implementations without SDL3 or Vulkan; Catch2 unit tests per module; integration test exercises full lifecycle without a real display |
| VI. Portability | ✅ PASS | SDL3 and Vulkan are cross-platform; CMake provides cross-platform build scripts; Vulkan SDK installation prerequisite documented in `README.md` and `quickstart.md` |
| VII. Internationalization | ⚠️ EXCEPTION | Window title `"Sonnet Engine"` and FR-008 error message are hardcoded strings. See Complexity Tracking. |

## Project Structure

### Documentation (this feature)

```text
specs/001-basic-window-triangle/
├── plan.md              # This file
├── research.md          # Phase 0 findings
├── data-model.md        # Phase 1 entity and interface definitions
├── quickstart.md        # Phase 1 build/run/test guide
├── contracts/
│   ├── window-api.md    # IWindow C++ interface contract
│   ├── renderer-api.md  # IRenderer C++ interface contract
│   └── cmake-modules.md # CMake module dependency contracts
└── tasks.md             # Phase 2 output (/speckit-tasks — not created here)
```

### Source Code (repository root)

```text
CMakeLists.txt                            # Root: project, options, find_package(Vulkan), subdirs
README.md                                 # Overview + build/run/test snippets [→ quickstart.md]
cmake/
  FetchDependencies.cmake                 # All FetchContent declarations [→ research.md §SDL3/spdlog/Catch2/Vulkan-Hpp]
  CompilerOptions.cmake                   # sonnet_set_compile_options() [→ research.md §Warnings-as-Errors]
  ShaderCompilation.cmake                 # compile_shader() helper [→ research.md §SPIR-V Shader Compilation]
src/
  app/
    CMakeLists.txt                        # SonnetApp executable
    main.cpp                              # SDL3 callbacks [→ research.md §SDL3 Callback API; data-model.md §AppState]
  window/
    CMakeLists.txt                        # SonnetWindow target [→ cmake-modules.md §Dependency Matrix]
    include/sonnet/window/
      IWindow.hpp                         # PUBLIC interface [→ contracts/window-api.md §IWindow Interface]
      WindowFactory.hpp                   # PUBLIC factory [→ contracts/window-api.md §WindowFactory Interface]
    src/
      SDLWindow.hpp                       # PRIVATE: SDL3-backed IWindow declaration
      SDLWindow.cpp                       # PRIVATE: SDL3 impl [→ contracts/window-api.md §Invariants; research.md §SDL3 Vulkan Integration]
  renderer/
    CMakeLists.txt                        # SonnetRenderer (frontend) [→ cmake-modules.md §Dependency Matrix]
    include/sonnet/renderer/
      IRenderer.hpp                       # PUBLIC frontend interface [→ contracts/renderer-api.md §IRenderer Interface]
      IRendererBackend.hpp                # PUBLIC backend contract [→ contracts/renderer-api.md §IRendererBackend Interface]
      RendererFactory.hpp                 # PUBLIC factory [→ contracts/renderer-api.md §RendererFactory Interface]
    src/
      Renderer.hpp                        # PRIVATE: concrete frontend declaration
      Renderer.cpp                        # PRIVATE: beginFrame→drawPrimitive→endFrame delegation
  renderer_vulkan/
    CMakeLists.txt                        # SonnetRendererVulkan (backend) [→ cmake-modules.md §Dependency Matrix; research.md §SPIR-V]
    include/sonnet/renderer/
      VulkanBackendFactory.hpp            # PUBLIC factory [→ contracts/renderer-api.md §VulkanBackendFactory]
    src/
      VulkanBackend.hpp                   # PRIVATE: Vulkan IRendererBackend declaration
      VulkanBackend.cpp                   # PRIVATE: Vulkan impl [→ contracts/renderer-api.md §Vulkan Resource Allocation Order; research.md §Vulkan Instance; research.md §VulkanBackend Implementation Notes]
      shaders/
        triangle.vert                     # GLSL vertex [→ research.md §SPIR-V Shader Compilation]
        triangle.frag                     # GLSL fragment [→ research.md §VulkanBackend Implementation Notes]
  logging/
    CMakeLists.txt                        # SonnetLogging target [→ cmake-modules.md §Dependency Matrix]
    include/sonnet/logging/
      Logger.hpp                          # PUBLIC spdlog wrapper [→ research.md §spdlog Configuration]
    src/
      Logger.cpp                          # PRIVATE: stdout+stderr sinks [→ research.md §spdlog Configuration]
tests/
  CMakeLists.txt
  unit/
    CMakeLists.txt
    window/
      WindowInterfaceTest.cpp             # [→ plan.md §Test Scenarios: Unit SonnetWindow]
    renderer/
      RendererInterfaceTest.cpp           # [→ plan.md §Test Scenarios: Unit SonnetRenderer]
    logging/
      LoggerTest.cpp                      # [→ plan.md §Test Scenarios: Unit SonnetLogging]
  integration/
    CMakeLists.txt
    AppLifecycleTest.cpp                  # [→ plan.md §Test Scenarios: Integration]
vendor/                                   # Reserved; empty (all deps via FetchContent)
```

**Structure Decision**: Single-project layout; modules are static libraries to simplify distribution.
`vendor/` is reserved for any dependency that cannot be acquired via FetchContent in future features.

**CMake dependency chain** (one-way, no cycles):

```
SonnetApp ──PRIVATE──► SonnetRenderer        ──PUBLIC──►  SonnetWindow ──PRIVATE──► SDL3::SDL3
           │            (frontend)            └──PRIVATE──► SonnetLogging
           │
           ├──PRIVATE──► SonnetRendererVulkan ──PUBLIC──►  SonnetRenderer (transitively → SonnetWindow)
           │              (backend)            └──PRIVATE──► Vulkan::Vulkan
           │                                  └──PRIVATE──► VulkanHpp (SYSTEM include)
           │                                  └──PRIVATE──► SonnetLogging
           │
           ├──PRIVATE──► SonnetWindow
           ├──PRIVATE──► SonnetLogging
           └──PRIVATE──► SDL3::SDL3 (for SDL_MAIN_USE_CALLBACKS and SDL_Event in callbacks)

SonnetLogging ──PRIVATE──► spdlog::spdlog
```

## Implementation Order

Build phases in dependency order. Each phase produces a working, compilable state.

### Phase 1 — CMake Infrastructure

Files: `CMakeLists.txt`, `cmake/FetchDependencies.cmake`, `cmake/CompilerOptions.cmake`, `cmake/ShaderCompilation.cmake`

1. Declare project with `cmake_minimum_required(VERSION 4.2.1)`, `project(Sonnet CXX)`, `set(CMAKE_CXX_STANDARD 23)`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`.
2. Write `cmake/FetchDependencies.cmake` with all four FetchContent declarations (SDL3, spdlog, Catch2, VulkanHpp). Call `FetchContent_MakeAvailable` for SDL3, spdlog, Catch2; use `FetchContent_Populate` for VulkanHpp (EXCLUDE_FROM_ALL). [→ research.md §SDL3 Callback API; research.md §Vulkan-Hpp C++ Bindings; research.md §spdlog Configuration; research.md §Catch2 Testing Setup]
3. Write `cmake/CompilerOptions.cmake` with `sonnet_set_compile_options()`. [→ research.md §Warnings-as-Errors]
4. Write `cmake/ShaderCompilation.cmake` with `compile_shader()`. [→ research.md §SPIR-V Shader Compilation]
5. Add `find_package(Vulkan REQUIRED)` and `SONNET_LOG_LEVEL` / `SONNET_ENABLE_VALIDATION_LAYERS` / `SONNET_BUILD_TESTS` options to root `CMakeLists.txt`. [→ contracts/cmake-modules.md §CMake Options; research.md §spdlog Configuration]
6. Add `add_subdirectory` calls for `src/logging`, `src/window`, `src/renderer`, `src/renderer_vulkan`, `src/app`, and conditionally `tests`.

**Gate**: `cmake -S . -B build` completes without error.

---

### Phase 2 — SonnetLogging

Files: `src/logging/CMakeLists.txt`, `src/logging/include/sonnet/logging/Logger.hpp`, `src/logging/src/Logger.cpp`

1. Declare `SonnetLogging` static library; call `sonnet_set_compile_options`. [→ contracts/cmake-modules.md §Dependency Matrix]
2. Add `target_include_directories(SonnetLogging PUBLIC include)`.
3. Link `spdlog::spdlog` PRIVATE.
4. Apply `SPDLOG_ACTIVE_LEVEL` compile definition from `SONNET_LOG_LEVEL`. [→ research.md §spdlog Configuration]
5. Implement `Logger.hpp`: `SONNET_LOG_*` macro wrappers over `SPDLOG_*` macros; no spdlog types in public interface. [→ data-model.md §Interfaces — note: ILogger not an entity; Logger.hpp only contains macros]
6. Implement `Logger.cpp`: `init()` function creating multi-sink logger (stdout for INFO and below, stderr for WARN and above), pattern `[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v`. [→ research.md §spdlog Configuration]

**Gate**: `cmake --build build --target SonnetLogging` succeeds.

---

### Phase 3 — SonnetWindow

Files: `src/window/CMakeLists.txt`, `src/window/include/sonnet/window/IWindow.hpp`, `src/window/include/sonnet/window/WindowFactory.hpp`, `src/window/src/SDLWindow.hpp`, `src/window/src/SDLWindow.cpp`

1. Declare `SonnetWindow` static library; call `sonnet_set_compile_options`. [→ contracts/cmake-modules.md §Dependency Matrix]
2. Link `SDL3::SDL3` PRIVATE, `SonnetLogging` PRIVATE.
3. Implement `IWindow.hpp` with `uint64_t` surface handles; no SDL3 or Vulkan headers. [→ data-model.md §IWindow; contracts/window-api.md §IWindow Interface]
4. Implement `WindowFactory.hpp` declaring `createDefaultWindow()`. [→ contracts/window-api.md §WindowFactory Interface]
5. Implement `SDLWindow`: `SDL_CreateWindow` with `SDL_WINDOW_VULKAN` flag and 800×600 default; `SDL_Vulkan_GetInstanceExtensions` for extension list; `SDL_Vulkan_CreateSurface` with `reinterpret_cast` on both handles. [→ contracts/window-api.md §Invariants; research.md §SDL3 Vulkan Integration; research.md §VulkanBackend Implementation Notes §SDL_WINDOW_VULKAN]

**Gate**: `cmake --build build --target SonnetWindow` succeeds; no SDL3 or Vulkan types in installed headers.

---

### Phase 4 — SonnetRenderer (Frontend)

Files: `src/renderer/CMakeLists.txt`, `src/renderer/include/sonnet/renderer/IRenderer.hpp`, `src/renderer/include/sonnet/renderer/IRendererBackend.hpp`, `src/renderer/include/sonnet/renderer/RendererFactory.hpp`, `src/renderer/src/Renderer.hpp`, `src/renderer/src/Renderer.cpp`

1. Declare `SonnetRenderer` static library; call `sonnet_set_compile_options`. [→ contracts/cmake-modules.md §Dependency Matrix; contracts/cmake-modules.md §R7]
2. Link `SonnetWindow` PUBLIC, `SonnetLogging` PRIVATE. **Do not link Vulkan**.
3. Implement `IRenderer.hpp` and `IRendererBackend.hpp` — only forward declarations and `uint64_t` handles visible. [→ data-model.md §IRenderer; data-model.md §IRendererBackend; contracts/renderer-api.md §Frontend API]
4. Implement `RendererFactory.hpp` taking `std::unique_ptr<IRendererBackend>`. [→ contracts/renderer-api.md §RendererFactory Interface]
5. Implement concrete `Renderer`: `init()` stores backend + calls `backend->init(window)`; `renderFrame()` calls `beginFrame → drawPrimitive → endFrame`; `shutdown()` calls `backend->shutdown()`. [→ contracts/renderer-api.md §Frontend Invariants]

**Gate**: `cmake --build build --target SonnetRenderer` succeeds; `nm` or `objdump` on the archive shows no Vulkan symbols.

---

### Phase 5 — SonnetRendererVulkan (Backend)

Files: `src/renderer_vulkan/CMakeLists.txt`, `src/renderer_vulkan/include/sonnet/renderer/VulkanBackendFactory.hpp`, `src/renderer_vulkan/src/VulkanBackend.hpp`, `src/renderer_vulkan/src/VulkanBackend.cpp`, `src/renderer_vulkan/src/shaders/triangle.vert`, `src/renderer_vulkan/src/shaders/triangle.frag`

1. Declare `SonnetRendererVulkan` static library; call `sonnet_set_compile_options`. [→ contracts/cmake-modules.md §Dependency Matrix]
2. Link `SonnetRenderer` PUBLIC; `Vulkan::Vulkan` PRIVATE; `SonnetLogging` PRIVATE.
3. Add `target_include_directories(SonnetRendererVulkan SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR})`. [→ research.md §Vulkan-Hpp C++ Bindings]
4. Write GLSL shaders in `src/shaders/`; call `compile_shader()` for each. [→ research.md §SPIR-V Shader Compilation]
5. Implement `VulkanBackend::init()` in resource-allocation order: instance → debug messenger → surface (via `IWindow::createSurface`) → physical device → logical device → swapchain → image views → render pass → pipeline → framebuffers → command pool → command buffers → sync objects. [→ contracts/renderer-api.md §Vulkan Resource Allocation Order; research.md §Vulkan Instance and Validation Layers; research.md §VulkanBackend Implementation Notes]
6. Implement `beginFrame()` / `drawPrimitive()` / `endFrame()` for the swapchain present loop. [→ contracts/renderer-api.md §Backend Invariants; research.md §VulkanBackend Implementation Notes]
7. Implement `shutdown()` destroying resources in reverse order.
8. Implement `VulkanBackendFactory.hpp` returning `std::unique_ptr<IRendererBackend>`. [→ contracts/renderer-api.md §VulkanBackendFactory]

**Gate**: `cmake --build build --target SonnetRendererVulkan` succeeds (shaders compile to SPIR-V without errors).

---

### Phase 6 — SonnetApp

Files: `src/app/CMakeLists.txt`, `src/app/main.cpp`

1. Declare `Sonnet` executable; call `sonnet_set_compile_options`. [→ contracts/cmake-modules.md §Dependency Matrix]
2. Link all four modules PRIVATE + `SDL3::SDL3` PRIVATE.
3. Define `SDL_MAIN_USE_CALLBACKS` before `#include <SDL3/SDL_main.h>`. [→ research.md §SDL3 Callback API]
4. Implement `SDL_AppInit`: call `sonnet::logging::init()` first, then `WindowFactory::createDefaultWindow()`, then `createRenderer(createVulkanBackend())`, then `renderer->init(*window)`. Heap-allocate `AppState`; write to `*appstate`. [→ data-model.md §AppState; research.md §VulkanBackend Implementation Notes §Logger Init]
5. Implement `SDL_AppIterate`: call `renderer->renderFrame()`; return `SDL_APP_CONTINUE`.
6. Implement `SDL_AppEvent`: handle `SDL_EVENT_QUIT` → return `SDL_APP_SUCCESS`; forward window-resize events for swapchain rebuild.
7. Implement `SDL_AppQuit`: shutdown renderer then window; `delete state`. [→ data-model.md §AppState]

**Gate**: `./build/src/app/Sonnet` opens a window showing the RGB triangle; closes cleanly on OS close. [→ quickstart.md §Run]

---

### Phase 7 — Tests

Files: `tests/CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `tests/integration/CMakeLists.txt`, and the four test source files

1. Enable `SONNET_BUILD_TESTS` guard in root `CMakeLists.txt`; add `enable_testing()` and `add_subdirectory(tests)`.
2. Append Catch2 extras to `CMAKE_MODULE_PATH`; include `Catch`. [→ research.md §Catch2 Testing Setup]
3. Write `WindowInterfaceTest.cpp`, `RendererInterfaceTest.cpp`, `LoggerTest.cpp`, `AppLifecycleTest.cpp` per the Test Scenarios section below.
4. Register each executable with `catch_discover_tests()`.

**Gate**: `cd build && ctest --output-on-failure` — all tests pass without a display or GPU. [→ quickstart.md §Test]

---

### Phase 8 — README

File: `README.md`

Write per the content outlined in `quickstart.md`. Must include: project description, prerequisites table, configure/build/run/test code blocks, brief module overview. [→ quickstart.md]

**Gate**: A developer following only README.md steps can build and run the engine from a clean clone.

---

## Test Scenarios

### Unit: SonnetWindow (`tests/unit/window/WindowInterfaceTest.cpp`)

| Test | Description | Assertion |
|------|-------------|-----------|
| `IWindow_InitSucceeds_ReturnsTrue` | Mock `IWindow` with `init()` returning `true` | `REQUIRE(window->init() == true)` |
| `IWindow_ShouldClose_FalseAfterInit` | After `init()`, `shouldClose()` is `false` | `REQUIRE_FALSE(window->shouldClose())` |
| `IWindow_ShouldClose_TrueAfterClose` | Simulate close event on mock; `shouldClose()` becomes `true` | `REQUIRE(window->shouldClose())` |
| `IWindow_GetSize_Returns800x600` | Default window returns 800×600 | `auto [w, h] = window->getSize(); REQUIRE(w == 800); REQUIRE(h == 600)` |
| `IWindow_Shutdown_IsIdempotent` | Calling `shutdown()` twice on mock does not throw | `REQUIRE_NOTHROW(...)` |
| `IWindow_PublicHeader_NoGraphicsIncludes` | Compile-time: `IWindow.hpp` included alone without SDL3 or Vulkan headers | Static assertion via separate translation unit |

**Mock implementation**: `MockWindow` in the test file implementing `IWindow`; all state held in member variables.

---

### Unit: SonnetRenderer (`tests/unit/renderer/RendererInterfaceTest.cpp`)

| Test | Description | Assertion |
|------|-------------|-----------|
| `IRenderer_InitWithMockBackend_ReturnsTrue` | Inject `MockBackend`; call `renderer->init(mockWindow)` | `REQUIRE(renderer->init(mockWindow) == true)` |
| `IRenderer_RenderFrame_CallsBackendSequence` | After `init`, call `renderFrame()`; verify `beginFrame`, `drawPrimitive`, `endFrame` called in order | Order flags on `MockBackend` set in sequence |
| `IRenderer_Shutdown_DelegatestoBackend` | After `init`, call `shutdown()`; backend `shutdown()` was called | `REQUIRE(mockBackend.shutdownCalled)` |
| `IRenderer_PublicHeaders_NoVulkanIncludes` | `IRenderer.hpp` and `IRendererBackend.hpp` included alone without Vulkan headers | Static assertion via separate translation unit |

**Mock implementations**: `MockBackend` (implements `IRendererBackend`) and `MockWindow` (reused from window tests) defined in the test file.

---

### Unit: SonnetLogging (`tests/unit/logging/LoggerTest.cpp`)

| Test | Description | Assertion |
|------|-------------|-----------|
| `Logger_Init_DoesNotThrow` | `sonnet::logging::init()` completes without exception | `REQUIRE_NOTHROW(sonnet::logging::init())` |
| `Logger_MacroInfo_WritesToStdout` | `SONNET_LOG_INFO("test")` produces output on stdout | Redirect stdout; verify non-empty |
| `Logger_MacroError_WritesToStderr` | `SONNET_LOG_ERROR("test")` produces output on stderr | Redirect stderr; verify non-empty |
| `Logger_DoubleInit_IsIdempotent` | Calling `init()` twice does not crash or duplicate sinks | `REQUIRE_NOTHROW(...)` called twice |

---

### Integration: AppLifecycle (`tests/integration/AppLifecycleTest.cpp`)

| Test | Description | Assertion |
|------|-------------|-----------|
| `AppLifecycle_InitShutdown_NoLeaks` | Construct `MockWindow` + `MockBackend`, run full init/render/shutdown sequence | No exception; all `shutdown()` methods called exactly once |
| `AppLifecycle_RendererInit_InjectsWindow` | `renderer->init(window)` passes the window reference to the backend | `mockBackend.initWindowPtr == &window` |
| `AppLifecycle_RenderLoop_FrameCountIncrements` | Call `renderFrame()` N times; verify N `beginFrame` invocations | `REQUIRE(mockBackend.beginFrameCount == N)` |
| `AppLifecycle_WindowCloseRequest_PropagatesCorrectly` | Set mock window `shouldClose = true`; verify app loop would return `SDL_APP_SUCCESS` | Tested via logic extracted from `SDL_AppIterate` |

All integration tests run without a display, GPU, or SDL3 initialization by using mock implementations.

---

## CMake File Outlines

### `CMakeLists.txt` (root)

```cmake
cmake_minimum_required(VERSION 4.2.1)
project(Sonnet CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
set(SONNET_LOG_LEVEL "info" CACHE STRING "...")
set_property(CACHE SONNET_LOG_LEVEL PROPERTY STRINGS trace debug info warn error critical off)
option(SONNET_ENABLE_VALIDATION_LAYERS ...)  # default ON in Debug, OFF in Release
option(SONNET_BUILD_TESTS "Build tests" ON)

include(cmake/FetchDependencies.cmake)
include(cmake/CompilerOptions.cmake)
include(cmake/ShaderCompilation.cmake)

find_package(Vulkan REQUIRED)

add_subdirectory(src/logging)
add_subdirectory(src/window)
add_subdirectory(src/renderer)
add_subdirectory(src/renderer_vulkan)
add_subdirectory(src/app)

if(SONNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

---

### `src/logging/CMakeLists.txt`

```cmake
add_library(SonnetLogging STATIC src/Logger.cpp)
target_include_directories(SonnetLogging PUBLIC include)
target_link_libraries(SonnetLogging PRIVATE spdlog::spdlog)
string(TOUPPER ${SONNET_LOG_LEVEL} _LOG_LEVEL_UPPER)
target_compile_definitions(SonnetLogging PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${_LOG_LEVEL_UPPER})
sonnet_set_compile_options(SonnetLogging)
```

---

### `src/window/CMakeLists.txt`

```cmake
add_library(SonnetWindow STATIC src/SDLWindow.cpp)
target_include_directories(SonnetWindow PUBLIC include)
target_link_libraries(SonnetWindow PRIVATE SDL3::SDL3 SonnetLogging)
sonnet_set_compile_options(SonnetWindow)
```

---

### `src/renderer/CMakeLists.txt`

```cmake
add_library(SonnetRenderer STATIC src/Renderer.cpp)
target_include_directories(SonnetRenderer PUBLIC include)
target_link_libraries(SonnetRenderer PUBLIC SonnetWindow PRIVATE SonnetLogging)
sonnet_set_compile_options(SonnetRenderer)
# No Vulkan linkage — enforces R7.1
```

---

### `src/renderer_vulkan/CMakeLists.txt`

```cmake
add_library(SonnetRendererVulkan STATIC src/VulkanBackend.cpp)
target_include_directories(SonnetRendererVulkan PUBLIC include)
target_include_directories(SonnetRendererVulkan SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR})
target_link_libraries(SonnetRendererVulkan PUBLIC SonnetRenderer PRIVATE Vulkan::Vulkan SonnetLogging)
sonnet_set_compile_options(SonnetRendererVulkan)

compile_shader(SonnetRendererVulkan src/shaders/triangle.vert)
compile_shader(SonnetRendererVulkan src/shaders/triangle.frag)

if(SONNET_ENABLE_VALIDATION_LAYERS)
    target_compile_definitions(SonnetRendererVulkan PRIVATE SONNET_VALIDATION_LAYERS_ENABLED)
endif()
```

---

### `src/app/CMakeLists.txt`

```cmake
add_executable(Sonnet src/main.cpp)
target_link_libraries(Sonnet PRIVATE
    SonnetRenderer SonnetRendererVulkan SonnetWindow SonnetLogging SDL3::SDL3)
sonnet_set_compile_options(Sonnet)
```

---

### `tests/CMakeLists.txt`

```cmake
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
add_subdirectory(unit)
add_subdirectory(integration)
```

---

### `tests/unit/CMakeLists.txt`

```cmake
add_executable(SonnetWindowTests    window/WindowInterfaceTest.cpp)
add_executable(SonnetRendererTests  renderer/RendererInterfaceTest.cpp)
add_executable(SonnetLoggingTests   logging/LoggerTest.cpp)

foreach(target SonnetWindowTests SonnetRendererTests SonnetLoggingTests)
    sonnet_set_compile_options(${target})
endforeach()

target_link_libraries(SonnetWindowTests   PRIVATE SonnetWindow   Catch2::Catch2WithMain)
target_link_libraries(SonnetRendererTests PRIVATE SonnetRenderer Catch2::Catch2WithMain)
target_link_libraries(SonnetLoggingTests  PRIVATE SonnetLogging  Catch2::Catch2WithMain)

catch_discover_tests(SonnetWindowTests)
catch_discover_tests(SonnetRendererTests)
catch_discover_tests(SonnetLoggingTests)
```

---

### `tests/integration/CMakeLists.txt`

```cmake
add_executable(AppLifecycleTests AppLifecycleTest.cpp)
target_link_libraries(AppLifecycleTests PRIVATE SonnetRenderer SonnetWindow SonnetLogging Catch2::Catch2WithMain)
sonnet_set_compile_options(AppLifecycleTests)
catch_discover_tests(AppLifecycleTests)
# Note: SonnetRendererVulkan is NOT linked — tests use mock backends only
```

---

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| VII. I18n — window title `"Sonnet Engine"` hardcoded | MVP: 2 developer-facing strings; a locale subsystem adds significant complexity for zero user value at this stage | Will be addressed in a dedicated localization feature once the engine has a meaningful user-visible string surface |
| VII. I18n — FR-008 error message hardcoded | Same rationale | Same |
