# Build system

CMake with presets, vcpkg in manifest mode, Ninja. The scaffolding described here landed in milestone M0; parts that a later milestone adds say so.

## Toolchains

C++23 is required. Minimum compilers, chosen for `std::expected`, `std::print`, deducing `this` and `std::format`:

| Platform | Compiler |
|---|---|
| Linux | GCC 14+, or Clang 19+ with libstdc++ 14+, or Clang 18+ with libc++ 18+ (libstdc++ hides `std::expected` from Clang 18 because it reports `__cpp_concepts` below 202002L) |
| Windows | MSVC 17.10+ (Visual Studio 2022) or clang-cl of the same LLVM version |
| macOS, iOS | Apple Clang from Xcode 16.3+ |
| Android | NDK r30+ (Clang 21), the first NDK whose sysroot reaches API 36 (Android 16); r27 to r29 stop at 35 |

libc++ from LLVM 19 on, which every supported NDK ships, is stricter than libstdc++ and Xcode's libc++ in one place the code met: it has no `std::char_traits<std::byte>`, so JSON and CBOR are read through `assets::parseJson` and `parseCbor` rather than handing nlohmann bytes ([assets.md](assets.md#reading-json-and-cbor)).

C++23 features not relied on until every toolchain above ships them: `std::generator`, `std::flat_map`, `import std`. C++20 modules are not used for engine code. Vulkan-HPP's `vulkan.cppm` module is an optional experiment for compile times, behind a CMake option, never required.

Other prerequisites:

