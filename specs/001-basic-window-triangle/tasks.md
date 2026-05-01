# Tasks: Basic Window with Triangle Rendering

**Input**: Design documents from `/specs/001-basic-window-triangle/`
**Prerequisites**: plan.md ✓, spec.md ✓, data-model.md ✓, contracts ✓, research.md ✓, quickstart.md ✓

**Organization**: Tasks are grouped by build phase and user story to enable sequential,
independently verifiable delivery. This feature has a single user story (US1 — P1) that covers
the full scope; foundational modules are separated because later modules depend on them.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no shared dependencies on in-progress tasks)
- **[US1]**: Belongs to User Story 1 — Launch Window and See Triangle (P1)

---

## Phase 1: Setup — CMake Infrastructure

**Purpose**: Establish the build system skeleton that all modules will be added to.
All tasks in this phase can proceed immediately. Marked [P] tasks touch separate files.

- [ ] T001 Create directory structure: `cmake/`, `src/logging/`, `src/window/`, `src/renderer/`, `src/renderer_vulkan/`, `src/app/`, `tests/unit/window/`, `tests/unit/renderer/`, `tests/unit/logging/`, `tests/integration/`, `vendor/` (empty) at repository root
- [ ] T002 Write `CMakeLists.txt` (root): `cmake_minimum_required(VERSION 4.2.1)`, `project(Sonnet CXX)`, `CMAKE_CXX_STANDARD 23` + `REQUIRED` + `EXTENSIONS OFF`, `SONNET_LOG_LEVEL` STRING option, `SONNET_ENABLE_VALIDATION_LAYERS` BOOL option (ON for Debug, OFF for Release), `SONNET_BUILD_TESTS` BOOL option, `find_package(Vulkan REQUIRED)`, `include()` for all three cmake helpers, `add_subdirectory()` for all modules, conditional `enable_testing()` + `add_subdirectory(tests)` [→ plan.md §CMake File Outlines §root; contracts/cmake-modules.md §CMake Options]
- [ ] T003 [P] Write `cmake/FetchDependencies.cmake`: FetchContent declarations for SDL3 3.4.4, spdlog v1.17.0, Catch2 v3.14.0, VulkanHpp v1.4.334 (EXCLUDE_FROM_ALL + FetchContent_Populate pattern); call `FetchContent_MakeAvailable` for SDL3/spdlog/Catch2 [→ research.md §SDL3 Callback API; research.md §Vulkan-Hpp C++ Bindings; research.md §spdlog Configuration; research.md §Catch2 Testing Setup]
- [ ] T004 [P] Write `cmake/CompilerOptions.cmake`: `sonnet_set_compile_options(TARGET)` function applying `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) and `/W4 /WX` (MSVC) as PRIVATE options; no flags on FetchContent targets [→ research.md §Warnings-as-Errors]
- [ ] T005 [P] Write `cmake/ShaderCompilation.cmake`: `compile_shader(TARGET SHADER_SOURCE)` function using `Vulkan::glslc`, outputting `.spv` to `${CMAKE_CURRENT_BINARY_DIR}/shaders/`, copying `.spv` next to executable with `POST_BUILD`, adding `.spv` to `target_sources` [→ research.md §SPIR-V Shader Compilation]

**Checkpoint**: `cmake -S . -B build` completes without error (modules have no source yet; subdirs must exist with stub `CMakeLists.txt` files if needed).

---

## Phase 2: Foundational — SonnetLogging

**Purpose**: Logging infrastructure must be complete before any other module because every
module depends on it for error reporting.

**⚠️ CRITICAL**: No other module can be implemented until this phase is complete.

- [ ] T006 Write `src/logging/include/sonnet/logging/Logger.hpp`: `SONNET_LOG_TRACE`, `SONNET_LOG_DEBUG`, `SONNET_LOG_INFO`, `SONNET_LOG_WARN`, `SONNET_LOG_ERROR`, `SONNET_LOG_CRITICAL` macros wrapping `SPDLOG_TRACE`/`SPDLOG_DEBUG` etc.; `sonnet::logging::init()` function declaration; `#pragma once`; do NOT expose `spdlog::` types in the public interface [→ research.md §spdlog Configuration; contracts/cmake-modules.md §R2]
- [ ] T007 Write `src/logging/src/Logger.cpp`: implement `init()` — create `spdlog::stdout_color_sink_mt` for levels ≤ INFO, `spdlog::stderr_color_sink_mt` for levels ≥ WARN, combine into `spdlog::logger` named `"sonnet"`, set as default logger, apply pattern `"[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v"` to both sinks [→ research.md §spdlog Configuration]
- [ ] T008 Write `src/logging/CMakeLists.txt`: `add_library(SonnetLogging STATIC)`, `target_include_directories PUBLIC include`, `target_link_libraries PRIVATE spdlog::spdlog`, `target_compile_definitions PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${_LOG_LEVEL_UPPER}` (derive from `SONNET_LOG_LEVEL`), `sonnet_set_compile_options(SonnetLogging)` [→ plan.md §CMake File Outlines §SonnetLogging]

