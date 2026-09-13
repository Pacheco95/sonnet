# Build system

CMake with presets, vcpkg in manifest mode, Ninja. The scaffolding described here landed in milestone M0; parts that a later milestone adds say so.

## Toolchains

C++23 is required. Minimum compilers, chosen for `std::expected`, `std::print`, deducing `this` and `std::format`:

| Platform | Compiler |
|---|---|
| Linux | GCC 14+, or Clang 19+ with libstdc++ 14+, or Clang 18+ with libc++ 18+ (libstdc++ hides `std::expected` from Clang 18 because it reports `__cpp_concepts` below 202002L) |
| Windows | MSVC 17.10+ (Visual Studio 2022) or clang-cl of the same LLVM version |
| macOS, iOS | Apple Clang from Xcode 16.3+ |
| Android | NDK r27+ (Clang 18) |

C++23 features not relied on until every toolchain above ships them: `std::generator`, `std::flat_map`, `import std`. C++20 modules are not used for engine code. Vulkan-HPP's `vulkan.cppm` module is an optional experiment for compile times, behind a CMake option, never required.

Other prerequisites:

- CMake 3.28+ and Ninja.
- vcpkg, with `VCPKG_ROOT` set. The presets read it.
- Vulkan SDK on developer machines for the validation layers, RenderDoc-friendly tooling and `slangc`. The SDK is not needed to build: headers and loader come from vcpkg.
- Android: NDK r27+, `ANDROID_NDK_HOME` set. iOS: Xcode on a macOS host.

## Dependency policy

vcpkg manifest mode is the default for every dependency. `FetchContent` is allowed only when a library has no vcpkg port, or when a pinned fork carrying local patches is needed and an overlay port is more work than it is worth. A library is never provided by both; if a `FetchContent` dependency later gets a port, it moves. The rationale is in [ADR-0004](decisions/0004-vcpkg-first.md).

`vcpkg.json` lists the dependencies with the features used. A port is added in the milestone that first uses it, so the manifest never carries an unused dependency:

| Milestone | Ports |
|---|---|
| M0 | `sdl3` (features `vulkan`, and `x11` and `wayland` on Linux; default features off so `ibus` and `dbus` do not pull in `libsystemd`), `vulkan-headers`, `vulkan-loader`, `vk-bootstrap`, `vulkan-memory-allocator-hpp`, `shader-slang`, `glm`, `spdlog`, `tracy`, `catch2` |
| M1 | `imgui` with `docking-experimental`, `sdl3-binding`, `vulkan-binding` |
| M2 | `flecs`, `nlohmann-json` |
| M3 | `fastgltf`, `stb`, `ktx` |
| M4 | `joltphysics`, `lua`, `sol2` |
| M5 | `miniaudio` |

Versions are pinned through the `builtin-baseline` in the manifest (and `overrides` when a port has to stay behind the baseline), so every machine and CI job resolves the same set. CI checks out vcpkg at that baseline rather than using the runner's copy.

Patched ports live under `ports/` as overlay ports, registered by `vcpkg-configuration.json` next to the manifest so no preset or environment variable has to name them. Each one is a copy of the registry port with the change described in `ports/README.md`. One is `sdl3`, which keeps the X11 extensions an editor needs (XInput2 for relative mouse mode and raw motion, Xcursor, Xfixes, XRandR, XScrnSaver); the registry port turns them all off. The other is `imgui`: the registry port's SDL3 bindings depend on `sdl3` with its default features, which on Linux re-enables `ibus` and through it `dbus[systemd]` and `libsystemd` for every consumer, undoing the top-level manifest's choice; the overlay turns the defaults off. Its Vulkan binding also links the `vulkan-loader` port, which would put a second loader into every binary, so the overlay builds the backend against the headers only and the `ui` module feeds it the entry points of the loader SDL holds.

Triplets: `x64-windows`, `x64-linux`, `arm64-osx` (and `x64-osx`), `arm64-android`, `arm64-ios`. Mobile triplets are wired in M7.

## Presets

`CMakePresets.json` defines one configure preset per platform and configuration, each pointing at `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` as the toolchain file, and matching build and test presets:

| Preset | Notes |
|---|---|
| `linux-debug`, `linux-release` | Ninja, Clang or GCC from `PATH` |
| `linux-asan` | Address and undefined-behaviour sanitizers |
| `linux-coverage` | gcov instrumentation, `coverage` target runs gcovr (same shape as the previous iteration) |
| `windows-debug`, `windows-release` | Ninja with MSVC from a developer prompt |
| `macos-debug`, `macos-release` | Ninja, Apple Clang |
| `android-debug` | Chain-loads the NDK toolchain through `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`, player only. Added in M7 |
| `ios-debug` | Xcode generator, `CMAKE_SYSTEM_NAME=iOS`, player only. Added in M7 |

