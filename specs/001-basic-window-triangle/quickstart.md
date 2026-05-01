# Quickstart: Basic Window with Triangle Rendering

**Date**: 2026-05-01 | **Plan**: [plan.md](plan.md)

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | GCC 13+ / Clang 17+ / MSVC 2022 17.5+ | C++23 support required |
| CMake | 4.2.1+ | `cmake --version` to verify |
| Vulkan SDK | 1.3+ | [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home); sets `VULKAN_SDK` env var |
| Git | Any | FetchContent clones SDL3, spdlog, Catch2, Vulkan-Hpp at configure time |
| Internet connection | — | Required on first configure for FetchContent downloads |

**Verify Vulkan SDK**:

```bash
# Linux / macOS
echo $VULKAN_SDK          # should point to SDK root
vulkaninfo --summary      # should list a Vulkan-capable GPU

# Windows (PowerShell)
$env:VULKAN_SDK           # should point to SDK root
vulkaninfo --summary
```

---

## Configure

```bash
# Clone the repository
git clone <repo-url> sonnet
cd sonnet

# Configure (Debug build — enables Vulkan validation layers by default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Configure (Release build — validation layers off, optimized)
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
```

### Optional CMake flags

```bash
# Change compile-time log level (trace/debug/info/warn/error/critical/off)
cmake -S . -B build -DSONNET_LOG_LEVEL=debug

# Force validation layers on in Release (useful for profiling)
cmake -S . -B build-release -DSONNET_ENABLE_VALIDATION_LAYERS=ON

# Skip building tests
cmake -S . -B build -DSONNET_BUILD_TESTS=OFF
```

---

## Build

```bash
cmake --build build --parallel
```

On first build, CMake will download SDL3 3.4.4, spdlog v1.17.0, Catch2 v3.14.0, and Vulkan-Hpp
v1.4.334. Subsequent builds use the cached downloads.

GLSL shaders are compiled to SPIR-V automatically during the build. Compiled `.spv` files are
placed in `build/shaders/`.

---

## Run

```bash
# Linux / macOS
./build/src/app/Sonnet

# Windows
.\build\src\app\Debug\Sonnet.exe
```

A window titled **"Sonnet Engine"** opens at 800×600 displaying a single RGB triangle on a dark
background. Close the window using the OS window controls (X button) to exit cleanly.

**Expected log output** (info level):

```
[HH:MM:SS.mmm] [info] [Logger.cpp:23] Sonnet logging initialized
[HH:MM:SS.mmm] [info] [SDLWindow.cpp:45] Window created: 800x600 "Sonnet Engine"
[HH:MM:SS.mmm] [info] [VulkanBackend.cpp:87] Vulkan instance created
[HH:MM:SS.mmm] [info] [VulkanBackend.cpp:143] Selected GPU: <device name>
[HH:MM:SS.mmm] [info] [VulkanBackend.cpp:210] Renderer initialized
```

**Headless / no display** — the engine exits with code 1 and prints:

```
[HH:MM:SS.mmm] [error] [SDLWindow.cpp:38] Failed to create window: <SDL error>
```

---

## Test

```bash
# Run all tests via CTest
cd build
ctest --output-on-failure

# Run tests with verbose output
ctest -V

# Run a specific test suite
ctest -R SonnetWindowTests
ctest -R SonnetRendererTests
ctest -R SonnetLoggingTests
ctest -R AppLifecycleTests
```

All unit tests run without a display server or GPU. The integration test (`AppLifecycleTest`)
exercises the full init/shutdown lifecycle using mock implementations of `IWindow` and `IRenderer`.

---

## Project Structure at a Glance

```
cmake/                    CMake helpers (FetchDependencies, CompilerOptions, ShaderCompilation)
src/
  app/                    SonnetApp executable (SDL3 callback entry point)
  window/                 SonnetWindow module (IWindow + SDL3 implementation)
  renderer/               SonnetRenderer module (IRenderer + Vulkan implementation)
  logging/                SonnetLogging module (spdlog wrapper)
tests/
  unit/                   Per-module unit tests (Catch2)
  integration/            Full lifecycle integration test (Catch2)
specs/                    Feature documentation (spec, plan, research, tasks)
vendor/                   Reserved for deps not available via FetchContent
```