**Checkpoint**: `cmake --build build --target SonnetLogging` succeeds.

---

## Phase 3: User Story 1 — Launch Window and See Triangle (Priority: P1) 🎯 MVP

**Goal**: A developer runs the engine binary; a 800×600 window titled "Sonnet Engine" opens
showing a single RGB-interpolated triangle on a dark background. The window responds to OS
events and exits cleanly when closed.

**Independent Test**: `./build/src/app/Sonnet` opens a window with a visible colored triangle.
Closing via OS controls exits with code 0 and no error output.

### SonnetWindow Module

- [ ] T009 [P] [US1] Write `src/window/include/sonnet/window/IWindow.hpp`: `namespace sonnet::window`, abstract class `IWindow` with pure virtual `init() → bool`, `shutdown()`, `shouldClose() const → bool`, `getSize() const → std::pair<int,int>`, `getRequiredInstanceExtensions() const → std::vector<std::string>`, `createSurface(uint64_t instanceHandle) const → uint64_t`; includes only `<cstdint>`, `<string>`, `<utility>`, `<vector>`; no SDL3 or Vulkan headers [→ data-model.md §IWindow; contracts/window-api.md §IWindow Interface]
- [ ] T010 [P] [US1] Write `src/window/include/sonnet/window/WindowFactory.hpp`: `namespace sonnet::window`, declare `std::unique_ptr<IWindow> createDefaultWindow()` (returns SDL-backed window at 800×600 titled "Sonnet Engine") [→ contracts/window-api.md §WindowFactory Interface]
- [ ] T011 [US1] Write `src/window/src/SDLWindow.hpp`: PRIVATE header declaring `class SDLWindow : public IWindow` with `SDL_Window* m_window`, `bool m_shouldClose`, implement all `IWindow` methods; include `<SDL3/SDL.h>` [→ contracts/window-api.md §Invariants]
- [ ] T012 [US1] Write `src/window/src/SDLWindow.cpp`: `init()` calls `SDL_Init(SDL_INIT_VIDEO)` then `SDL_CreateWindow("Sonnet Engine", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE)`; log success/failure via `SONNET_LOG_INFO`/`SONNET_LOG_ERROR`; `getRequiredInstanceExtensions()` calls `SDL_Vulkan_GetInstanceExtensions`; `createSurface(uint64_t)` casts to `VkInstance`, calls `SDL_Vulkan_CreateSurface`, casts result back to `uint64_t`; `shutdown()` calls `SDL_DestroyWindow` then `SDL_Quit`; idempotent [→ research.md §SDL3 Vulkan Integration; research.md §VulkanBackend Implementation Notes §SDL_WINDOW_VULKAN; contracts/window-api.md §Invariants]
- [ ] T013 [US1] Write `src/window/CMakeLists.txt`: `add_library(SonnetWindow STATIC)`, add both src files, `target_include_directories PUBLIC include`, `target_link_libraries PRIVATE SDL3::SDL3 SonnetLogging`, `sonnet_set_compile_options(SonnetWindow)` [→ plan.md §CMake File Outlines §SonnetWindow]

