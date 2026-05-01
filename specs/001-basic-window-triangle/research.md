# Research: Basic Window with Triangle Rendering

**Phase**: 0 | **Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

---

## SDL3 Callback API

**Decision**: Use `SDL_MAIN_USE_CALLBACKS` to opt into SDL3's lifecycle callback model instead of
the traditional `int main()` entry point.

**Rationale**: The callback API provides a deterministic, SDL-managed event loop that maps cleanly
to the constitution's Predictability principle. It eliminates manual event-loop boilerplate and
handles platform-specific main() quirks (Windows `WinMain`, iOS `UIApplicationMain`, etc.).

**Interface** (defined in `<SDL3/SDL_main.h>`):

```cpp
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

// Called once at startup. Return SDL_APP_FAILURE to abort immediately.
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]);

// Called every frame. Return SDL_APP_CONTINUE to keep running,
// SDL_APP_SUCCESS to exit cleanly (code 0), SDL_APP_FAILURE to exit with error (code 1).
SDL_AppResult SDL_AppIterate(void *appstate);

// Called for each OS event. Same return semantics as SDL_AppIterate.
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);

// Called once on shutdown, always — even if SDL_AppInit returns failure.
// result holds the SDL_AppResult that triggered shutdown.
void SDL_AppQuit(void *appstate, SDL_AppResult result);
```

**Exit code mapping**:
- `SDL_APP_SUCCESS` → process exit code 0 (satisfies FR-006 and SC-001)
- `SDL_APP_FAILURE` → process exit code 1 (satisfies FR-008)

**FetchContent declaration**:

```cmake
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)
# Exposed target: SDL3::SDL3
```

**Alternatives considered**: Traditional `int main()` with `SDL_Init`/`SDL_Quit` manual loop — rejected
because the callback API is SDL3's recommended modern approach and removes the need for a manual
`while (!quit)` event loop.

---

## SDL3 Vulkan Integration

**Decision**: Use `SDL_Vulkan_GetInstanceExtensions` and `SDL_Vulkan_CreateSurface` within the
`SDLWindow` implementation to bridge SDL3 and Vulkan.

**Rationale**: SDL3 abstracts platform-specific Vulkan surface creation
(`VkXlibSurfaceCreateInfoKHR`, `VkWin32SurfaceCreateInfoKHR`, etc.) behind a single portable call.
This isolates all platform-specific surface creation inside `SDLWindow.cpp`.

**Interface** (from `<SDL3/SDL_vulkan.h>`):

```c
// Returns the instance extensions SDL needs; count written to *count.
// Returns NULL on error. The array is owned by SDL; do not free.
const char * const * SDL_Vulkan_GetInstanceExtensions(Uint32 *count);

// Creates a VkSurfaceKHR for the given SDL_Window and VkInstance.
// allocator may be NULL. Returns false on failure.
bool SDL_Vulkan_CreateSurface(SDL_Window        *window,
                               VkInstance         instance,
                               const VkAllocationCallbacks *allocator,
                               VkSurfaceKHR      *surface);
```

**Design consequence**: `IWindow` remains fully framework-agnostic. Rather than exposing
`VkInstance` / `VkSurfaceKHR` in the public interface, `SDLWindow` accepts and returns opaque
`uint64_t` handles, casting internally:

```cpp
// SDLWindow.cpp (PRIVATE — never visible to consumers)
std::vector<std::string> SDLWindow::getRequiredInstanceExtensions() const {
    Uint32 count{};
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&count);
    return std::vector<std::string>(exts, exts + count);
}

uint64_t SDLWindow::createSurface(uint64_t instanceHandle) const {
    VkInstance instance = reinterpret_cast<VkInstance>(instanceHandle);
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
        SONNET_LOG_ERROR("Failed to create surface: {}", SDL_GetError());
        return 0;
    }
    return reinterpret_cast<uint64_t>(surface);
}
```

`VkInstance` is a dispatchable handle (`void*`); `VkSurfaceKHR` is a non-dispatchable handle
(`uint64_t` on 64-bit platforms). Both fit safely in `uint64_t` on the 64-bit desktop targets
this engine supports. All Vulkan types stay inside `SDLWindow.cpp`. `SonnetWindow` has **no
external PUBLIC CMake dependencies** — its public headers use only the C++ standard library.

