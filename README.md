# Sonnet Engine

[![CI](https://github.com/Pacheco95/sonnet/actions/workflows/ci.yml/badge.svg)](https://github.com/Pacheco95/sonnet/actions/workflows/ci.yml)

A native desktop game engine rendering a single RGB triangle using Vulkan, driven by SDL3's callback-based main loop. This is the first runnable milestone.

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | GCC 13+ / Clang 17+ / MSVC 2022 17.5+ | C++23 required |
| CMake | 4.2.1+ | `cmake --version` to verify |
| Vulkan SDK | 1.3+ | [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home) |
| Git | Any | FetchContent downloads deps at configure time |
| Internet | — | Required on first configure |

## Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

## Build

```bash
cmake --build build --parallel
```

## Run

```bash
./build/src/app/Sonnet
```

A window titled **"Sonnet Engine"** opens at 800×600 displaying an RGB triangle. Close it via the OS window controls to exit cleanly.

## Test

```bash
cd build && ctest --output-on-failure
```

All tests run without a display or GPU.

## Modules

| Module | Type | Description |
|---|---|---|
| `SonnetLogging` | Static library | spdlog wrapper; `SONNET_LOG_*` macros |
| `SonnetWindow` | Static library | `IWindow` interface; SDL3-backed implementation |
| `SonnetRenderer` | Static library | Frontend `IRenderer`; delegates to a backend |
| `SonnetRendererVulkan` | Static library | Vulkan `IRendererBackend` implementation |
| `Sonnet` | Executable | SDL3 callback entry point; wires modules together |

## CMake Options

| Option | Default | Description |
|---|---|---|
| `SONNET_LOG_LEVEL` | `info` | Compile-time log level (`trace`/`debug`/`info`/`warn`/`error`/`critical`/`off`) |
| `SONNET_ENABLE_VALIDATION_LAYERS` | `ON` (Debug) | Vulkan `VK_LAYER_KHRONOS_validation` |
| `SONNET_BUILD_TESTS` | `ON` | Build and register Catch2 tests |