**Checkpoint**: `cmake --build build --target SonnetWindow` succeeds; `IWindow.hpp` and `WindowFactory.hpp` contain no SDL3 or Vulkan types.

---

### SonnetRenderer Module (Frontend)

- [ ] T014 [P] [US1] Write `src/renderer/include/sonnet/renderer/IRenderer.hpp`: `namespace sonnet::renderer`, forward declare `sonnet::window::IWindow`, abstract class `IRenderer` with pure virtual `init(IWindow&) → bool`, `shutdown()`, `renderFrame()`; no Vulkan headers [→ data-model.md §IRenderer; contracts/renderer-api.md §Frontend API]
- [ ] T015 [P] [US1] Write `src/renderer/include/sonnet/renderer/IRendererBackend.hpp`: `namespace sonnet::renderer`, forward declare `sonnet::window::IWindow`, abstract class `IRendererBackend` with pure virtual `init(IWindow&) → bool`, `shutdown()`, `beginFrame()`, `drawPrimitive()`, `endFrame()`; no Vulkan headers [→ data-model.md §IRendererBackend; contracts/renderer-api.md §Frontend API]
- [ ] T016 [P] [US1] Write `src/renderer/include/sonnet/renderer/RendererFactory.hpp`: declare `std::unique_ptr<IRenderer> createRenderer(std::unique_ptr<IRendererBackend> backend)` in `namespace sonnet::renderer`; include `<memory>` and forward declarations only [→ contracts/renderer-api.md §RendererFactory Interface]
- [ ] T017 [US1] Write `src/renderer/src/Renderer.hpp`: PRIVATE header declaring `class Renderer : public IRenderer` with `std::unique_ptr<IRendererBackend> m_backend`; implement all `IRenderer` methods [→ contracts/renderer-api.md §Frontend Invariants]
- [ ] T018 [US1] Write `src/renderer/src/Renderer.cpp`: implement `init()` — store backend, call `m_backend->init(window)`, log; `renderFrame()` — call `m_backend->beginFrame()`, `m_backend->drawPrimitive()`, `m_backend->endFrame()` in that exact order; `shutdown()` — call `m_backend->shutdown()`; implement `createRenderer()` factory function [→ contracts/renderer-api.md §Frontend Invariants; contracts/cmake-modules.md §R7.2]
- [ ] T019 [US1] Write `src/renderer/CMakeLists.txt`: `add_library(SonnetRenderer STATIC)`, add both src files, `target_include_directories PUBLIC include`, `target_link_libraries PUBLIC SonnetWindow PRIVATE SonnetLogging`, `sonnet_set_compile_options(SonnetRenderer)`; **no Vulkan linkage** [→ plan.md §CMake File Outlines §SonnetRenderer; contracts/cmake-modules.md §R7.1]

**Checkpoint**: `cmake --build build --target SonnetRenderer` succeeds. Verify with `nm` or `objdump` that the `SonnetRenderer` archive contains no Vulkan symbols.

---

### SonnetRendererVulkan Module (Backend)

