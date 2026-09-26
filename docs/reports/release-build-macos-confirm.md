# macOS release build confirmation

Commit tested: `2efd44a` (`ci: build plain Release with every supported toolchain`).

## Build results

| Build | Preset and configuration | CMake C++ compiler | Xcode | macOS SDK | Build exit | `FAILED:` lines |
|---|---|---|---|---|---:|---:|
| RelWithDebInfo | `macos-release-local` (`RelWithDebInfo`) | Apple Clang 17.0.0 (`clang++`) | 26.3 (17C529) | 26.2 | 0 | 0 |
| Release | `macos-release-local` with `-DCMAKE_BUILD_TYPE=Release` | Apple Clang 17.0.0 (`clang++`) | 26.3 (17C529) | 26.2 | 0 | 0 |
| Debug | `macos-debug-local` (`Debug`) | Apple Clang 17.0.0 (`clang++`) | 26.3 (17C529) | 26.2 | 0 | 0 |

CMake selected `/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++`; that path is shortened here to `XcodeDefault.xctoolchain/usr/bin/clang++`. The compiler reports Apple clang version 17.0.0 (clang-1700.6.4.2). Each build used a new directory under the ignored `build/` directory. The Release build that failed before this fix now completes without compiler errors.

All three builds emitted the same linker warning three times per configuration, with the Debug library path varying as shown:

```text
ld: warning: ignoring duplicate libraries: 'vcpkg_installed/arm64-osx/lib/libSDL3.a'
ld: warning: ignoring duplicate libraries: 'vcpkg_installed/arm64-osx/lib/libSDL3.a'
ld: warning: ignoring duplicate libraries: 'vcpkg_installed/arm64-osx/lib/libSDL3.a'
```

For Debug, each warning names `vcpkg_installed/arm64-osx/debug/lib/libSDL3.a`. There were no other compiler or linker diagnostics.

## Test results

CTest used the installed Vulkan SDK loader via `DYLD_LIBRARY_PATH=~/VulkanSDK/1.4.341.1/macOS/lib`. Each configuration passed all 12 suites: `core_tests`, `platform_tests`, `rhi_tests`, `renderer_tests`, `assets_tests`, `world_tests`, `physics_tests`, `scripting_tests`, `audio_tests`, `runtime_tests`, `ui_tests` and `editor_tests`.

The focused Release assertion test also passed:

```text
All tests passed (4 assertions in 2 test cases)
```

The asset tests locate the sample project relative to their executable. They therefore need build directories under the repository tree; the final fresh build directories were placed under the ignored `build/` folder, and all suites passed there.

## Result

On this Mac, Apple Clang 17.0.0 builds RelWithDebInfo, plain Release and Debug successfully. All three configurations pass all 12 CTest suites, and the new compiled-out assertion test passes in Release.
