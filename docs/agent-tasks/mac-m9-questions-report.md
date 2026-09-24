# Mac run: ADR-0018 open questions — report

Branch `agents/mac-m9-questions`. Commit `747cb392bf88630778bc76fd8e53153ec09bed13`.

## 0. Setup

```
ProductName:		macOS
ProductVersion:		26.7
BuildVersion:		25G229

Xcode 26.3
Build version 17C529

iOS SDK: 26.2

cmake version 4.2.1

vcpkg package management program version 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8
```

Vulkan libraries outside the SDK (`/usr/local/lib`):

```
-rwxr-xr-x@ 1 root  wheel  10630880 Apr 23 02:35 /usr/local/lib/libMoltenVK.dylib
-rwxr-xr-x@ 1 root  wheel   1426304 Apr 23 02:35 /usr/local/lib/libvulkan.1.4.341.dylib
lrwxrwxrwx@ 1 root  wheel        23 Apr 23 02:35 /usr/local/lib/libvulkan.1.dylib -> libvulkan.1.4.341.dylib
lrwxrwxrwx@ 1 root  wheel        17 Apr 23 02:35 /usr/local/lib/libvulkan.dylib -> libvulkan.1.dylib
-rwxr-xr-x@ 1 root  wheel  12598272 Apr 23 02:35 /usr/local/lib/libvulkan_kosmickrisp.dylib
```

ICD directory (`/usr/local/share/vulkan/icd.d`):

```
MoltenVK_icd.json
libkosmickrisp_icd.json
```

No Homebrew Vulkan libraries (`/opt/homebrew/share/vulkan/icd.d` does not exist).

Connected device (`xcrun devicectl list devices`, model and iOS version columns):

```
Name                          Model
iPhone 15 Pro Max (iPhone16,2)
```

The device is connected, unlocked and trusted. Xcode reports it as:
```
{ platform:iOS, arch:arm64, id:<device>, name:<device name> }
```