The `VulkanBackend` class (in `SonnetRendererVulkan`) calls these two `IWindow` methods during
its `init()` to obtain the extensions and create the surface, without ever seeing SDL3 types.

---

## Vulkan-Hpp C++ Bindings

**Decision**: Use Vulkan-Hpp v1.4.334 as header-only SYSTEM include; bypass its CMakeLists.txt
configuration and add the source root to the renderer's include path directly.

**Rationale**: Vulkan-Hpp's CMakeLists.txt is intended for SDK-level packaging and does not produce
a clean lightweight CMake target for embedded use. The header-only nature makes direct inclusion
the simplest and most reproducible approach.

**FetchContent declaration**:

```cmake
FetchContent_Declare(
    VulkanHpp
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
    GIT_TAG        v1.4.334
    GIT_SHALLOW    TRUE
    # Exclude from the default build — we only need the headers.
    EXCLUDE_FROM_ALL TRUE
)
FetchContent_GetProperties(VulkanHpp)
if(NOT vulkanhpp_POPULATED)
    FetchContent_Populate(VulkanHpp)
endif()
# In SonnetRenderer/CMakeLists.txt:
target_include_directories(SonnetRenderer SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR})
```

**Usage** (in private renderer source files only):

```cpp
#include <vulkan/vulkan.hpp>   // C++ wrapper; namespace vk::
// e.g. vk::Instance, vk::Device, vk::SwapchainKHR
```

**Key macros** (set before the include if needed):
- `VULKAN_HPP_NO_EXCEPTIONS` — replace exceptions with `vk::Result` returns (not used; we keep exceptions)
- `VULKAN_HPP_DISPATCH_LOADER_DYNAMIC` — use dynamic dispatch loader (not needed for this feature)

**Alternatives considered**: Using the system Vulkan SDK's bundled Vulkan-Hpp headers — rejected
because SDK versions vary across developer machines and would break reproducibility.

---

## Vulkan Instance and Validation Layers

**Decision**: Enable `VK_LAYER_KHRONOS_validation` and the `VK_EXT_debug_utils` extension in debug
builds only (controlled by a CMake option `SONNET_ENABLE_VALIDATION_LAYERS`, defaulting to `ON`
for `Debug` config).

**Rationale**: Satisfies the technical requirement to use validation layers for tracking warnings
and errors. Gating on build type avoids performance overhead in release builds.

**Programmatic setup**:

```cpp
// 1. Check layer availability before requesting:
auto availableLayers = vk::enumerateInstanceLayerProperties();
bool validationAvailable = std::ranges::any_of(availableLayers, [](const auto& p) {
    return std::string_view(p.layerName) == "VK_LAYER_KHRONOS_validation";
});

// 2. Collect extensions from SDL + debug utils:
Uint32 sdlExtCount{};
const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
std::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);
if (validationEnabled) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

// 3. Create instance with layers and debug messenger in pNext:
vk::DebugUtilsMessengerCreateInfoEXT debugCI{ /* severity + message type + callback */ };
vk::InstanceCreateInfo instanceCI{};
instanceCI.setPNext(validationEnabled ? &debugCI : nullptr);
```

The debug messenger callback forwards messages to `SONNET_LOG_WARN` / `SONNET_LOG_ERROR` using
the spdlog wrapper.

---

## SPIR-V Shader Compilation

**Decision**: Compile GLSL shaders to SPIR-V at CMake configure/build time using `Vulkan::glslc`;
copy compiled `.spv` files next to the executable; load them at runtime.

**Rationale**: Build-time compilation catches shader errors early (at `cmake --build` time rather
than at runtime). Runtime file loading keeps the binary small and avoids embedding binary blobs.

**CMake helper** (`cmake/ShaderCompilation.cmake`):

```cmake
function(compile_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SPV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT  ${SPV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc ${SHADER_SOURCE} -o ${SPV_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_NAME} → SPIR-V"
        VERBATIM
    )
    # Copy SPV next to the executable after each build
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SPV_OUTPUT}
            "$<TARGET_FILE_DIR:${TARGET}>/shaders/${SHADER_NAME}.spv"
        COMMENT "Copying ${SHADER_NAME}.spv to output directory"
    )
    target_sources(${TARGET} PRIVATE ${SPV_OUTPUT})
endfunction()
```