- [ ] T020 [P] [US1] Write `src/renderer_vulkan/src/shaders/triangle.vert`: GLSL `#version 450`; hardcode `positions[3]` (NDC: top (0,−0.5), bottom-right (0.5,0.5), bottom-left (−0.5,0.5)) and `colors[3]` (red/green/blue); set `gl_Position` and output `fragColor` via `gl_VertexIndex` [→ research.md §SPIR-V Shader Compilation]
- [ ] T021 [P] [US1] Write `src/renderer_vulkan/src/shaders/triangle.frag`: GLSL `#version 450`; `layout(location=0) in vec3 fragColor`; `layout(location=0) out vec4 outColor`; `outColor = vec4(fragColor, 1.0)` [→ research.md §VulkanBackend Implementation Notes §Fragment Shader]
- [ ] T022 [P] [US1] Write `src/renderer_vulkan/include/sonnet/renderer/VulkanBackendFactory.hpp`: declare `std::unique_ptr<IRendererBackend> createVulkanBackend()` in `namespace sonnet::renderer`; include `<memory>` and forward declaration of `IRendererBackend` only; no Vulkan headers [→ contracts/renderer-api.md §VulkanBackendFactory; contracts/cmake-modules.md §R2]
- [ ] T023 [US1] Write `src/renderer_vulkan/src/VulkanBackend.hpp`: PRIVATE header for `class VulkanBackend : public IRendererBackend`; declare all private members (instance, debug messenger, surface, physical device, logical device, graphics/present queues, swapchain, image views, render pass, pipeline layout, pipeline, framebuffers, command pool, command buffers, sync semaphores and fences, current frame index, swapchain extent); declare all public and private methods [→ contracts/renderer-api.md §Vulkan Resource Allocation Order]
- [ ] T024 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 1 (instance through surface): include `<vulkan/vulkan.hpp>`, implement `init()` up through surface creation: create `vk::Instance` with app info + SDL3 extensions + optional `VK_EXT_debug_utils`; create debug messenger if `SONNET_VALIDATION_LAYERS_ENABLED`; obtain `VkSurfaceKHR` via `window.createSurface(reinterpret_cast<uint64_t>(instance))`; implement `debugCallback` static function routing to `SONNET_LOG_WARN`/`SONNET_LOG_ERROR` [→ research.md §Vulkan Instance and Validation Layers; research.md §VulkanBackend Implementation Notes §SONNET_ENABLE_VALIDATION_LAYERS; contracts/renderer-api.md §Vulkan Resource Allocation Order steps 1–3]
- [ ] T025 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 2 (device selection through swapchain): select physical device (require `VK_QUEUE_GRAPHICS_BIT` + surface present support); select graphics and present queue families (may be same family); create logical device with required queues + `VK_KHR_swapchain` extension; create swapchain preferring `eB8G8R8A8Srgb`/`eSrgbNonlinear` format and `eMailbox` present mode (fallback `eFifo`); clamp swap extent to `window.getSize()`; create 2 image views [→ research.md §VulkanBackend Implementation Notes §Queue Family Selection; §Swapchain Format and Present Mode; contracts/renderer-api.md §Vulkan Resource Allocation Order steps 4–7]
- [ ] T026 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 3 (render pass through pipeline): create render pass with single color attachment (swapchain format, `eColorAttachmentOptimal`, `ePresentSrcKHR`); load SPIR-V from `<exe_dir>/shaders/triangle.vert.spv` and `triangle.frag.spv` using `std::filesystem` relative to executable path; create shader modules; create graphics pipeline (input assembly `eTriangleList`, no vertex input, single viewport/scissor, fill rasterizer, no blending, pipeline layout with no descriptors) [→ research.md §SPIR-V Shader Compilation; research.md §VulkanBackend Implementation Notes §SPIR-V Runtime Path; contracts/renderer-api.md §Vulkan Resource Allocation Order steps 8–9]
- [ ] T027 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 4 (framebuffers, command infrastructure, sync objects): create one framebuffer per swapchain image view; create command pool for graphics queue family; allocate 2 command buffers (one per frame in flight); create 2 image-available semaphores, 2 render-finished semaphores, 2 in-flight fences (signaled at creation) [→ research.md §VulkanBackend Implementation Notes §In-Flight Frames; contracts/renderer-api.md §Vulkan Resource Allocation Order steps 10–11]
- [ ] T028 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 5 (render loop and swapchain rebuild): implement `beginFrame()` — wait on in-flight fence, acquire next image (handle `eErrorOutOfDateKHR`/`eSuboptimalKHR` → call `rebuildSwapchain()`); implement `drawPrimitive()` — record command buffer: begin render pass, bind pipeline, `draw(3,1,0,0)`, end render pass; implement `endFrame()` — submit command buffer, present, advance frame index; implement `rebuildSwapchain()` — destroy framebuffers → image views → swapchain then recreate them [→ research.md §VulkanBackend Implementation Notes §Swapchain Rebuild; contracts/renderer-api.md §Backend Invariants]
- [ ] T029 [US1] Write `src/renderer_vulkan/src/VulkanBackend.cpp` — Part 6 (shutdown and factory): implement `shutdown()` — `device.waitIdle()`, then destroy in reverse allocation order: sync objects → command pool → framebuffers → pipeline → pipeline layout → render pass → image views → swapchain → logical device → surface → debug messenger → instance; track initialized state for idempotency; implement `createVulkanBackend()` factory function [→ contracts/renderer-api.md §Vulkan Resource Allocation Order; data-model.md §Backend Renderer lifecycle]
- [ ] T030 [US1] Write `src/renderer_vulkan/CMakeLists.txt`: `add_library(SonnetRendererVulkan STATIC)`, `target_include_directories PUBLIC include`, `target_include_directories SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR}`, `target_link_libraries PUBLIC SonnetRenderer PRIVATE Vulkan::Vulkan SonnetLogging`, `sonnet_set_compile_options(SonnetRendererVulkan)`, `compile_shader(SonnetRendererVulkan ...)` for both shaders, conditional `target_compile_definitions PRIVATE SONNET_VALIDATION_LAYERS_ENABLED` [→ plan.md §CMake File Outlines §SonnetRendererVulkan; research.md §VulkanBackend Implementation Notes §SONNET_ENABLE_VALIDATION_LAYERS]