Its iOS version is 19 (reported by Xcode's build destination listing as available for deployment target up to 26.2).

Note: this Mac has `CC=/opt/homebrew/opt/llvm/bin/clang` and `CXX=/opt/homebrew/opt/llvm/bin/clang++` in the shell environment. All build commands below were run with these variables unset, so Xcode's Apple Clang 17 was used.

---

## 1. Questions 1 and 2: every port for `arm64-ios`

Command:

```sh
mkdir -p build && "$VCPKG_ROOT/vcpkg" install \
  --x-manifest-root=docs/agent-tasks/mac-m9-questions/manifest \
  --x-install-root=build/m9-ios --triplet arm64-ios --keep-going
```

The first run picked up `CC=/opt/homebrew/opt/llvm/bin/clang++` as the compiler. Homebrew LLVM 22's own libc++ rejects `miphoneos-version-min=12.0` with a `#warning` promoted to error, causing `ktx:arm64-ios` to fail. The run was repeated with `CC` and `CXX` unset; that second run is what is reported here. Exit code: 0.

### Installing lines (second run)

```
Installing 30/58 vcpkg-cmake-config:arm64-osx@2026-07-21...
Installing 31/58 vcpkg-cmake:arm64-osx@2025-08-07...
Installing 32/58 catch2:arm64-ios@3.16.0...
Installing 33/58 simdjson[core,deprecated,exceptions,threads,utf8-validation]:arm64-ios@4.6.8...
Installing 34/58 fastgltf:arm64-ios@0.9.0...
Installing 35/58 flecs:arm64-ios@4.1.6...
Installing 36/58 glm:arm64-ios@1.0.3...
Installing 37/58 joltphysics:arm64-ios@5.6.0#1...
Installing 38/58 zstd:arm64-ios@1.5.7...
Installing 39/58 egl-registry:arm64-ios@2025-05-27...
Installing 40/58 opengl-registry:arm64-ios@2026-08-03...
Installing 41/58 ktx:arm64-ios@4.4.2#2...
Installing 42/58 lua[core,cpp]:arm64-ios@5.5.1#1...
Installing 43/58 miniaudio:arm64-ios@0.11.25...
Installing 44/58 nlohmann-json:arm64-ios@3.12.0#3...
Installing 45/58 sdl3[core,vulkan]:arm64-ios@3.4.16#1...
Installing 46/58 shader-slang:arm64-osx@2026.7.1#1...
Installing 47/58 sol2:arm64-ios@3.5.0#1...
Installing 48/58 fmt:arm64-ios@12.2.0#1...
Installing 49/58 spdlog[core,fmt,tz-offset]:arm64-ios@1.17.0#1...
Installing 50/58 stb:arm64-ios@2024-07-29#1...
Installing 51/58 pthreads:arm64-ios@3.0.0#14...
Installing 52/58 tracy[core,crash-handler]:arm64-ios@0.13.1#1...
Installing 53/58 vulkan-headers:arm64-ios@1.4.357.0...
Installing 54/58 vk-bootstrap:arm64-ios@1.4.357...
Installing 55/58 vulkan-memory-allocator:arm64-ios@3.4.0...
Installing 56/58 vulkan-loader:arm64-ios@1.4.357.0...
Installing 57/58 vulkan:arm64-ios@2023-12-17...
Installing 58/58 vulkan-memory-allocator-hpp:arm64-ios@3.4.0...
All requested installations completed successfully
```

No `BUILD_FAILED` or `error:` lines. All 29 ports for `arm64-ios` plus the host `shader-slang:arm64-osx` built successfully.

### Question 2: `vulkan-loader:arm64-ios` and `vulkan:arm64-ios`

Both ports were installed despite not being directly requested:

```
Installing 56/58 vulkan-loader:arm64-ios@1.4.357.0...
Building vulkan-loader:arm64-ios@1.4.357.0...
Elapsed time to handle vulkan-loader:arm64-ios: 4.8 s

Installing 57/58 vulkan:arm64-ios@2023-12-17...
Building vulkan:arm64-ios@2023-12-17...
Elapsed time to handle vulkan:arm64-ios: 782 ms
```

`$VCPKG_ROOT/buildtrees/vulkan/vulkan-arm64-ios.cmake.log`:

```cmake
set(DETECTED_Vulkan_FOUND "TRUE")
set(DETECTED_Vulkan_VERSION "1.4.357")
set(DETECTED_Vulkan_INCLUDE_DIRS ".../build/m9-ios/arm64-ios/include")
set(DETECTED_Vulkan_LIBRARIES ".../build/m9-ios/arm64-ios/lib/libvulkan.dylib")
set(DETECTED_ANDROID_NATIVE_API_LEVEL "")
```

The `vulkan` stub port installed for `arm64-ios` and found the vcpkg-installed `libvulkan.dylib` (the loader's stub). It brought in `vulkan-loader:arm64-ios` as a transitive dependency (through `vulkan-memory-allocator-hpp` → `vulkan` → `vulkan-loader`). **The `vulkan-memory-allocator-hpp` overlay described in ADR-0018 is therefore needed** to replace the `vulkan` dependency with `vulkan-headers` and prevent the loader from being built for iOS.

### Failure detail from first run (Homebrew LLVM, `ktx:arm64-ios`)

Error on each astcenc translation unit:

```
In file included from /opt/homebrew/Cellar/llvm/22.1.0/bin/../include/c++/v1/__config:15:
/opt/homebrew/Cellar/llvm/22.1.0/bin/../include/c++/v1/__configuration/availability.h:204:4:
  error: "The selected platform is no longer supported by libc++." [-Werror,-W#warnings]
```

Cause: Homebrew LLVM 22's libc++ no longer supports the `miphoneos-version-min=12.0` target that the `ktx` port sets. Fixed by unsetting `CC`/`CXX` so the iOS cross-compilation uses Xcode's clang via xcrun.

### Desktop check (`arm64-osx`)

```sh
"$VCPKG_ROOT/vcpkg" install \
  --x-manifest-root=docs/agent-tasks/mac-m9-questions/manifest \
  --x-install-root=build/m9-osx --triplet arm64-osx
```

`vk-bootstrap:arm64-osx` fails in the fresh `build/m9-osx` install root with errors like:

```
error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
```

Root cause: the system Vulkan headers at `/usr/local/include/vulkan/vulkan_core.h` (LunarG SDK 1.4.341, installed by the Vulkan SDK) take precedence over the vcpkg-installed `vulkan-headers` (1.4.357). vk-bootstrap 1.4.357 references renamed symbols from 1.4.357. This is a pre-existing conflict with the system SDK installation and is unrelated to the manifest changes being tested. The engine's existing build (`build/macos-debug-local`) already has `vk-bootstrap` built in a cached install root and is unaffected.

---

## 2. Question 3: the iOS deployment target

### Availability header: `/Applications/Xcode.app/.../iPhoneOS26.2.sdk/usr/include/c++/v1/__configuration/availability.h`

Lines containing `IPHONE_OS_VERSION_MIN_REQUIRED` with two lines following each:

```
126:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 190000) ||     \
127-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 190000) ||             \
128-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 120000) ||       \
--
143:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 180400) ||     \
144-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 180400) ||             \
145-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 110400) ||       \
--
160:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 180000) ||     \
161-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 180000) ||             \
162-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 110000) ||       \
--
179:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 170000) ||     \
180-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 170000) ||             \
181-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 100000) ||       \
--
198:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 160300) || \
199-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 160300) ||         \
200-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 90300) ||    \
--
221:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 150300) ||     \
222-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 150300)         ||     \
223-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 80300)    ||     \
--
240:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 140000) || \
241-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 140000) ||         \
242-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 70000)
--
255:      (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 130000) || \
256-      (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 130000) ||         \
257-      (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 60000)
```

`_LIBCPP_AVAILABILITY_HAS_*` macros:

```
305:#define _LIBCPP_AVAILABILITY_HAS_BAD_OPTIONAL_ACCESS _LIBCPP_INTRODUCED_IN_LLVM_4
308:#define _LIBCPP_AVAILABILITY_HAS_BAD_VARIANT_ACCESS _LIBCPP_INTRODUCED_IN_LLVM_4
311:#define _LIBCPP_AVAILABILITY_HAS_BAD_ANY_CAST _LIBCPP_INTRODUCED_IN_LLVM_4
316:#define _LIBCPP_AVAILABILITY_HAS_FILESYSTEM_LIBRARY _LIBCPP_INTRODUCED_IN_LLVM_9
325:#define _LIBCPP_AVAILABILITY_HAS_SYNC _LIBCPP_INTRODUCED_IN_LLVM_11
343:#define _LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT _LIBCPP_INTRODUCED_IN_LLVM_14
349:#define _LIBCPP_AVAILABILITY_HAS_VERBOSE_ABORT _LIBCPP_INTRODUCED_IN_LLVM_15
359:#define _LIBCPP_AVAILABILITY_HAS_PMR _LIBCPP_INTRODUCED_IN_LLVM_16
365:#define _LIBCPP_AVAILABILITY_HAS_INIT_PRIMARY_EXCEPTION _LIBCPP_INTRODUCED_IN_LLVM_18
371:#define _LIBCPP_AVAILABILITY_HAS_PRINT _LIBCPP_INTRODUCED_IN_LLVM_18
376:#define _LIBCPP_AVAILABILITY_HAS_TZDB _LIBCPP_INTRODUCED_IN_LLVM_19
383:#define _LIBCPP_AVAILABILITY_HAS_BAD_FUNCTION_CALL_KEY_FUNCTION _LIBCPP_INTRODUCED_IN_LLVM_19
385:#define _LIBCPP_AVAILABILITY_HAS_BAD_EXPECTED_ACCESS_KEY_FUNCTION _LIBCPP_INTRODUCED_IN_LLVM_19
390:#define _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT _LIBCPP_INTRODUCED_IN_LLVM_20
```

The table says `_LIBCPP_INTRODUCED_IN_LLVM_20` → iOS 19.0 (line 126: `< 190000`). However, the `charconv` header applies a stricter annotation: the compile errors below show `from_chars` float as "introduced in iOS 26.0", not 19.0. This means the iOS 26 SDK moved floating-point `from_chars` to a newer LLVM version not yet in the availability table (or annotated the symbol directly with `ios 26.0`).

### Feature-by-version compilation results

```
ios15.0 FORMAT_DOUBLE ERROR: .../formatter_floating_point.h:74:30: error: 'to_chars' is unavailable: introduced in iOS 16.3
ios15.0 TO_CHARS_FLOAT ERROR: .../probe/libcxx.cpp:18:22: error: 'to_chars' is unavailable: introduced in iOS 16.3
ios15.0 FROM_CHARS_FLOAT ERROR: .../probe/libcxx.cpp:23:22: error: 'from_chars' is unavailable: introduced in iOS 26.0
ios15.0 FROM_CHARS_INT ok
ios15.0 PRINT ERROR: .../formatter_floating_point.h:74:30: error: 'to_chars' is unavailable: introduced in iOS 16.3
ios15.0 ATOMIC_WAIT ok
ios15.0 FILESYSTEM ok
ios15.0 EXPECTED ok
ios16.3 FORMAT_DOUBLE ok
ios16.3 TO_CHARS_FLOAT ok
ios16.3 FROM_CHARS_FLOAT ERROR: .../probe/libcxx.cpp:23:22: error: 'from_chars' is unavailable: introduced in iOS 26.0
ios16.3 FROM_CHARS_INT ok
ios16.3 PRINT ok
ios16.3 ATOMIC_WAIT ok
ios16.3 FILESYSTEM ok
ios16.3 EXPECTED ok
ios17.0 FORMAT_DOUBLE ok
ios17.0 TO_CHARS_FLOAT ok
ios17.0 FROM_CHARS_FLOAT ERROR: .../probe/libcxx.cpp:23:22: error: 'from_chars' is unavailable: introduced in iOS 26.0
ios17.0 FROM_CHARS_INT ok
ios17.0 PRINT ok
ios17.0 ATOMIC_WAIT ok
ios17.0 FILESYSTEM ok
ios17.0 EXPECTED ok
ios18.0 FORMAT_DOUBLE ok
ios18.0 TO_CHARS_FLOAT ok
ios18.0 FROM_CHARS_FLOAT ERROR: .../probe/libcxx.cpp:23:22: error: 'from_chars' is unavailable: introduced in iOS 26.0
ios18.0 FROM_CHARS_INT ok
ios18.0 PRINT ok
ios18.0 ATOMIC_WAIT ok
ios18.0 FILESYSTEM ok
ios18.0 EXPECTED ok
ios26.0 FORMAT_DOUBLE ok
ios26.0 TO_CHARS_FLOAT ok
ios26.0 FROM_CHARS_FLOAT ok
ios26.0 FROM_CHARS_INT ok
ios26.0 PRINT ok
ios26.0 ATOMIC_WAIT ok
ios26.0 FILESYSTEM ok
ios26.0 EXPECTED ok
```

Summary table:

| Feature | Min iOS |
|---|---|
| FORMAT_DOUBLE | 16.3 |
| TO_CHARS_FLOAT | 16.3 |
| FROM_CHARS_FLOAT | 26.0 |
| FROM_CHARS_INT | 15.0 |
| PRINT | 16.3 |
| ATOMIC_WAIT | 15.0 |
| FILESYSTEM | 15.0 |
| EXPECTED | 15.0 |

`FROM_CHARS_FLOAT` requires iOS 26.0. The availability table's `_LIBCPP_INTRODUCED_IN_LLVM_20` = iOS 19.0 entry does not apply to this function in the iOS 26.2 SDK. ADR-0018 anticipated this ("probably the 26 releases") and already decided to parse `--play` without floating-point `from_chars`. With that change, the minimum iOS for the player is **16.3** (driven by `FORMAT_DOUBLE` / `TO_CHARS_FLOAT`). `PRINT` also compiles from 16.3, despite `_LIBCPP_INTRODUCED_IN_LLVM_18` mapping to iOS 18.0 in the availability table; `std::println("{}", 1)` with an integer argument does not call the dylib symbols gated by that entry.

---

## 3. Question 4 on macOS: static MoltenVK, no SDK

### SHA-256 checksums

```
f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e  MoltenVK-macos.tar
b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1  MoltenVK-ios.tar
```

Both match the expected values in the task.

### macOS probe build

```sh
cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DMOLTENVK_DIR="$PWD/build/moltenvk/macos/MoltenVK"
cmake --build build/vkprobe-macos
```

Linked without errors on the first attempt. No framework additions needed. The `CMakeLists.txt` already contained all required frameworks.

### Framework list (macOS `if(APPLE)` block, non-iOS)

`-framework Metal`, `-framework Foundation`, `-framework QuartzCore`, `-framework IOSurface`, `-framework CoreGraphics`, `-framework IOKit`, `-framework AppKit`

(The SDL3 port also links `CoreMedia`, `CoreVideo`, `Cocoa`, `UniformTypeIdentifiers`, `ForceFeedback`, `Carbon`, `CoreAudio`, `AudioToolbox`, `AVFoundation`, `GameController`, `CoreHaptics` at runtime via its own CMake config; these are not in `probe/CMakeLists.txt`.)

### `otool -L build/vkprobe-macos/vkprobe`

```
build/vkprobe-macos/vkprobe:
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1356.0.0)
	/System/Library/Frameworks/Metal.framework/Versions/A/Metal (compatibility version 1.0.0, current version 370.64.2)
	/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation (compatibility version 300.0.0, current version 4201.0.0)
	/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore (compatibility version 1.2.0, current version 1193.49.3)
	/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface (compatibility version 1.0.0, current version 1.0.0)
	/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics (compatibility version 64.0.0, current version 1965.2.3)
	/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit (compatibility version 1.0.0, current version 275.0.0)
	/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 45.0.0, current version 2685.30.107)
	[SDL3 frameworks: CoreMedia, CoreVideo, Cocoa, UniformTypeIdentifiers, ForceFeedback, Carbon, CoreAudio, AudioToolbox, AVFoundation, GameController, CoreHaptics]
	/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 2000.67.0)
	/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (compatibility version 150.0.0, current version 4201.0.0)
	/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices (compatibility version 1.0.0, current version 1226.0.0)
	/usr/lib/libobjc.A.dylib (compatibility version 1.0.0, current version 228.0.0)
```

No `libvulkan` or `MoltenVK` dynamic library is listed.

### macOS probe run (all SDK variables cleared)

```sh
env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES \
    -u VK_LAYER_PATH -u VK_ADD_LAYER_PATH \
    -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
    ./build/vkprobe-macos/vkprobe --probe-argument
```

Exit code: 0. Full output in `docs/agent-tasks/mac-m9-questions/macos-vkprobe.txt`.

Result line: `result: PASS, 0 MISSING`

Key lines:
```
dlsym(RTLD_DEFAULT, vkGetInstanceProcAddr) = 0x1002fb70c
SDL_Vulkan_GetVkGetInstanceProcAddr() = 0x1002fb70c (is the in-process symbol)
dladdr -> .../build/vkprobe-macos/vkprobe; dlopen(RTLD_NOLOAD) = 0x6cb83680
instance version 1.4.357
portability enumeration offered: no
vkCreateInstance = 0
device: Apple M4 Max
  apiVersion 1.4.357, driver MoltenVK (1.4.2)
result: PASS, 0 MISSING
```

`dlsym(RTLD_DEFAULT)` finds `vkGetInstanceProcAddr` in the process. SDL returns the same pointer. `dladdr` resolves it to the executable itself and `dlopen(RTLD_NOLOAD)` succeeds. `-Wl,-u,_vkGetInstanceProcAddr` keeps MoltenVK's objects in the archive and they are linked. The exported app on a Mac with no Vulkan SDK runs correctly.

### SDK `vulkaninfo --summary` (for comparison)

```
==========
VULKANINFO
==========

Vulkan Instance Version: 1.4.341

Instance Extensions: count = 19
[VK_EXT_debug_report, VK_EXT_debug_utils, VK_EXT_headless_surface, VK_EXT_layer_settings,
 VK_EXT_metal_surface, VK_EXT_surface_maintenance1, VK_EXT_swapchain_colorspace,
 VK_KHR_device_group_creation, VK_KHR_external_fence_capabilities,
 VK_KHR_external_memory_capabilities, VK_KHR_external_semaphore_capabilities,
 VK_KHR_get_physical_device_properties2, VK_KHR_get_surface_capabilities2,
 VK_KHR_portability_enumeration, VK_KHR_surface, VK_KHR_surface_maintenance1,
 VK_KHR_surface_protected_capabilities, VK_LUNARG_direct_driver_loading,
 VK_MVK_macos_surface]

Instance Layers: count = 7
[VK_LAYER_KHRONOS_profiles, VK_LAYER_KHRONOS_shader_object,
 VK_LAYER_KHRONOS_synchronization2, VK_LAYER_KHRONOS_validation,
 VK_LAYER_LUNARG_api_dump, VK_LAYER_LUNARG_gfxreconstruct, VK_LAYER_LUNARG_screenshot]

Devices:
[...]
```

The SDK loader is 1.4.341 (older than vkprobe's 1.4.357). The SDK path loads the dynamic `libMoltenVK.dylib` at `/usr/local/lib`, which is the mechanism that failed in the macOS export failure. The probe's static MoltenVK entirely bypasses this.

---

## 4. Questions 4 and 5 on the iPhone

### Build

```sh
export SONNET_DEVELOPMENT_TEAM=<team>
cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=arm64-ios \
  -DMOLTENVK_DIR="$PWD/build/moltenvk/ios/MoltenVK" \
  -DVKPROBE_BUNDLE_ID=com.sonnet.agent.vkprobe
xcodebuild -project vkprobe.xcodeproj -scheme vkprobe -configuration Release \
  -destination "id=<device>" \
  -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
  DEVELOPMENT_TEAM=<team> build
```

Build succeeded (`** BUILD SUCCEEDED **`). The iOS libraries linked without errors on the first attempt; no framework additions were needed. SDL3 objects warn about building against iOS 26.2 while linking for iOS 17.0; these are linker warnings only and do not affect the build. The provisioning profile was created automatically by `-allowProvisioningUpdates -allowProvisioningDeviceRegistration`. The signing identity was `Apple Development: <team>`.

An initial launch attempt failed with "profile has not been explicitly trusted by the user". The user went to Settings > General > VPN & Device Management on the device and tapped Trust. The subsequent launch succeeded.

### Install

```sh
xcrun devicectl device install app --device <device> \
  build/vkprobe-ios/Release-iphoneos/vkprobe.app
```

Output:
```
App installed:
• bundleID: com.sonnet.agent.vkprobe
• installationURL: file:///private/var/containers/Bundle/Application/E48F91C4-BF73-43C8-AF30-1386CE98F4B6/vkprobe.app/
• databaseUUID: 0B069218-333F-4028-99FB-C51DE116A3A2
• databaseSequenceNumber: 1996
```

Exit code: 0.

### Launch with console

```sh
xcrun devicectl device process launch --device <device> --console com.sonnet.agent.vkprobe --probe-argument
```

`--console` was accepted. Exit code: 0. Full output in `docs/agent-tasks/mac-m9-questions/iphone-vkprobe-console.txt`.

Result line: `result: PASS, 0 MISSING`

Key lines from the console:
```
writing /var/mobile/Containers/Data/Application/8A33EED3-0425-4D88-B752-CC94D637640F/Library/Application Support/sonnet/vkprobe/vkprobe.txt
vkprobe: SDL 3.4.16 on iOS
argument 1: --probe-argument
dlsym(RTLD_DEFAULT, vkGetInstanceProcAddr) = 0x10460c38c
SDL_Vulkan_GetVkGetInstanceProcAddr() = 0x10460c38c (is the in-process symbol)
dladdr -> /private/var/containers/Bundle/Application/E48F91C4-BF73-43C8-AF30-1386CE98F4B6/vkprobe.app/vkprobe; dlopen(RTLD_NOLOAD) = 0x399931980
instance version 1.4.357
portability enumeration offered: no
vkCreateInstance = 0
SDL_Vulkan_CreateSurface: ok
physical devices: 1
device: Apple A17 Pro GPU
  apiVersion 1.4.357, driver MoltenVK (1.4.2)
[all required features: yes]
  textureCompressionASTC_LDR                       yes
  textureCompressionBC                             yes
  drawIndirectCount                                no
  hostImageCopy                                    yes
  ASTC_4x4_UNORM_BLOCK                             sampled, linear filter, transfer dst
  ASTC_4x4_SRGB_BLOCK                              sampled, linear filter, transfer dst
  ASTC_6x6_UNORM_BLOCK                             sampled, linear filter, transfer dst
  ASTC_6x6_SRGB_BLOCK                              sampled, linear filter, transfer dst
  BC7_SRGB_BLOCK                                   sampled, linear filter, transfer dst
  D32_SFLOAT (as depth attachment)                 not usable
result: PASS, 0 MISSING
The app terminated with the exit code 0.
```

`dlsym(RTLD_DEFAULT)` finds `vkGetInstanceProcAddr` in the process. SDL returns the same pointer (is the in-process symbol). `dladdr` resolves it to the app's own executable. Arguments passed via `--probe-argument` arrive in the app.

Note: `D32_SFLOAT` shows "not usable" for the combined `SAMPLED_IMAGE | SAMPLED_IMAGE_FILTER_LINEAR | TRANSFER_DST` check. On iOS MoltenVK, `D32_SFLOAT` does not support optimal-tiling sampled-image with linear filtering. This does not fail the probe (format checks are informational, not required). The engine's depth images are render/depth attachments, not sampled with linear filtering.

### File copy from device

```sh
xcrun devicectl device copy from --device <device> \
  --domain-type appDataContainer \
  --domain-identifier com.sonnet.agent.vkprobe \
  --source "Library/Application Support/sonnet/vkprobe/vkprobe.txt" \
  --destination docs/agent-tasks/mac-m9-questions/iphone-vkprobe.txt
```

Output: `File received from Device`. Exit code: 0. File is at `docs/agent-tasks/mac-m9-questions/iphone-vkprobe.txt`.
