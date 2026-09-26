# macOS release build report

Commit tested: `40ca7a6` (`fix(renderer): resolve cascaded shadow artifacts`).

## Build results

| Build | Preset and configuration | CMake C++ compiler | Xcode | macOS SDK | Build exit | Failed objects |
|---|---|---|---|---|---:|---:|
| RelWithDebInfo | `macos-release-local` (`RelWithDebInfo`) | Apple Clang 17.0.0 (`clang++`) | 26.3 (17C529) | 26.2 | 0 | 0 |
| Release | `macos-release-local` with `-DCMAKE_BUILD_TYPE=Release` | Apple Clang 17.0.0 (`clang++`) | 26.3 (17C529) | 26.2 | 1 | 2 |

The repository's `macos-release` preset leaves compiler selection to `PATH` and first selected Homebrew LLVM 22. The ignored machine-local `macos-release-local` preset pins Apple Clang from Xcode and the macOS SDK, so it was used for both builds. `c++ --version` from the compiler selected by CMake reports Apple clang version 17.0.0 (clang-1700.6.4.2).

The RelWithDebInfo build passed all 12 CTest suites: `assets_tests`, `audio_tests`, `core_tests`, `editor_tests`, `physics_tests`, `platform_tests`, `renderer_tests`, `rhi_tests`, `runtime_tests`, `scripting_tests`, `ui_tests`, and `world_tests`. The test run selected the installed Vulkan SDK loader with `DYLD_LIBRARY_PATH=~/VulkanSDK/1.4.341.1/macOS/lib`; the RHI run completed 39 cases and 4,487 assertions, including its Vulkan cases.

## Unique diagnostics

| Build | File and line | Warning flag | Source line | Reached from | Occurrences |
|---|---|---|---|---|---:|
| Release | `modules/rhi/src/OwnerThread.h:16` (`owner`) | `-Wunused-parameter` | `inline void assertOwnerThread(std::thread::id owner, std::string_view what) {` | Included from `modules/rhi/src/NullDevice.cpp:3` and `modules/rhi/src/VulkanDevice.cpp:3` | 2 |
| Release | `modules/rhi/src/OwnerThread.h:16` (`what`) | `-Wunused-parameter` | `inline void assertOwnerThread(std::thread::id owner, std::string_view what) {` | Included from `modules/rhi/src/NullDevice.cpp:3` and `modules/rhi/src/VulkanDevice.cpp:3` | 2 |
| Release | `modules/rhi/src/VulkanDevice.cpp:795` (`expected`) | `-Wunused-variable` | `const std::uint64_t expected = levelByteSize(desc.format, size);` | — | 1 |

## Failed objects

| Build | Target | Source file |
|---|---|---|
| Release | `sonnet_rhi` | `modules/rhi/src/NullDevice.cpp` |
| Release | `sonnet_rhi` | `modules/rhi/src/VulkanDevice.cpp` |

`grep -c '^FAILED:'` reported 0 for RelWithDebInfo and 2 for Release. The Release compiler diagnostics were:

```text
modules/rhi/src/OwnerThread.h:16:47: error: unused parameter 'owner' [-Werror,-Wunused-parameter]
modules/rhi/src/OwnerThread.h:16:71: error: unused parameter 'what' [-Werror,-Wunused-parameter]
modules/rhi/src/VulkanDevice.cpp:795:25: error: unused variable 'expected' [-Werror,-Wunused-variable]
ninja: build stopped: cannot make progress due to previous errors.
```

## Configuration and other failures

The initial configure using `macos-release` selected Homebrew LLVM 22 and completed, but did not select the required Apple Clang. Both final configurations using `macos-release-local` completed successfully. There were no link errors. The successful RelWithDebInfo links printed this linker warning three times:

```text
ld: warning: ignoring duplicate libraries: 'vcpkg_installed/arm64-osx/lib/libSDL3.a'
```

## Observations

RelWithDebInfo builds with Apple Clang 17.0.0; plain Release fails in two RHI objects. Its three distinct unused-value diagnostics account for five diagnostic occurrences because the two `OwnerThread.h` parameters are each reported from both including translation units.