**Checkpoint**: `cmake --build build --target SonnetRendererVulkan` succeeds; both `.spv` files appear in `build/src/renderer_vulkan/shaders/`.

---

### SonnetApp Executable

- [ ] T031 [US1] Write `src/app/main.cpp`: define `SDL_MAIN_USE_CALLBACKS` before `#include <SDL3/SDL_main.h>`; define `AppState { std::unique_ptr<IWindow> window; std::unique_ptr<IRenderer> renderer; }` struct; implement `SDL_AppInit` — call `sonnet::logging::init()` first, create window via `WindowFactory::createDefaultWindow()`, call `window->init()` (return `SDL_APP_FAILURE` with logged error on false), create renderer via `createRenderer(createVulkanBackend())`, call `renderer->init(*window)` (return `SDL_APP_FAILURE` on false), heap-allocate `AppState`, write to `*appstate`; implement `SDL_AppIterate` — call `renderer->renderFrame()`, return `SDL_APP_CONTINUE`; implement `SDL_AppEvent` — handle `SDL_EVENT_QUIT` → return `SDL_APP_SUCCESS`; implement `SDL_AppQuit` — shutdown renderer then window, delete AppState [→ research.md §SDL3 Callback API; research.md §Logger Initialization Call Site; data-model.md §AppState]
- [ ] T032 [US1] Write `src/app/CMakeLists.txt`: `add_executable(Sonnet src/main.cpp)`, `target_link_libraries PRIVATE SonnetRenderer SonnetRendererVulkan SonnetWindow SonnetLogging SDL3::SDL3`, `sonnet_set_compile_options(Sonnet)` [→ plan.md §CMake File Outlines §SonnetApp]

**Checkpoint**: `cmake --build build` succeeds end-to-end; `./build/src/app/Sonnet` (or platform equivalent) opens a window with the RGB triangle and exits cleanly on window close.

---

### Tests

