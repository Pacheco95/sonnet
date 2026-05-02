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

## Code Quality

PRs are gated on a `code-quality` CI job that runs clang-format, clang-tidy, and cppcheck before the build-and-test job. Maintainers can run the same checks locally.

### Install tools

**Linux (Ubuntu/Debian)**

```sh
sudo apt install clang-format clang-tidy cppcheck
```

**macOS**

```sh
brew install llvm cppcheck
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

**Windows** — Install LLVM from https://releases.llvm.org/ (provides clang-format and clang-tidy) and cppcheck from https://cppcheck.sourceforge.io/. Ensure both are on your `PATH`.

### Format check (non-modifying)

```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

Exit code 0 = all files conform. Non-zero = violations found (files + lines printed to stderr).

### Format fix (in-place rewrite)

```sh
find src/ tests/ -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

### SAST — clang-tidy

Requires `compile_commands.json`. Generate it once with:

```sh
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
```

Then run:

```sh
run-clang-tidy -p build src/
```

### SAST — cppcheck (blocking checks)

```sh
cppcheck src/ tests/ \
  --enable=warning,performance,portability \
  --error-exitcode=1 \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

### SAST — cppcheck (informational, non-blocking)

```sh
cppcheck src/ tests/ \
  --enable=information \
  --suppress="*:vendor/*" \
  --suppress="*:build*/*"
```

### Post-merge branch protection

After merging this branch, add `code-quality` to the required status checks under **GitHub Settings → Branches → main branch protection rule**.