**Runtime path**: The renderer loads shaders from `<executable_dir>/shaders/triangle.vert.spv` and
`<executable_dir>/shaders/triangle.frag.spv`. `std::filesystem::path` is used to construct the path
relative to `argv[0]` (captured during `SDL_AppInit`).

**Triangle vertex shader strategy**: Vertices are hardcoded in the GLSL source (no vertex buffer).
This satisfies the static-triangle requirement with minimum Vulkan resource setup.

```glsl
// triangle.vert — three hardcoded NDC positions
const vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);
const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),  // red
    vec3(0.0, 1.0, 0.0),  // green
    vec3(0.0, 0.0, 1.0)   // blue
);
void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
```

---

## spdlog Configuration

**Decision**: Use spdlog v1.17.0 as a compiled static library (`spdlog::spdlog`); route `INFO` and
below to stdout, `WARN` and above to stderr; include filename and line number in every log line.

**Rationale**: Separating severity streams satisfies "all logs must output to standard streams"
while allowing operators to capture errors independently. Source location in every line satisfies
the "file name and line number" requirement.

**FetchContent declaration**:

```cmake
set(SPDLOG_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL   OFF CACHE BOOL "" FORCE)  # use bundled fmt
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)
# Exposed target: spdlog::spdlog
```

**Log level CMake option** (root `CMakeLists.txt`):

```cmake
set(SONNET_LOG_LEVEL "info" CACHE STRING "Compile-time log level (trace/debug/info/warn/error/critical/off)")
set_property(CACHE SONNET_LOG_LEVEL PROPERTY STRINGS trace debug info warn error critical off)

string(TOUPPER ${SONNET_LOG_LEVEL} SONNET_LOG_LEVEL_UPPER)
target_compile_definitions(SonnetLogging PUBLIC
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${SONNET_LOG_LEVEL_UPPER}
)
```

**Log pattern** (file/line included via `%s:%#`):

```cpp
// Logger.cpp
constexpr auto kPattern = "[%H:%M:%S.%e] [%^%l%$] [%s:%#] %v";
stdout_sink->set_pattern(kPattern);
stderr_sink->set_pattern(kPattern);
```

**Macros** (in `Logger.hpp`): `SPDLOG_INFO`, `SPDLOG_WARN`, etc. capture `__FILE__` / `__LINE__`
automatically when `SPDLOG_ACTIVE_LEVEL` is set. Re-exported as `SONNET_LOG_*` aliases.

---

## Catch2 Testing Setup

**Decision**: Use Catch2 v3.14.0 with `Catch2::Catch2WithMain` and `catch_discover_tests` for
automatic CTest registration.

**FetchContent declaration**:

```cmake
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
# Per test executable: catch_discover_tests(target)
```

**Test executable pattern** (each subdirectory has its own executable):

```cmake
add_executable(SonnetWindowTests window/WindowInterfaceTest.cpp)
target_link_libraries(SonnetWindowTests PRIVATE SonnetWindow Catch2::Catch2WithMain)
catch_discover_tests(SonnetWindowTests)
```

---

## Warnings-as-Errors (Project Code Only)

**Decision**: Apply `-Werror` (GCC/Clang) and `/WX` (MSVC) to all project targets via a shared
CMake function; never set these flags on FetchContent targets.

**Rationale**: External libraries often ship with benign warnings under strict compiler settings.
Applying `-Werror` to third-party code would break builds on compiler updates.

**Implementation** (`cmake/CompilerOptions.cmake`):

```cmake
function(sonnet_set_compile_options TARGET)
    target_compile_options(${TARGET} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror>
    )
    # Mark FetchContent headers as SYSTEM to suppress their warnings
    # (done at include_directories call sites, not here)
endfunction()
```

Called once per project target in each module's `CMakeLists.txt`. External targets (SDL3, spdlog,
Catch2, VulkanHpp) are never passed to this function.

---

## VulkanBackend Implementation Notes

These are implementation details that must be known to correctly implement `VulkanBackend.cpp`
in `SonnetRendererVulkan`.

### SDL_WINDOW_VULKAN Flag