Binary directories are `build/<preset>/`. In-source builds are rejected. Configuring without a preset still works when `VCPKG_ROOT` is set or a checkout exists at `~/vcpkg`: the root `CMakeLists.txt` picks the toolchain file up itself, and otherwise stops with a message saying so.

Machine-local presets go in `CMakeUserPresets.json` next to `CMakePresets.json`, which CMake reads automatically and git ignores. It is the place for settings that depend on one machine rather than on the platform. The known case is macOS with Homebrew LLVM instead of Apple Clang: that compiler does not find the SDK by itself, so a local preset inherits `macos-base` and sets `CMAKE_OSX_SYSROOT`:

```json
{
  "version": 8,
  "configurePresets": [
    {
      "name": "macos-debug-local",
      "inherits": "macos-base",
      "displayName": "macOS Debug (local, Homebrew LLVM)",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_OSX_SYSROOT": "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
      }
    }
  ],
  "buildPresets": [{ "name": "macos-debug-local", "configurePreset": "macos-debug-local" }],
  "testPresets": [{ "name": "macos-debug-local", "configurePreset": "macos-debug-local", "output": { "outputOnFailure": true } }]
}
```

IDEs: CLion and Visual Studio read `CMakePresets.json`; in CLion enable the preset profiles under Settings, Build, CMake, and give the profile `VCPKG_ROOT` in its environment field when the IDE was not started from a shell that exports it. `cmake-build-*` directories are ignored by git. Platform presets carry a `condition` on the host system, so `cmake --list-presets` shows only the ones that apply. Every preset exports `compile_commands.json` for clangd and clang-tidy.

## Options

| Option | Default | Meaning |
|---|---|---|
| `SONNET_RHI` | `Vulkan` | Graphics implementation. Only `Vulkan` exists |
| `SONNET_BUILD_EDITOR` | `ON` on desktop, forced `OFF` on mobile | Builds `ui`, `editor` and `apps/editor` |
| `SONNET_BUILD_PLAYER` | `ON` | Builds `apps/player`. Added in M6 |
| `SONNET_BUILD_TESTS` | `ON` | Builds Catch2 tests and registers them with CTest |
| `SONNET_BUILD_SAMPLES` | `ON` | Copies `apps/samples/` next to the editor binary, so `sonnet_editor samples/basic` works from the build directory |
| `SONNET_ENABLE_TRACY` | `ON` | Compiles Tracy zones in, and links Tracy, in Debug and RelWithDebInfo; Release never has them |
| `SONNET_ENABLE_VALIDATION` | `ON` | Requests Vulkan validation layers at instance creation in Debug |
| `SONNET_SANITIZERS` | `OFF` | Address and undefined-behaviour sanitizers (the `linux-asan` preset) |
| `SONNET_COVERAGE` | `OFF` | gcov instrumentation for engine modules only |

Global flags: `CMAKE_CXX_STANDARD 23`, extensions off. Warnings are an interface target, `sonnet::warnings`, that `sonnet_add_module` links privately with `COMPILE_WARNING_AS_ERROR`; vcpkg include directories are `SYSTEM`, so third-party headers never trip them. On GCC and Clang, `-fmacro-prefix-map` makes `__FILE__` and `std::source_location` repository-relative while debug info keeps absolute paths, so debuggers find the sources; `-ffile-prefix-map` would rewrite those too and breakpoints set from an IDE would never bind.

