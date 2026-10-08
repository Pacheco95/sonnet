# macOS Vulkan header search diagnosis

Date: 2026-10-08. Revision: `ac533cd`, after `5383d54` and base `618bc63`.
Validation used a fresh detached worktree at `/private/tmp/sonnet-mac-occlusion`.
Compiler: Apple Clang 17.0.0 (`clang-1700.6.4.2`), arm64, Xcode SDK 26.2.
No engine source, dependency, or build configuration was changed. No workaround was applied.

## Finding

Invoking `/usr/bin/clang++` adds `-I/usr/local/include` to the internal compiler command, although that flag is absent from the CMake compile command. An ordinary `-I` directory precedes vcpkg's `-isystem` directories. This explains why the system Vulkan headers (341) shadow the installed vcpkg headers (357).

The injection reproduces with an empty environment. Invoking `/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++` directly does not inject that flag, with either the current or an empty environment. Its search order puts the vcpkg `-isystem` directory before `/usr/local/include`.

This isolates the behavior to the `/usr/bin/clang++` invocation path. The internal implementation responsible for adding the flag was not inspected. No successful engine build with the direct compiler is claimed.

## Validation results

| Step | Result |
|---|---|
| Configure `macos-debug` | Pass; vcpkg packages restored from binary cache |
| Build `macos-debug` | Fail; `VulkanDevice.cpp` and `VulkanSwapchain.cpp` each produced 19 errors |
| CTest | 13 tests not run because executables were missing; shader check failed because SPIR-V files were missing |
| Direct renderer tests and benchmark | Blocked; renderer executable missing |
| Screenshots and pixel comparison | Blocked; no editor/player binary; off variant skipped |
| Statistics panel | Source contains the text, guarded by `visibleCountsKnown`; runtime appearance unverified |

No survivor counts, GPU or CPU timings, submission-time comparison, or pixel differences were measured. Vulkan validation and Metal shader compilation were not exercised.

## 1. Environment

Filtered with `env | grep -i -E 'cpath|cplus|c_include|include|vulkan|sdk|prefix'`:

```text
CMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm
CPPFLAGS=-I/opt/homebrew/opt/llvm/include
HOMEBREW_PREFIX=/opt/homebrew
NVM_INC=~/.nvm/versions/node/v24.15.0/include/node
```

`CPATH`, `CPLUS_INCLUDE_PATH` and `C_INCLUDE_PATH` were unset.

## 2. Exact compile command

From `build/macos-debug/compile_commands.json`:

```text
/usr/bin/clang++ -DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_EXPLICIT_CTOR -DGLM_FORCE_RADIANS -DSONNET_ASSERTS_ENABLED=1 -DSONNET_ENABLE_TRACY=1 -DSONNET_ENABLE_VALIDATION=1 -DSONNET_MODULE=\"rhi\" -DSONNET_RHI_VULKAN=1 -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DTRACY_ENABLE -DVK_NO_PROTOTYPES -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0 -I/private/tmp/sonnet-mac-occlusion/modules/rhi/include -I/private/tmp/sonnet-mac-occlusion/modules/rhi/src -I/private/tmp/sonnet-mac-occlusion/modules/platform/include -I/private/tmp/sonnet-mac-occlusion/modules/core/include -isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include -isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include/tracy -isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/share/unofficial-vulkan-memory-allocator-hpp/../../include -g -std=c++2b -arch arm64 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wcast-align -Wunused -Wformat=2 -Wno-c2y-extensions -fmacro-prefix-map=/private/tmp/sonnet-mac-occlusion/= -Werror -Winvalid-pch -Xclang -include-pch -Xclang /private/tmp/sonnet-mac-occlusion/build/macos-debug/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch -Xclang -include -Xclang /private/tmp/sonnet-mac-occlusion/build/macos-debug/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx -o modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o -c /private/tmp/sonnet-mac-occlusion/modules/rhi/src/VulkanDevice.cpp
```

Include-related flags in order (no `-iquote`):

```text
-I/private/tmp/sonnet-mac-occlusion/modules/rhi/include
-I/private/tmp/sonnet-mac-occlusion/modules/rhi/src
-I/private/tmp/sonnet-mac-occlusion/modules/platform/include
-I/private/tmp/sonnet-mac-occlusion/modules/core/include
-isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include
-isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include/tracy
-isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/share/unofficial-vulkan-memory-allocator-hpp/../../include
-Xclang -include-pch -Xclang /private/tmp/sonnet-mac-occlusion/build/macos-debug/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch
-Xclang -include -Xclang /private/tmp/sonnet-mac-occlusion/build/macos-debug/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx
```