`SDL_CreateWindow` must receive `SDL_WINDOW_VULKAN` in its flags parameter. Without this flag,
`SDL_Vulkan_CreateSurface` will fail. This flag is set inside `SDLWindow.cpp` (PRIVATE).

### Queue Family Selection

During physical device selection, select queue families as follows:
- **Graphics queue**: first family with `VK_QUEUE_GRAPHICS_BIT`.
- **Present queue**: first family where `vkGetPhysicalDeviceSurfaceSupportKHR` returns `VK_TRUE`.
- Both may be the same family (unified queue) — use a single `vk::DeviceQueueCreateInfo` in that case.

### Swapchain Format and Present Mode

Prefer in order:
- **Surface format**: `vk::Format::eB8G8R8A8Srgb` + `vk::ColorSpaceKHR::eSrgbNonlinear`. Fall back to the first available format if not found.
- **Present mode**: `vk::PresentModeKHR::eMailbox` if available, otherwise `vk::PresentModeKHR::eFifo` (always available per spec).
- **Swap extent**: clamp `VkSurfaceCapabilitiesKHR.currentExtent` to the window's reported pixel size from `IWindow::getSize()`.

### In-Flight Frames

Use **2** frames in flight (double-buffering). Allocate 2 sets of: command buffers, image-available semaphores, render-finished semaphores, in-flight fences. Track current frame index as `m_currentFrame = (m_currentFrame + 1) % 2`.

### Swapchain Rebuild on Resize

`beginFrame()` should check for `vk::Result::eErrorOutOfDateKHR` or `vk::Result::eSuboptimalKHR`
from `acquireNextImageKHR`. If either is returned, call an internal `rebuildSwapchain()` method
that destroys and recreates: framebuffers → image views → swapchain. Do not recreate the render
pass or pipeline.

### SPIR-V Runtime Path

Load shaders relative to the executable using `std::filesystem`:

```cpp
std::filesystem::path exeDir = std::filesystem::path(argv0).parent_path();
auto vertPath = exeDir / "shaders" / "triangle.vert.spv";
auto fragPath = exeDir / "shaders" / "triangle.frag.spv";
```

`argv0` is captured from the `argc`/`argv` parameters of `SDL_AppInit` and passed to
`VulkanBackend::init()` (or stored in `AppState` before `renderer->init()` is called).

### Fragment Shader

```glsl
// triangle.frag
#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(fragColor, 1.0);
}
```

### SONNET_ENABLE_VALIDATION_LAYERS → C++ Guard

In `src/renderer_vulkan/CMakeLists.txt`, the CMake option maps to a preprocessor define:

```cmake
if(SONNET_ENABLE_VALIDATION_LAYERS)
    target_compile_definitions(SonnetRendererVulkan PRIVATE SONNET_VALIDATION_LAYERS_ENABLED)
endif()
```

In `VulkanBackend.cpp`, guard layer and debug messenger setup with:

```cpp
#ifdef SONNET_VALIDATION_LAYERS_ENABLED
    constexpr bool kValidationEnabled = true;
#else
    constexpr bool kValidationEnabled = false;
#endif
```

### Debug Messenger Callback

```cpp
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        SONNET_LOG_ERROR("Vulkan: {}", data->pMessage);
    else
        SONNET_LOG_WARN("Vulkan: {}", data->pMessage);
    return VK_FALSE;
}
```

---

## Logger Initialization Call Site

**Decision**: `sonnet::logging::init()` is the **first call** inside `SDL_AppInit`, before
`SDL_Init` or any module construction.

**Rationale**: If SDL3 or Vulkan initialization fails, the error must be captured by the logger.
Initializing logging after SDL would silently drop any errors that occur during SDL startup.

**Pattern** (in `src/app/main.cpp`):

```cpp
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    sonnet::logging::init();   // must be first

    auto window = sonnet::window::WindowFactory::createDefaultWindow();
    if (!window->init()) {
        return SDL_APP_FAILURE;  // error already logged by SDLWindow
    }
    // ... renderer setup ...
}
```

---

## Window Default Dimensions

**Decision**: 800×600 as the startup default (documented in spec Assumptions section).

**Rationale**: 800×600 is universally supported, low-risk for test environments, and matches the
spec assumption that "exact default will be decided at planning stage."