- CMake 3.28+ and Ninja.
- vcpkg, with `VCPKG_ROOT` set. The presets read it.
- Vulkan SDK on developer machines for the validation layers, RenderDoc-friendly tooling and `slangc`. The SDK is not needed to build: headers and the shader compiler come from vcpkg. On Linux, SDL loads the system Vulkan loader (`libvulkan1` on Ubuntu).
- Android: NDK r30+, with `ANDROID_NDK_HOME` pointing at it (the presets chain-load `$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake`), from a Linux or macOS host. The presets also package the APK, which needs the Android SDK with platform `android-36` and build-tools 36.1.0, `ANDROID_HOME` pointing at it, a JDK 17 or later through `JAVA_HOME` or `PATH`, and `zip` ([Android](#android)). iOS: Xcode on a macOS host.

### Linux setup

vcpkg builds the libraries, but SDL3, the Vulkan loader and miniaudio compile against the system's window and audio headers, so a Linux machine needs them installed. On Ubuntu 24.04 the list is the one the CI workflow installs:

```bash
wget -qO- https://apt.llvm.org/llvm.sh | sudo bash -s -- 20
sudo apt-get install -y ninja-build gcc-14 g++-14 clang-20 clang-tidy-20 clang-format-20 vulkan-tools \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl-dev libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libibus-1.0-dev
```

Neither of Ubuntu 24.04's own compilers meets the table above: GCC is 13, and Clang 18 sees libstdc++ 13. Clang 20 from apt.llvm.org is what CI uses, and `g++-14` is there for its libstdc++ 14, which Clang picks up on its own. Select the compiler on the first configure, because vcpkg builds every port with it and keeps it for the build directory; changing it later needs `--fresh`:

```bash
CC=clang-20 CXX=clang++-20 cmake --preset linux-debug
```

Ubuntu's `vulkan-validationlayers` package is 1.3.275, older than the Vulkan 1.4 the engine requires, so the layers come from the Vulkan SDK. Without them a Debug build logs that validation was requested but is not installed, and runs without it.


## Dependency policy

vcpkg manifest mode is the default for every dependency. `FetchContent` is allowed only when a library has no vcpkg port, or when a pinned fork carrying local patches is needed and an overlay port is more work than it is worth. A library is never provided by both; if a `FetchContent` dependency later gets a port, it moves. The rationale is in [ADR-0004](decisions/0004-vcpkg-first.md).

`vcpkg.json` lists the dependencies with the features used. A port is added in the milestone that first uses it, so the manifest never carries an unused dependency:

| Milestone | Ports |
|---|---|
| M0 | `sdl3` (features `vulkan`, and `x11` and `wayland` on Linux; default features off so `ibus` and `dbus` do not pull in `libsystemd`), `vulkan-headers`, `vulkan-loader`, `vk-bootstrap`, `vulkan-memory-allocator-hpp`, `shader-slang`, `glm`, `spdlog`, `tracy`, `catch2` |
| M1 | `imgui` with `docking-experimental`, `sdl3-binding`, `vulkan-binding` |
| M2 | `flecs`, `nlohmann-json` |
| M3 | `fastgltf`, `stb`, `ktx` |
| M4 | `joltphysics`, `lua` (feature `cpp`), `sol2` |
| M5 | `miniaudio` (and `stb`'s `stb_vorbis.c`, already there, for Ogg Vorbis) |

Versions are pinned through the `builtin-baseline` in the manifest (and `overrides` when a port has to stay behind the baseline), so every machine and CI job resolves the same set. CI checks out vcpkg at that baseline rather than using the runner's copy.

Patched ports live under `ports/` as overlay ports, registered by `vcpkg-configuration.json` next to the manifest so no preset or environment variable has to name them. Each one is a copy of the registry port with the change described in `ports/README.md`. One is `sdl3`, which keeps the X11 extensions an editor needs (XInput2 for relative mouse mode and raw motion, Xcursor, Xfixes, XRandR, XScrnSaver); the registry port turns them all off. Another is `imgui`: the registry port's SDL3 bindings depend on `sdl3` with its default features, which on Linux re-enables `ibus` and through it `dbus[systemd]` and `libsystemd` for every consumer, undoing the top-level manifest's choice; the overlay turns the defaults off. Its Vulkan binding also links the `vulkan-loader` port, which would put a second loader into every binary, so the overlay builds the backend against the headers only and the `ui` module feeds it the entry points of the loader SDL holds. The third is `ktx`, whose build applies a Clang-only floating-point flag to C sources whenever the C++ compiler is Clang, which fails with a GCC `cc` next to a Clang `c++`; the overlay decides the flag per language. The fourth is `nlohmann-json`, whose `byte_swap` leaves a loop after `if constexpr` branches that return, which MSVC reports as unreachable code in the translation unit that reads CBOR, beyond the reach of `/external:W0`; the overlay chains the branches with `else`.

Lua is built with its `cpp` feature and `scripting` links the C++ library, so a Lua error unwinds C++ frames as an exception instead of a `longjmp` over destructors; `SOL_USING_CXX_LUA` tells sol2 ([scripting.md](scripting.md#errors-and-hot-reload)).

miniaudio and stb_vorbis are single-file libraries without CMake packages: `audio` finds their headers with `find_path` and compiles their implementations once, in `src/MiniaudioImplementation.cpp`, with stb_vorbis's header part before miniaudio so its Ogg Vorbis decoder is found. Both come from vcpkg's include directory, which is a system one, so the engine's warnings do not reach them. The module links the platform's threads and `${CMAKE_DL_LIBS}`; miniaudio loads the audio libraries it needs at run time ([audio.md](audio.md#miniaudio)).

The `joltphysics` port builds Jolt with AVX2 and its companions on x64; its instruction-set flags are an interface property of `Jolt::Jolt`, which `physics` links privately so they stay on that module's sources. The manifest asks for the port's `rtti` feature, so Jolt and every module are compiled with RTTI and the sanitizer build keeps UBSan's `vptr` check everywhere ([physics.md](physics.md#jolt)).

Triplets: `x64-windows`, `x64-linux` (and `x64-linux-tsan` for the thread sanitizer, see [Presets](#presets)), `arm64-osx` (and `x64-osx`), `arm64-android`, `arm64-ios` (M10). The manifest declares `shader-slang` twice, once as a host tool for `slangc` and once for the library, desktop only, since only `editor` links it; `vulkan-loader` and `imgui` are desktop only too ([ADR-0018](decisions/0018-mobile-export.md#builds)). `arm64-android` therefore installs `slangc` under the host triplet, `vcpkg_installed/<host triplet>/tools/shader-slang`. vcpkg's toolchain puts only the target triplet's tools on `CMAKE_PROGRAM_PATH` unless `VCPKG_HOST_TRIPLET` is set, which it never sets itself, so a cross build's `cmake/SonnetShaders.cmake` adds the host directory. A Vulkan SDK on `PATH` or `CMAKE_PREFIX_PATH` is not used, since its `slangc` is not the pinned version.

## Linux runtime libraries

Only `editor` links the Slang library. `ShaderCompiler` and its tests live there; the player, cook and lower modules need only the SPIR-V compiled by the build. `SonnetShaders.cmake` finds the host `slangc` executable without importing the Slang library package.

`sonnet_copy_slang_runtime` copies the compiler and its loadable Slang modules beside the Linux editor and `editor_tests`, and sets their RUNPATH to `$ORIGIN`. CMake's automatic build RUNPATH is replaced, so it cannot expose the vcpkg library directory and its Vulkan loader to SDL. The other Linux binaries have no Slang RUNPATH. SDL keeps its usual library lookup, using the distribution's loader unless the user explicitly overrides it ([ADR-0020](decisions/0020-linux-runtime-libraries.md)). The `editor_tests` loader case checks the retained library after `Platform` shuts down; CTest clears `LD_LIBRARY_PATH` and `SDL_VULKAN_LIBRARY` so SDK settings cannot hide a regression. It skips under `linux-tsan`, whose test preset explicitly sets `VK_DRIVER_FILES=/dev/null` to disable Vulkan drivers.

## Presets

`CMakePresets.json` defines one configure preset per platform and configuration, each pointing at `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` as the toolchain file, and matching build and test presets:

| Preset | Notes |
|---|---|
| `linux-debug`, `linux-release` | Ninja, Clang or GCC from `PATH` |
| `linux-asan` | Address and undefined-behaviour sanitizers. An undefined-behaviour finding is fatal: `SONNET_SANITIZERS` compiles with `-fno-sanitize-recover=undefined`, so the first finding aborts the process and fails its test, whether the binary runs under `ctest` or on its own. An address finding was already fatal |
| `linux-tsan` | Thread sanitizer, for the job system and what runs on it. Tracy is off in it, because its lock-free queue uses fences the sanitizer cannot model and reports races of its own that would drown real ones. The test preset points `TSAN_OPTIONS` at `tools/tsan.supp`, makes a race fail the test, and sets `VK_DRIVER_FILES` to `/dev/null` so the GPU cases skip: Mesa's software rasterizer runs on threads of its own that carry no instrumentation. The preset's triplet is `x64-linux-tsan`, an overlay triplet in `triplets/` that `vcpkg-configuration.json` registers: `x64-linux` with Jolt alone compiled with `-fsanitize=thread`, since Jolt orders its threads inside the library and an uninstrumented build hid that from the sanitizer. Jolt has to be compiled by the preset's own compiler, which vcpkg takes from `CC` and `CXX` in the configure environment on Linux (Clang 20 locally and in CI). The other ports are built as `x64-linux` builds them, once more under the new triplet's name. A build directory configured before the triplet changed needs `cmake --preset linux-tsan --fresh`, or it keeps tool paths such as `slangc` under the old triplet's directory |
| `linux-coverage` | gcov instrumentation, `coverage` target runs gcovr (same shape as the previous iteration) |
| `windows-debug`, `windows-release` | Ninja with MSVC from a developer prompt |
| `macos-debug`, `macos-release` | Ninja, Apple Clang |
| `android-debug`, `android-release` | Linux or macOS host. Ninja, the `arm64-android` triplet, and the NDK's `android.toolchain.cmake` chain-loaded through `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`, with `ANDROID_ABI=arm64-v8a` and `ANDROID_PLATFORM=android-36` (Android 16). Player only: `SONNET_BUILD_EDITOR` is forced off, so `ui`, `editor` and `apps/cook` are not configured, and `SONNET_BUILD_TESTS` is off. Build presets but no test presets, since a device cannot run `ctest`. The player is `libsonnet_player.so` (see [Module helpers](#module-helpers)), packaged into `apps/player/sonnet_player.apk` ([Android](#android)) |
| `ios-debug` | Xcode generator, `CMAKE_SYSTEM_NAME=iOS`, player only. Added in M10 |

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

IDEs: CLion and Visual Studio read `CMakePresets.json`; in CLion enable the preset profiles under Settings, Build, CMake, and give the profile `VCPKG_ROOT` in its environment field when the IDE was not started from a shell that exports it. `cmake-build-*` directories are ignored by git. For VS Code, `/setup-sonnet` offers to write `.vscode/` for one configured Linux preset: build and test tasks, and gdb launches for the editor and for one module's tests on Lavapipe. `.vscode/` is ignored by git too. Platform presets carry a `condition` on the host system, so `cmake --list-presets` shows only the ones that apply. Every preset exports `compile_commands.json` for clangd and clang-tidy.

## Options

| Option | Default | Meaning |
|---|---|---|
| `SONNET_RHI` | `Vulkan` | Graphics implementation. Only `Vulkan` exists |
| `SONNET_BUILD_EDITOR` | `ON` on desktop, forced `OFF` on mobile | Builds `ui`, `editor` and `apps/editor` |
| `SONNET_BUILD_PLAYER` | `ON` | Builds `apps/player`, the generic runtime ([player.md](player.md)) |
| `SONNET_BUILD_TESTS` | `ON` | Builds Catch2 tests and registers them with CTest |
| `SONNET_BUILD_SAMPLES` | `ON` | Copies `apps/samples/` next to the editor binary, so `sonnet_editor samples/basic` works from the build directory |
| `SONNET_ENABLE_TRACY` | `ON` | Compiles Tracy zones in, and links Tracy, in Debug and RelWithDebInfo; Release never has them |
| `SONNET_ENABLE_VALIDATION` | `ON` | Requests Vulkan validation layers at instance creation in Debug |
| `SONNET_SANITIZERS` | `OFF` | Address and undefined-behaviour sanitizers (the `linux-asan` preset) |
| `SONNET_THREAD_SANITIZER` | `OFF` | Thread sanitizer (the `linux-tsan` preset); mutually exclusive with `SONNET_SANITIZERS` |
| `SONNET_COVERAGE` | `OFF` | gcov instrumentation for engine modules only |

Global flags: `CMAKE_CXX_STANDARD 23`, extensions off. Warnings are an interface target, `sonnet::warnings`, that `sonnet_add_module` links privately with `COMPILE_WARNING_AS_ERROR`; vcpkg include directories are `SYSTEM`, so third-party headers never trip them. On GCC and Clang, `-fmacro-prefix-map` makes `__FILE__` and `std::source_location` repository-relative while debug info keeps absolute paths, so debuggers find the sources; `-ffile-prefix-map` would rewrite those too and breakpoints set from an IDE would never bind.

Per-configuration definitions applied by `sonnet_add_module`: `SONNET_ASSERTS_ENABLED` in Debug and RelWithDebInfo, `SPDLOG_ACTIVE_LEVEL` at `TRACE` there and `INFO` in Release, `SONNET_ENABLE_TRACY` together with the Tracy link. `SONNET_MODULE` is defined per target to its name and feeds the logging macros ([core.md](core.md#logging)).

## Module helpers

`cmake/functions.cmake` provides two functions so every module has the same shape:

- `sonnet_add_module(<name> SOURCES ... DEPENDS ... PUBLIC_DEPENDS ...)` creates the static library `sonnet_<name>` with alias `sonnet::<name>`, sets the include directory to `modules/<name>/include`, applies the shared warning flags, and links the declared dependencies. Dependencies are the only way a module reaches another, which is what enforces the one-way rule.
- `sonnet_add_module_test(<name> SOURCES ... DEPENDS ...)` creates `<name>_tests` linked against the module and `Catch2::Catch2WithMain`, registers it with CTest under the label `<name>` (so `ctest -L core` runs one module) with a five-minute timeout and `--allow-running-no-tests` (a suite whose every case skipped, such as `rhi_tests` without a Vulkan device, is a pass), compiles in `tests/support/TestSupport.cpp` (which turns Windows crash and assertion dialogs into stderr output and a non-zero exit), and allows including the module's `src/` directory for white-box tests.

- `sonnet_add_executable(<name> SOURCES ... DEPENDS ...)` creates the target `sonnet_<name>_app`, whose binary is `sonnet_<name>`, with the same flags and definitions as a module. The target name carries the suffix because an app shares its name with the module it fronts (`editor`), and the module owns `sonnet_<name>`. On Android the target is a shared library, `libsonnet_<name>.so`, since SDL's Java side loads the app with `System.loadLibrary` and calls its `SDL_main`.
- `sonnet_add_shaders(<target> SHADERS ...)` and `sonnet_add_engine_shaders(<target>)` compile Slang shaders next to a target's binary ([Shaders in the build](#shaders-in-the-build)).
- `sonnet_add_apk(<target> MANIFEST ... RESOURCES ... JAVA_SOURCES ... [BUNDLE ...])`, in `cmake/SonnetAndroid.cmake`, packages an Android shared library into a debug-signed APK ([Android](#android)).
- `sonnet_copy_tracy_client(<target>)`, applied by the test and executable helpers, copies `TracyClient.dll` next to Windows binaries. vcpkg's tracy port installs the Debug DLL under `debug/bin/Debug`, where vcpkg's own applocal copy step does not look, so a binary that references Tracy symbols would otherwise fail to start with `STATUS_DLL_NOT_FOUND` before reaching `main`, which no in-process setting can catch. `find_package(Tracy)` is `GLOBAL` for that reason.

The helpers live in `cmake/SonnetFunctions.cmake`; options are in `SonnetOptions.cmake`, warning flags in `SonnetWarnings.cmake` and the coverage target in `SonnetCoverage.cmake`.

`modules/CMakeLists.txt` adds the modules in dependency order, and that order is the canonical statement of the architecture. `ui` and `editor` are added only when `SONNET_BUILD_EDITOR` is on, which also builds `apps/cook`; `apps/player` follows `SONNET_BUILD_PLAYER`. `rhi` also exports the interface target `sonnet::rhi_vulkan` (its implementation headers and the Vulkan-HPP configuration), which only `ui` may link, for Dear ImGui's Vulkan backend.

## Shaders in the build

Engine shaders live in `modules/renderer/shaders/*.slang`; the rhi tests keep their own next to them. `sonnet_add_shaders(<target> SHADERS ...)` compiles each file to SPIR-V with `slangc` from the `shader-slang` port at build time and places the result in `shaders/` next to the target's binary, so a release build has no runtime compiler dependency. Every compilation sees the engine shader directory, so any file can `import sonnet`. The renderer module registers its entry-point files in the `SONNET_ENGINE_SHADERS` global property and `sonnet_add_engine_shaders(<target>)` compiles them next to the given executable or test, since a static library has no binary directory for them to land in. The editor additionally compiles at runtime for hot reload from M3. See [rendering.md](rendering.md#shaders).

## Building and testing

On Linux or Windows, `python3 tools/check_setup.py` checks a machine against this page before the first configure. On Linux it checks CMake, Ninja, a C++23 compiler, the X11 and Wayland headers the `sdl3` port builds against, vcpkg and the manifest's baseline, the system Vulkan loader, and a Lavapipe at 1.4 for the tests, choosing the manifest for the host's architecture; a last line gathers the missing packages into one `apt-get` command, and it prints the compiler to configure with twice: one for `linux-debug`, `linux-release` and `linux-coverage`, and a Clang that ships the sanitizer runtime for `linux-asan` and `linux-tsan`. On Windows it checks CMake, Ninja, git, a Visual Studio installation with the C++ workload (17.10+) through `vswhere`, vcpkg and the manifest's baseline, the system Vulkan loader and a GPU reaching 1.4 (a case that needs one skips itself otherwise, as it does on the `windows-latest` CI runner rather than failing the check), and prints the `Launch-VsDevShell.ps1` call that puts MSVC on `PATH` before `cmake --preset windows-debug` (also `windows-release`); missing tools are named one `winget install --id ... -e` line each. It installs nothing, and exits 1 when something required is missing. Each line names its fix. It lists the existing build directories with the compiler each was configured with, and flags one whose vcpkg triplet no longer matches its preset's. Validation layers, an interactive session, clang-format 20, the commit hook, and (Linux only) a sanitizer runtime, gcovr, and too little free disk or memory per core are warnings: the build does without them, or only some presets need them. In Claude Code, `/setup-sonnet` (`.claude/skills/setup-sonnet/`) runs the check and walks through the fixes. It then configures, builds, runs the tests (on Lavapipe on Linux), takes an editor screenshot and, if asked to, writes the VS Code configuration. It leaves `sudo`, `winget install` and the Visual Studio Installer to the user.

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

# run the editor on a sample project
./build/linux-debug/apps/editor/sonnet_editor apps/samples/basic
# run the player on the same project, or on a bundle cooked from it
./build/linux-debug/apps/player/sonnet_player apps/samples/basic
./build/linux-debug/apps/cook/sonnet_cook apps/samples/basic --out /tmp/basic
./build/linux-debug/apps/player/sonnet_player /tmp/basic/game.sbundle
```

## Android

`cmake/SonnetAndroid.cmake` packages the player into an APK without Gradle ([ADR-0018](decisions/0018-mobile-export.md#packaging)). It is adapted from SDL's `SdlAndroidFunctions.cmake`, whose zlib licence it carries. `apps/player/CMakeLists.txt` calls `sonnet_add_apk(sonnet_player_app ...)` on Android only, which adds the target `sonnet_player_apk`, part of the default build, producing `build/android-*/apps/player/sonnet_player.apk`. The player library still builds alone as `sonnet_player_app`. The steps:

1. `aapt2` compiles `apps/player/android/res/` and links it with `apps/player/android/AndroidManifest.xml` against `platforms/android-36/android.jar`. In Debug and RelWithDebInfo, which both presets are, the APK is debuggable, so `adb shell run-as` works.
2. `javac --release 17` compiles SDL's Java glue, which the `sdl3` overlay port installs into `share/sdl3/android-java/` for Android triplets ([ports/README.md](../ports/README.md)), with `SonnetActivity.java`. The release flag gives the same bytecode from the JDK 17 on CI and a newer local JDK.
3. `d8` dexes the classes.
4. `zip` adds `classes.dex`, then stores `lib/arm64-v8a/libsonnet_player.so`, stripped with the NDK's `llvm-strip` (the unstripped library stays in the build directory for symbols), `assets/shaders/` and, when given, `assets/game.sbundle`, all uncompressed, so they are read in place.
5. `zipalign -P 16 4` aligns the entries and puts the library on a 16 KB page, which the manifest's `extractNativeLibs="false"` relies on.
6. `apksigner` signs with `build/android-*/android/debug.keystore`, which `keytool` generates on the first build and keeps. Each build directory has its own key, so switching an installed APK between `android-debug` and `android-release` needs `adb uninstall io.github.pacheco95.sonnet` first.

Prerequisites, besides the NDK:

- The Android SDK with `platforms;android-36` and `build-tools;36.1.0` (`sdkmanager "platforms;android-36" "build-tools;36.1.0"`), and `ANDROID_HOME` pointing at it. The configure caches `ANDROID_HOME`, so a later regeneration does not need the environment. `SONNET_ANDROID_BUILD_TOOLS_VERSION` selects another build-tools version.
- A JDK 17 or later: `javac`, `java` and `keytool` from `JAVA_HOME/bin`, or from `PATH`. `d8` and `apksigner` run on the same JDK.
- `zip`.

A missing one fails the configure with a message naming it.

The APK carries no game unless a cooked bundle is given: `-DSONNET_ANDROID_BUNDLE=<path>/game.sbundle` at configure, or `BUNDLE` in the call. The build does not cook. Cooking for Android needs ASTC, and the player cannot yet read from the APK, both later M9 steps ([roadmap.md](roadmap.md#the-android-apk)).

```bash
export VCPKG_ROOT=$HOME/vcpkg ANDROID_NDK_HOME=$HOME/Android/ndk/30.0.16248370 ANDROID_HOME=$HOME/Android
cmake --preset android-debug
cmake --build --preset android-debug            # the library and the APK
$ANDROID_HOME/platform-tools/adb install -r build/android-debug/apps/player/sonnet_player.apk
```

Running it is in [player.md](player.md#running-on-android).

## Continuous integration

GitHub Actions, one workflow with a matrix:

- `ubuntu-latest` (GCC and Clang), `windows-latest` (MSVC), `macos-latest` (Apple Clang): configure, build, run tests.
- The same toolchains, plus clang-cl on `windows-latest`, again in plain Release: the `*-release` preset with `CMAKE_BUILD_TYPE=Release`. That is the configuration that compiles `SONNET_ASSERT` out, and warnings are errors, so a value only an assertion reads, or a warning only GCC's `-O3` inlining finds, fails there first ([#36](https://github.com/Pacheco95/sonnet/issues/36)).
- Vulkan-dependent tests run on Linux under Lavapipe (Mesa's CPU Vulkan implementation, which supports 1.4) so the renderer is exercised without a GPU.
- `linux-asan` job on every pull request.
- An Android job on `ubuntu-24.04` installs NDK r30 (the runner's default, r27.3, cannot target API 36), configures `android-release`, builds the player library and then the debug-signed APK with the runner's JDK 17 and build-tools 36.1.0, and uploads the APK as the `sonnet_player-android` artifact. Cooking the sample into it waits for ASTC ([ADR-0018](decisions/0018-mobile-export.md#ci)).
- vcpkg binary caching through the GitHub Actions cache so dependency builds are not repeated.
- A lint job runs first, and every build matrix job waits for it to pass; a lint failure skips the entire build matrix. It runs `clang-format --dry-run` on every tracked source (`.clang-format` lists only the differences from LLVM style, so it parses with clang-format 18 and newer; CI uses 20), `tools/check_docs.py`, `tools/check_version.py` (the manifest mirrors the CMake version) and, on pull requests, `tools/check_commit_msg.py` over the new commits. `clang-tidy` runs on the changed sources of a pull request in the Linux Clang job using the build's `compile_commands.json`.
- Linux runners install Mesa from the kisak PPA so Lavapipe exposes Vulkan 1.4, and the system libraries SDL3's X11 and Wayland features need, including the headers of the X11 extensions the `sdl3` overlay port enables (XInput2, Xcursor, Xfixes, XRandR, XScrnSaver); SDL's configure fails, naming the package, when an enabled extension's header is missing, so a developer machine needs the same packages. Tests run with `VK_DRIVER_FILES` pointing at Lavapipe. CI takes the first `lvp_icd*.json` it finds, which is right on its runners because they have only the 64-bit Mesa. On a machine that also has the 32-bit one, `lvp_icd.i686.json` sorts first and a 64-bit test process finds no device, so `tools/check_setup.py` picks the manifest named for the host's architecture instead. Tests that need a window ask `platform` for a headless instance, which uses SDL's offscreen video driver.

Tests that need a Vulkan device skip themselves when none is present, which is the case on the Windows and macOS runners.

`tools/install_hooks.sh` installs the `commit-msg` hook that runs the same commit-message check locally.

## Coverage

`linux-coverage` builds engine modules with `--coverage`, excludes third-party code, and the `coverage` target runs the tests and gcovr to produce `build/linux-coverage/coverage/index.html`. gcovr is pointed at the reader that matches the compiler, `gcov-<major>` for GCC and `llvm-cov-<major> gcov` for Clang: each GCC major version writes its own data format, and a plain `gcov` is the system GCC's (13 on Ubuntu 24.04), which rejects GCC 14's data.