## 3. Bare compiler search

Command: `c++ -E -v -x c++ /dev/null`.

```text
#include "..." search starts here:
#include <...> search starts here:
 /usr/local/include
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1
 /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/17/include
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
 /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks (framework directory)
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/SubFrameworks (framework directory)
End of search list.
```

## 4. Effective compile search

The exact command above, dropping `-o` and its operand and `-c`, then adding `-E -v`:

```text
#include "..." search starts here:
#include <...> search starts here:
 /private/tmp/sonnet-mac-occlusion/modules/rhi/include
 /private/tmp/sonnet-mac-occlusion/modules/rhi/src
 /private/tmp/sonnet-mac-occlusion/modules/platform/include
 /private/tmp/sonnet-mac-occlusion/modules/core/include
 /usr/local/include
 /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include
 /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include/tracy
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1
 /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/17/include
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
 /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks (framework directory)
 /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/SubFrameworks (framework directory)
End of search list.
```

## 5. Precompiled header

The command includes `-include-pch` for `build/macos-debug/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch`. Searching the top-level `build/macos-debug/CMakeFiles` found no PCH references; the files live under the module directory.

The generated `cmake_pch.hxx` includes `<vulkan/vulkan_raii.hpp>`. A diagnostic `-H` replay of that header shows:

```text
. /usr/local/include/vulkan/vulkan_raii.hpp
.. /usr/local/include/vulkan/vulkan.hpp
... /usr/local/include/vulkan/vulkan.h
.... /usr/local/include/vulkan/vk_platform.h
```

The existing PCH-backed compilation also reports declarations from `/usr/local/include/vulkan/vulkan_core.h`. A replay of `VulkanDevice.cpp` with PCH use removed for diagnosis selects the same system headers and fails. The PCH incorporates the wrong headers; it is not necessary to reproduce the wrong search order.

Both files existed during diagnosis:

| Header | Version |
|---|---|
| `build/macos-debug/vcpkg_installed/arm64-osx/include/vulkan/vulkan_core.h` | 357 |
| `/usr/local/include/vulkan/vulkan_core.h` | 341 |

## 6. Clean environment and object build

Repeating step 4 with `env -u CPATH -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH` produced an identical search block.

Command:

```sh
env -u CPATH -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH ninja -C build/macos-debug modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o
```

Result: fail, exit 1, 19 compiler errors. First error:

```text
/private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9139:5: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
```

## Compiler invocation isolation

`/usr/bin/clang++ -### -E -x c++ -isystem /private/tmp/sonnet-mac-occlusion/build/macos-debug/vcpkg_installed/arm64-osx/include /dev/null` exposes this added argument in the internal `-cc1` command:

```text
"-I/usr/local/include"
```

| Invocation | Injects the flag |
|---|---|
| `/usr/bin/clang++`, empty environment | Yes |
| Direct Xcode toolchain compiler, current environment | No |
| Direct Xcode toolchain compiler, empty environment | No |

The empty-environment probes used Python `subprocess.run(..., env={})`. Both compiler paths report the same Apple Clang version. These observations exclude the three include environment variables as the cause and identify the invocation path that changes precedence.

## CTest output

The incomplete build produced these failures; they do not establish runtime test regressions:

```text
The following tests FAILED:
	  1 - core_tests (Not Run)                              core
	  2 - platform_tests (Not Run)                          platform
	  3 - rhi_tests (Not Run)                               rhi
	  4 - rhi_tests_vulkan_1_3 (Not Run)                    rhi
	  5 - renderer_tests (Not Run)                          renderer
	  6 - renderer_spirv_for_adreno (Failed)                renderer
	  7 - assets_tests (Not Run)                            assets
	  8 - world_tests (Not Run)                             world
	  9 - physics_tests (Not Run)                           physics
	 10 - scripting_tests (Not Run)                         scripting
	 11 - audio_tests (Not Run)                             audio
	 12 - runtime_tests (Not Run)                           runtime
	 13 - ui_tests (Not Run)                                ui
	 14 - editor_tests (Not Run)                            editor
Errors while running CTest

```

```text
no .spv modules in /private/tmp/sonnet-mac-occlusion/build/macos-debug/modules/renderer/shaders
```