Per-configuration definitions applied by `sonnet_add_module`: `SONNET_ASSERTS_ENABLED` in Debug and RelWithDebInfo, `SPDLOG_ACTIVE_LEVEL` at `TRACE` there and `INFO` in Release, `SONNET_ENABLE_TRACY` together with the Tracy link. `SONNET_MODULE` is defined per target to its name and feeds the logging macros ([core.md](core.md#logging)).

## Module helpers

`cmake/functions.cmake` provides two functions so every module has the same shape:

- `sonnet_add_module(<name> SOURCES ... DEPENDS ... PUBLIC_DEPENDS ...)` creates the static library `sonnet_<name>` with alias `sonnet::<name>`, sets the include directory to `modules/<name>/include`, applies the shared warning flags, and links the declared dependencies. Dependencies are the only way a module reaches another, which is what enforces the one-way rule.
- `sonnet_add_module_test(<name> SOURCES ... DEPENDS ...)` creates `<name>_tests` linked against the module and `Catch2::Catch2WithMain`, registers it with CTest under the label `<name>` (so `ctest -L core` runs one module) with a five-minute timeout and `--allow-running-no-tests` (a suite whose every case skipped, such as `rhi_tests` without a Vulkan device, is a pass), compiles in `tests/support/TestSupport.cpp` (which turns Windows crash and assertion dialogs into stderr output and a non-zero exit), and allows including the module's `src/` directory for white-box tests.

- `sonnet_add_executable(<name> SOURCES ... DEPENDS ...)` creates the target `sonnet_<name>_app`, whose binary is `sonnet_<name>`, with the same flags and definitions as a module. The target name carries the suffix because an app shares its name with the module it fronts (`editor`), and the module owns `sonnet_<name>`.
- `sonnet_add_shaders(<target> SHADERS ...)` and `sonnet_add_engine_shaders(<target>)` compile Slang shaders next to a target's binary ([Shaders in the build](#shaders-in-the-build)).
- `sonnet_copy_tracy_client(<target>)`, applied by the test and executable helpers, copies `TracyClient.dll` next to Windows binaries. vcpkg's tracy port installs the Debug DLL under `debug/bin/Debug`, where vcpkg's own applocal copy step does not look, so a binary that references Tracy symbols would otherwise fail to start with `STATUS_DLL_NOT_FOUND` before reaching `main`, which no in-process setting can catch. `find_package(Tracy)` is `GLOBAL` for that reason.

The helpers live in `cmake/SonnetFunctions.cmake`; options are in `SonnetOptions.cmake`, warning flags in `SonnetWarnings.cmake` and the coverage target in `SonnetCoverage.cmake`.

`modules/CMakeLists.txt` adds the modules in dependency order, and that order is the canonical statement of the architecture. `ui` and `editor` are added only when `SONNET_BUILD_EDITOR` is on. `rhi` also exports the interface target `sonnet::rhi_vulkan` (its implementation headers and the Vulkan-HPP configuration), which only `ui` may link, for Dear ImGui's Vulkan backend.

## Shaders in the build

Engine shaders live in `modules/renderer/shaders/*.slang`; the rhi tests keep their own next to them. `sonnet_add_shaders(<target> SHADERS ...)` compiles each file to SPIR-V with `slangc` from the `shader-slang` port at build time and places the result in `shaders/` next to the target's binary, so a release build has no runtime compiler dependency. Every compilation sees the engine shader directory, so any file can `import sonnet`. The renderer module registers its entry-point files in the `SONNET_ENGINE_SHADERS` global property and `sonnet_add_engine_shaders(<target>)` compiles them next to the given executable or test, since a static library has no binary directory for them to land in. The editor additionally compiles at runtime for hot reload from M3. See [rendering.md](rendering.md#shaders).

## Building and testing

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

# run the editor on a sample project
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic
# run the player on the same project
./build/linux-debug/apps/player/sonnet_player apps/samples/basic
```

## Continuous integration

GitHub Actions, one workflow with a matrix:

- `ubuntu-latest` (GCC and Clang), `windows-latest` (MSVC), `macos-latest` (Apple Clang): configure, build, run tests.
- Vulkan-dependent tests run on Linux under Lavapipe (Mesa's CPU Vulkan implementation, which supports 1.4) so the renderer is exercised without a GPU.
- `linux-asan` job on every pull request.
- Android job that builds the player with the NDK, added in M7.
- vcpkg binary caching through the GitHub Actions cache so dependency builds are not repeated.
- A lint job runs `clang-format --dry-run` on every tracked source (`.clang-format` lists only the differences from LLVM style, so it parses with clang-format 18 and newer; CI uses 20), `tools/check_docs.py`, `tools/check_version.py` (the manifest mirrors the CMake version) and, on pull requests, `tools/check_commit_msg.py` over the new commits. `clang-tidy` runs on the changed sources of a pull request in the Linux Clang job using the build's `compile_commands.json`.
- Linux runners install Mesa from the kisak PPA so Lavapipe exposes Vulkan 1.4, and the system libraries SDL3's X11 and Wayland features need, including the headers of the X11 extensions the `sdl3` overlay port enables (XInput2, Xcursor, Xfixes, XRandR, XScrnSaver); SDL's configure fails, naming the package, when an enabled extension's header is missing, so a developer machine needs the same packages. Tests run with `VK_DRIVER_FILES` pointing at Lavapipe; tests that need a window ask `platform` for a headless instance, which uses SDL's offscreen video driver.

Tests that need a Vulkan 1.4 device skip themselves when none is present, which is the case on the Windows and macOS runners.

`tools/install_hooks.sh` installs the `commit-msg` hook that runs the same commit-message check locally.

## Coverage

`linux-coverage` builds engine modules with `--coverage`, excludes third-party code, and the `coverage` target runs the tests and gcovr to produce `build/linux-coverage/coverage/index.html`.