- [ ] T033 [P] [US1] Write `tests/unit/window/WindowInterfaceTest.cpp`: `MockWindow` implementing `IWindow` with in-memory state; test cases: `IWindow_InitSucceeds_ReturnsTrue`, `IWindow_ShouldClose_FalseAfterInit`, `IWindow_ShouldClose_TrueAfterClose`, `IWindow_GetSize_Returns800x600`, `IWindow_Shutdown_IsIdempotent` [→ plan.md §Test Scenarios §Unit SonnetWindow]
- [ ] T034 [P] [US1] Write `tests/unit/renderer/RendererInterfaceTest.cpp`: `MockBackend` implementing `IRendererBackend` with call-order tracking flags; `MockWindow` (minimal); test cases: `IRenderer_InitWithMockBackend_ReturnsTrue`, `IRenderer_RenderFrame_CallsBackendSequence`, `IRenderer_Shutdown_DelegatesToBackend` [→ plan.md §Test Scenarios §Unit SonnetRenderer]
- [ ] T035 [P] [US1] Write `tests/unit/logging/LoggerTest.cpp`: test cases: `Logger_Init_DoesNotThrow`, `Logger_MacroInfo_WritesToStdout` (capture stdout), `Logger_MacroError_WritesToStderr` (capture stderr), `Logger_DoubleInit_IsIdempotent` [→ plan.md §Test Scenarios §Unit SonnetLogging]
- [ ] T036 [US1] Write `tests/integration/AppLifecycleTest.cpp`: reuse `MockWindow` and `MockBackend`; test cases: `AppLifecycle_InitShutdown_NoLeaks`, `AppLifecycle_RendererInit_InjectsWindow`, `AppLifecycle_RenderLoop_FrameCountIncrements`, `AppLifecycle_WindowCloseRequest_PropagatesCorrectly` — all headless, no display or GPU required [→ plan.md §Test Scenarios §Integration]
- [ ] T037 [US1] Write `tests/unit/CMakeLists.txt`: three `add_executable` targets (`SonnetWindowTests`, `SonnetRendererTests`, `SonnetLoggingTests`), link each to its module + `Catch2::Catch2WithMain`, call `sonnet_set_compile_options` and `catch_discover_tests` for each [→ plan.md §CMake File Outlines §tests/unit]
- [ ] T038 [US1] Write `tests/integration/CMakeLists.txt`: `add_executable(AppLifecycleTests ...)`, link to `SonnetRenderer SonnetWindow SonnetLogging Catch2::Catch2WithMain` (NOT `SonnetRendererVulkan`), `sonnet_set_compile_options`, `catch_discover_tests` [→ plan.md §CMake File Outlines §tests/integration]
- [ ] T039 [US1] Write `tests/CMakeLists.txt`: append Catch2 extras to `CMAKE_MODULE_PATH`, `include(Catch)`, `add_subdirectory(unit)`, `add_subdirectory(integration)` [→ plan.md §CMake File Outlines §tests]

**Checkpoint**: `cd build && ctest --output-on-failure` — all tests pass without a display server or GPU.

---

## Phase 4: Polish & Cross-Cutting Concerns

- [ ] T040 Write `README.md`: project description, prerequisites table (C++ compiler, CMake 4.2.1+, Vulkan SDK 1.3+, Git, internet), configure/build/run/test code blocks matching `quickstart.md`, brief module overview table [→ quickstart.md; plan.md §Implementation Order §Phase 8]
- [ ] T041 [P] Verify full build produces zero warnings on project targets: `cmake --build build --parallel 2>&1 | grep -E "warning:|error:"` — only FetchContent targets may emit warnings
- [ ] T042 [P] Run `cd build && ctest -V` and confirm all four test executables pass; fix any test failures before marking complete
- [ ] T043 Run `./build/src/app/Sonnet`, observe triangle renders stably for 60 s at ≥60 FPS, RAM ≤100 MB, CPU ≤5% (after 3 s warm-up), clean exit ≤1 s [→ spec.md §Success Criteria SC-001 through SC-005]

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational — SonnetLogging)**: Depends on Phase 1 completion; **BLOCKS** all module work
- **Phase 3 (US1)**: Depends on Phase 2; internal ordering within Phase 3 is:
  1. SonnetWindow (T009–T013) — depends on Phase 2
  2. SonnetRenderer frontend (T014–T019) — depends on Phase 2 + SonnetWindow headers (T009, T010)
  3. SonnetRendererVulkan (T020–T030) — depends on SonnetRenderer (T014–T019)
  4. SonnetApp (T031–T032) — depends on all modules
  5. Tests (T033–T039) — can start once the module under test compiles; mock-based tests do not require VulkanBackend
- **Phase 4 (Polish)**: Depends on Phase 3 completion

### Within Phase 3 Parallel Opportunities

```
After T008 (SonnetLogging complete):

  ┌── T009 (IWindow.hpp)   ┐
  ├── T010 (WindowFactory) ┤ → T011 → T012 → T013 (SonnetWindow done)
  └── T033 (Window tests)  ┘              ↓
                                   T014 (IRenderer.hpp)    ┐
                                   T015 (IRendererBackend) ┤ → T017 → T018 → T019 (SonnetRenderer done)
                                   T016 (RendererFactory)  ┘              ↓
                                   T034 (Renderer tests)      T020 (vert shader)  ┐
                                                              T021 (frag shader)  ├ → T022 → T023 → T024
                                                              T035 (Logger tests) ┘    → T025 → T026 → T027
                                                                                        → T028 → T029 → T030
                                                                                                         ↓
                                                                                                 T031 → T032
                                                                                                   ↓
                                                                                          T036 → T037 → T038 → T039
```

---

## Parallel Example: Phase 3 Module Headers

```
# After SonnetLogging is built (T008 complete):

# These can run concurrently — separate files, no shared in-progress dependencies:
Task T009: "Write src/window/include/sonnet/window/IWindow.hpp"
Task T010: "Write src/window/include/sonnet/window/WindowFactory.hpp"
Task T014: "Write src/renderer/include/sonnet/renderer/IRenderer.hpp"
Task T015: "Write src/renderer/include/sonnet/renderer/IRendererBackend.hpp"
Task T020: "Write src/renderer_vulkan/src/shaders/triangle.vert"
Task T021: "Write src/renderer_vulkan/src/shaders/triangle.frag"
Task T035: "Write tests/unit/logging/LoggerTest.cpp"
```

---

## Implementation Strategy

### MVP (US1 — this entire feature is one story)

1. Complete Phase 1: CMake infra (T001–T005)
2. Complete Phase 2: SonnetLogging (T006–T008)
3. Complete Phase 3, in module order: Window → Renderer frontend → Renderer Vulkan backend → App → Tests
4. **Validate**: `./build/src/app/Sonnet` shows window with triangle; `ctest` passes
5. Complete Phase 4: README + quality gates (T040–T043)

### Incremental Sub-Deliveries Within Phase 3

Each sub-group has its own checkpoint; stop and validate before proceeding:
1. SonnetWindow compiles → T009–T013
2. SonnetRenderer (frontend) compiles without Vulkan → T014–T019
3. Shaders compile to SPIR-V → T020–T021 + T030 (partial)
4. Full backend compiles → T022–T030
5. Engine binary opens triangle window → T031–T032
6. All tests pass headlessly → T033–T039

---

## Notes

- [P] tasks touch separate files and have no dependencies on in-progress (incomplete) tasks
- T024–T029 are split sections of `VulkanBackend.cpp`; complete them in order (each part adds to the same file's implementation)
- Tests T033–T035 are mock-based and do not require SDL3 or Vulkan at runtime — safe to run in CI without a display
- T036 (integration test) also uses mocks — no GPU or display required
- `SonnetRendererVulkan` is NOT linked into any test target; mock backends cover correctness; runtime rendering is validated by T043
- Commit after each phase checkpoint to preserve a clean, compilable state
