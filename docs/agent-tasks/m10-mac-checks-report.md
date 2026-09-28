# M10 Mac-only checks report

Run on 2026-09-28, on `agents/m10-mac-checks`, following [the task](m10-mac-checks.md). Output below is verbatim except trimmed trailing whitespace and privacy replacements: `<home>`, `<name>` and `<team>`. Full build logs and the PNG remain local build artifacts.

## 0. Setup

Fetched origin and reset this task branch to its remote head. The Git name and email matched both required values exactly; the name is redacted below to satisfy the task’s privacy rule. `VCPKG_ROOT` was set to `$HOME/vcpkg`, which the presets use. `CC` and `CXX` were unset for every task command.

```text
$ git rev-parse HEAD
76bf0fd30defbf37c9eda656130f8a9a174495fb

$ git config user.name
<name>

$ git config user.email
mdpgd95@gmail.com

$ sw_vers
ProductName:		macOS
ProductVersion:		26.7
BuildVersion:		25G229

$ xcodebuild -version
Xcode 26.3
Build version 17C529

$ xcrun --sdk iphoneos --show-sdk-version
26.2

$ cmake --version
cmake version 4.2.1

$ <home>/vcpkg/vcpkg version
vcpkg package management program version 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8
```

## 1. macOS export

The first configure exited 1 while compiling `vk-bootstrap` against `/usr/local/include/vulkan/vulkan_core.h`. Its complete compiler failure is in section 4. The retry selected the Xcode macOS SDK explicitly and exited 0. No source, port, preset or CMake file was changed.

```text
unset CC CXX
export VCPKG_ROOT="$HOME/vcpkg"
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
cmake --preset macos-debug -DCMAKE_C_COMPILER="$(xcrun -f clang)" -DCMAKE_CXX_COMPILER="$(xcrun -f clang++)" -DCMAKE_OSX_SYSROOT="$SDKROOT"
```

```text
Waiting for 1 remaining binary cache submissions...
Completed submission of vulkan-memory-allocator-hpp:arm64-osx@3.4.0 to 1 binary cache(s) in 18.8 ms (1/1)
All requested installations completed successfully in: 11 s
-- Running vcpkg install - done
-- The CXX compiler identification is AppleClang 17.0.0.17000604
-- The C compiler identification is AppleClang 17.0.0.17000604
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Sonnet 0.11.0: rhi=Vulkan editor=ON player=ON tests=ON samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Found Python3: <home>/.pyenv/shims/python3.13 (found version "3.13.7") found components: Interpreter
-- Found Stb: <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/include
-- Found nlohmann_json: <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version "3.12.0")
-- Configuring done (20.3s)
-- Generating done (0.1s)
-- Build files have been written to: <home>/repositories/sonnet/build/macos-debug
exit: 0
```

```text
$ cmake --build --preset macos-debug --target sonnet_player_app sonnet_cook_app
[92/96] Building CXX object apps/player/CMakeFiles/sonnet_player_app.dir/main.cpp.o
[93/96] Building CXX object modules/scripting/CMakeFiles/sonnet_scripting.dir/src/LuaScriptRuntime.cpp.o
[94/96] Linking CXX static library modules/scripting/libsonnet_scripting.a
[95/96] Linking CXX static library modules/runtime/libsonnet_runtime.a
[96/96] Linking CXX executable apps/player/sonnet_player; Copying player binary next to editor for export
exit: 0
```

```text
$ ./build/macos-debug/apps/cook/sonnet_cook apps/samples/basic --platform macos --out build/macos-bundle
[14:46:08.522] [info] [assets] [Bundle.cpp:334] wrote build/macos-bundle/game.sbundle (27 assets, 4 files, 683600 bytes)
[14:46:08.522] [info] [assets] [Cook.cpp:195] cooked "Basic" for macos: 27 assets, 4 scenes and prefabs, 0 warnings
exit: 0
```

Assembled `build/macos-export` with only `sonnet_player`, `shaders/` and `game.sbundle`, using the task’s copy commands. Ran from that directory with all seven listed SDK/loader environment variables removed, plus `VULKAN_SDK`. Used an absolute screenshot path because relative player capture paths resolve under Application Support, not the working directory. Subprocess return codes replace the Bash-only `PIPESTATUS` example.

```text
$ ./sonnet_player --screenshot <home>/repositories/sonnet/build/macos-export-screenshot.png --settle-frames 5
[14:46:09.520] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1280x720 pixels
[14:46:09.524] [debug] [platform] [Platform.cpp:68] Vulkan loader "<home>/repositories/sonnet/build/macos-export/sonnet_player" kept mapped for the process
[14:46:09.525] [warn] [rhi] [VulkanDevice.cpp:202] validation requested but VK_LAYER_KHRONOS_validation is not installed
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
[14:46:09.530] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple M4 Max", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[14:46:10.856] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[14:46:10.858] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[14:46:10.858] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[14:46:10.859] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[14:46:13.142] [info] [player] [main.cpp:94] capture run on "Apple M4 Max", Vulkan 1.4.357, writing <home>/repositories/sonnet/build/macos-export-screenshot.png
[14:46:13.143] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at <home>/repositories/sonnet/build/macos-export/game.sbundle (27 assets, 4 files, cooked for macos by 0.11.0)
[14:46:13.484] [info] [runtime] [Screenshot.cpp:75] screenshot 1280x720 written to <home>/repositories/sonnet/build/macos-export-screenshot.png
[14:46:13.523] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
exit: 0
```

```text
$ ls -la build/macos-export-screenshot.png
-rw-r--r--  1 <name>  staff  319753 Sep 28 14:46 build/macos-export-screenshot.png

exit: 0
```

The PNG exists and was opened for visual inspection. The full capture log is `build/macos-export-run.log`; the excerpt above includes every warning.

## 2. iOS cooked bundle

```text
$ ./build/macos-debug/apps/cook/sonnet_cook apps/samples/basic --platform ios --out build/ios-bundle
[14:46:13.839] [info] [assets] [Bundle.cpp:334] wrote build/ios-bundle/game.sbundle (27 assets, 4 files, 703249 bytes)
[14:46:13.839] [info] [assets] [Cook.cpp:195] cooked "Basic" for ios: 27 assets, 4 scenes and prefabs, 0 warnings
exit: 0
```

```text
$ cmake --preset ios-debug -DSONNET_IOS_BUNDLE=<home>/repositories/sonnet/build/ios-bundle/game.sbundle
-- Detecting C compile features
-- Detecting C compile features - done
-- Sonnet 0.11.0: rhi=Vulkan editor=OFF player=ON tests=OFF samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Found Stb: <home>/repositories/sonnet/build/ios-debug/vcpkg_installed/arm64-ios/include
-- Found nlohmann_json: <home>/repositories/sonnet/build/ios-debug/vcpkg_installed/arm64-ios/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version "3.12.0")
-- The OBJCXX compiler identification is AppleClang 17.0.0.17000604
-- Detecting OBJCXX compiler ABI info
-- Detecting OBJCXX compiler ABI info - done
-- Check for working OBJCXX compiler: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++ - skipped
-- Configuring done (86.6s)
-- Generating done (0.1s)
-- Build files have been written to: <home>/repositories/sonnet/build/ios-debug
exit: 0
```

```text
$ cmake --build --preset ios-debug -- CODE_SIGNING_ALLOWED=NO
    cd <home>/repositories/sonnet
    /usr/bin/touch -c <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app

PhaseScriptExecution Generate\ CMakeFiles/ALL_BUILD <home>/repositories/sonnet/build/ios-debug/build/sonnet.build/Debug-iphoneos/ALL_BUILD.build/Script-C8CE0288B8D45A117769F5BC.sh (in target 'ALL_BUILD' from project 'sonnet')
    cd <home>/repositories/sonnet
    /bin/sh -c <home>/repositories/sonnet/build/ios-debug/build/sonnet.build/Debug-iphoneos/ALL_BUILD.build/Script-C8CE0288B8D45A117769F5BC.sh
Build all projects

note: Run script build phase 'Generate apps/samples/CMakeFiles/sonnet_samples' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_samples' from project 'sonnet')
note: Run script build phase 'Generate apps/player/CMakeFiles/sonnet_player_app_shaders' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app_shaders' from project 'sonnet')
note: Run script build phase 'Generate CMakeFiles/ZERO_CHECK' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ZERO_CHECK' from project 'sonnet')
note: Run script build phase 'Generate CMakeFiles/ALL_BUILD' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ALL_BUILD' from project 'sonnet')
note: Run script build phase 'CMake PostBuild Rules' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app' from project 'sonnet')
** BUILD SUCCEEDED **

exit: 0
```

```text
$ find build/ios-debug -name sonnet_player.app
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app
build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app

exit: 0
```

```text
$ find build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources -maxdepth 2
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/debug.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/skybox.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/ibl.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/post.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/depth.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/cull.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/cluster.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/skin.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/outline.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/forward.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/shaders/id.spv
build/ios-debug/apps/player/Debug${EFFECTIVE_PLATFORM_NAME}/sonnet_player.app/Resources/game.sbundle
exit: 0

$ find build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app/Resources -maxdepth 2
find: build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app/Resources: No such file or directory
exit: 1
```

The find command returned two directories, so Resources was listed separately for each. The actual `Debug-iphoneos/sonnet_player.app` has no Resources directory. The shaders and game bundle were copied under the literal `Debug${EFFECTIVE_PLATFORM_NAME}` directory instead. Stopped this section without modifying CMake. Full output from the post-build phase through validation follows:

```text
PhaseScriptExecution CMake\ PostBuild\ Rules <home>/repositories/sonnet/build/ios-debug/build/sonnet_player_app.build/Debug-iphoneos/Script-E497154619C417021372C8E1.sh (in target 'sonnet_player_app' from project 'sonnet')
    cd <home>/repositories/sonnet
    /bin/sh -c <home>/repositories/sonnet/build/ios-debug/build/sonnet_player_app.build/Debug-iphoneos/Script-E497154619C417021372C8E1.sh

RegisterExecutionPolicyException <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app (in target 'sonnet_player_app' from project 'sonnet')
    cd <home>/repositories/sonnet
    builtin-RegisterExecutionPolicyException <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app

Validate <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app (in target 'sonnet_player_app' from project 'sonnet')
    cd <home>/repositories/sonnet
    builtin-validationUtility <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app -shallow-bundle -infoplist-subpath Info.plist

Touch <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app (in target 'sonnet_player_app' from project 'sonnet')
    cd <home>/repositories/sonnet
    /usr/bin/touch -c <home>/repositories/sonnet/build/ios-debug/apps/player/Debug-iphoneos/sonnet_player.app
```

## 3. Automatic signing

One team was available in Xcode. The first signing attempt mistakenly used the enclosing account key from Xcode’s cached team dictionary and exited 65 (no account/provisioning profile). Retried with the actual `teamID` field; results below are from that corrected attempt.

```text
$ cmake --preset ios-release -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<team>
  find_package(vk-bootstrap CONFIG REQUIRED)
  target_link_libraries(main PRIVATE vk-bootstrap::vk-bootstrap vk-bootstrap::vk-bootstrap-compiler-warnings)

vulkan-memory-allocator-hpp provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)

All requested installations completed successfully in: 162 us
-- Running vcpkg install - done
-- Sonnet 0.11.0: rhi=Vulkan editor=OFF player=ON tests=OFF samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Configuring done (5.4s)
-- Generating done (0.1s)
-- Build files have been written to: <home>/repositories/sonnet/build/ios-release
exit: 0
```

```text
$ cmake --build --preset ios-release -- -allowProvisioningUpdates -allowProvisioningDeviceRegistration
    cd <home>/repositories/sonnet
    /usr/bin/touch -c <home>/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app

PhaseScriptExecution Generate\ CMakeFiles/ALL_BUILD <home>/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ALL_BUILD.build/Script-65814F6636F930831F18EBC5.sh (in target 'ALL_BUILD' from project 'sonnet')
    cd <home>/repositories/sonnet
    /bin/sh -c <home>/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ALL_BUILD.build/Script-65814F6636F930831F18EBC5.sh
Build all projects

note: Run script build phase 'Generate apps/samples/CMakeFiles/sonnet_samples' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_samples' from project 'sonnet')
note: Run script build phase 'Generate apps/player/CMakeFiles/sonnet_player_app_shaders' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app_shaders' from project 'sonnet')
note: Run script build phase 'Generate CMakeFiles/ZERO_CHECK' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ZERO_CHECK' from project 'sonnet')
note: Run script build phase 'Generate CMakeFiles/ALL_BUILD' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ALL_BUILD' from project 'sonnet')
note: Run script build phase 'CMake PostBuild Rules' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app' from project 'sonnet')
** BUILD SUCCEEDED **

exit: 0
```

```text
$ codesign -dv --verbose=4 build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app
Executable=<home>/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app/sonnet_player
Identifier=io.github.pacheco95.sonnet
Format=app bundle with Mach-O thin (arm64)
CodeDirectory v=20400 size=30622 flags=0x0(none) hashes=946+7 location=embedded
VersionPlatform=2
VersionMin=1049344
VersionSDK=1704448
Hash type=sha256 size=32
CandidateCDHash sha256=ae67327b092531b3569a3d3d906b16ffae9144e2
CandidateCDHashFull sha256=ae67327b092531b3569a3d3d906b16ffae9144e2feb708a1a8bad2b7f8bb397c
Hash choices=sha256
CMSDigest=ae67327b092531b3569a3d3d906b16ffae9144e2feb708a1a8bad2b7f8bb397c
CMSDigestType=2
Executable Segment base=0
Executable Segment limit=10649600
Executable Segment flags=0x11
Page size=16384
CDHash=ae67327b092531b3569a3d3d906b16ffae9144e2
Signature size=4828
Authority=Apple Development: <name> (<team>)
Authority=Apple Worldwide Developer Relations Certification Authority
Authority=Apple Root CA
Signed Time=28 Sep 2026 at 14:49:52
Info.plist entries=25
TeamIdentifier=<team>
Sealed Resources version=2 rules=10 files=4
Internal requirements count=1 size=208
exit: 0
```

The signing inspection targets the actual `RelWithDebInfo-iphoneos` app. An inspection of the extra literal `${EFFECTIVE_PLATFORM_NAME}` resource directory exited 1 because it is not an executable bundle. No iPhone installation or launch was attempted.

## 4. Deviations and initial configure failure

The checkout began on `main`; switched once to the user-requested task branch before reading its task file, and stayed there. The two pre-existing untracked root logs were left untouched. Used the task’s `macos-debug` preset with explicit Xcode compiler and SDK paths rather than the local notes’ `macos-debug-local` preset, so all task artifact paths match. The first configure had the explicit compiler paths but no explicit SDK selection.

The following is the full initial dependency build failure, with home paths redacted:

```text
Change Dir: '<home>/vcpkg/buildtrees/vk-bootstrap/arm64-osx-dbg'

Run Build Command(s): /opt/homebrew/bin/ninja -v -v -j15 install
[1/3] /usr/bin/c++  -I<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src -isystem <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/include -fPIC -g -std=gnu++17 -arch arm64 -Wall -Wextra -Wconversion -Wsign-conversion -MD -MT CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -MF CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o.d -o CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -c <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp
FAILED: [code=1] CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o
/usr/bin/c++  -I<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src -isystem <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/include -fPIC -g -std=gnu++17 -arch arm64 -Wall -Wextra -Wconversion -Wsign-conversion -MD -MT CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -MF CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o.d -o CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -c <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9139:5: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 9139 |     PFN_vkGetLatencyTimingsLegacyNV fp_vkGetLatencyTimingsLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkGetLatencyTimingsNV
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9402:5: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 9402 |     PFN_vkGetSleepStatusLegacyNV fp_vkGetSleepStatusLegacyNV = nullptr;
      |     ^
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9493:5: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 9493 |     PFN_vkLatencySleepLegacyNV fp_vkLatencySleepLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkLatencySleepNV
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9536:5: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 9536 |     PFN_vkQueueNotifyOutOfBandLegacyNV fp_vkQueueNotifyOutOfBandLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkQueueNotifyOutOfBandNV
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9674:5: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 9674 |     PFN_vkSetLatencyMarkerLegacyNV fp_vkSetLatencyMarkerLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkSetLatencyMarkerNV
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9684:5: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 9684 |     PFN_vkSetLatencySleepModeLegacyNV fp_vkSetLatencySleepModeLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkSetLatencySleepModeNV
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9714:5: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 9714 |     PFN_vkShutdownLatencyDeviceLegacyNV fp_vkShutdownLatencyDeviceLegacyNV = nullptr;
      |     ^
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:2954:59: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 2954 |         fp_vkGetLatencyTimingsLegacyNV = reinterpret_cast<PFN_vkGetLatencyTimingsLegacyNV>(procAddr(device, "vkGetLatencyTimingsLegacyNV"));
      |                                                           ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |                                                           PFN_vkGetLatencyTimingsNV
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3113:56: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 3113 |         fp_vkGetSleepStatusLegacyNV = reinterpret_cast<PFN_vkGetSleepStatusLegacyNV>(procAddr(device, "vkGetSleepStatusLegacyNV"));
      |                                                        ^
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3168:54: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 3168 |         fp_vkLatencySleepLegacyNV = reinterpret_cast<PFN_vkLatencySleepLegacyNV>(procAddr(device, "vkLatencySleepLegacyNV"));
      |                                                      ^~~~~~~~~~~~~~~~~~~~~~~~~~
      |                                                      PFN_vkLatencySleepNV
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3195:62: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 3195 |         fp_vkQueueNotifyOutOfBandLegacyNV = reinterpret_cast<PFN_vkQueueNotifyOutOfBandLegacyNV>(procAddr(device, "vkQueueNotifyOutOfBandLegacyNV"));
      |                                                              ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |                                                              PFN_vkQueueNotifyOutOfBandNV
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3281:58: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 3281 |         fp_vkSetLatencyMarkerLegacyNV = reinterpret_cast<PFN_vkSetLatencyMarkerLegacyNV>(procAddr(device, "vkSetLatencyMarkerLegacyNV"));
      |                                                          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |                                                          PFN_vkSetLatencyMarkerNV
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3287:61: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 3287 |         fp_vkSetLatencySleepModeLegacyNV = reinterpret_cast<PFN_vkSetLatencySleepModeLegacyNV>(procAddr(device, "vkSetLatencySleepModeLegacyNV"));
      |                                                             ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |                                                             PFN_vkSetLatencySleepModeNV
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:3305:63: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 3305 |         fp_vkShutdownLatencyDeviceLegacyNV = reinterpret_cast<PFN_vkShutdownLatencyDeviceLegacyNV>(procAddr(device, "vkShutdownLatencyDeviceLegacyNV"));
      |                                                               ^
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:5995:56: error: too few arguments to function call, expected 3, have 2
 5995 |         fp_vkGetLatencyTimingsLegacyNV(device, pTimings);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                 ^
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:6357:43: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'VkSemaphore' (aka 'VkSemaphore_T *')
 6357 |         fp_vkLatencySleepLegacyNV(device, signalSemaphore, value);
      |                                           ^~~~~~~~~~~~~~~
/usr/local/include/vulkan/vulkan_core.h:105:1: note: 'VkSemaphore_T' is not defined, but forward declared here; conversion would be valid if it was derived from 'VkSwapchainKHR_T'
  105 | VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
      | ^
/usr/local/include/vulkan/vulkan_core.h:56:74: note: expanded from macro 'VK_DEFINE_NON_DISPATCHABLE_HANDLE'
   56 |         #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
      |                                                                          ^
<scratch space>:92:1: note: expanded from here
   92 | VkSemaphore_T
      | ^
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from <home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:6406:50: error: cannot initialize a parameter of type 'const VkOutOfBandQueueTypeInfoNV *' with an lvalue of type 'uint32_t' (aka 'unsigned int')
 6406 |         fp_vkQueueNotifyOutOfBandLegacyNV(queue, queueType);
      |                                                  ^~~~~~~~~
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:6560:47: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'uint64_t' (aka 'unsigned long long')
 6560 |         fp_vkSetLatencyMarkerLegacyNV(device, frameID, marker);
      |                                               ^~~~~~~
<home>/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:6570:83: error: too many arguments to function call, expected 3, have 4
 6570 |         fp_vkSetLatencySleepModeLegacyNV(device, lowLatencyMode, lowLatencyBoost, minimumIntervalUs);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                                          ^~~~~~~~~~~~~~~~~
19 errors generated.
ninja: build stopped: subcommand failed.
```

```text
CMake Error at scripts/cmake/vcpkg_execute_build_process.cmake:134 (message):
    Command failed: <home>/vcpkg/downloads/tools/cmake-4.4.3-osx/cmake-4.4.3-macos-universal/CMake.app/Contents/bin/cmake --build . --config Debug --target install -- -v -j15
    Working Directory: <home>/vcpkg/buildtrees/vk-bootstrap/arm64-osx-dbg
    See logs for more information:
      <home>/vcpkg/buildtrees/vk-bootstrap/install-arm64-osx-dbg-out.log

Call Stack (most recent call first):
  <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/share/vcpkg-cmake/vcpkg_cmake_build.cmake:74 (vcpkg_execute_build_process)
  <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/arm64-osx/share/vcpkg-cmake/vcpkg_cmake_install.cmake:16 (vcpkg_cmake_build)
  buildtrees/versioning_/versions/vk-bootstrap/d37eaf5954a64eb83322593714026d982d491407/portfile.cmake:22 (vcpkg_cmake_install)
  scripts/ports.cmake:209 (include)


error: building vk-bootstrap:arm64-osx failed with: BUILD_FAILED
See https://learn.microsoft.com/vcpkg/troubleshoot/build-failures?WT.mc_id=vcpkg_inproduct_cli for more information.
Elapsed time to handle vk-bootstrap:arm64-osx: 1.6 s
Please ensure you're using the latest port files with `git pull` and `vcpkg update`.
Then check for known issues at:
  https://github.com/microsoft/vcpkg/issues?q=is%3Aissue+is%3Aopen+in%3Atitle+vk-bootstrap
You can submit a new issue at:
  https://github.com/microsoft/vcpkg/issues/new?title=%5Bvk-bootstrap%5D%20build%20error%20on%20arm64-osx&body=Copy%20issue%20body%20from%20<home>%2Frepositories%2Fsonnet%2Fbuild%2Fmacos-debug%2Fvcpkg_installed%2Fvcpkg%2Fissue_body.md
You can also submit an issue by running (GitHub CLI must be installed):
  /opt/homebrew/bin/gh issue create -R microsoft/vcpkg --title "[vk-bootstrap] Build failure on arm64-osx" --body-file <home>/repositories/sonnet/build/macos-debug/vcpkg_installed/vcpkg/issue_body.md
Completed submission of moltenvk:arm64-osx@1.4.2 to 1 binary cache(s) in 423 ms
-- Running vcpkg install - failed
CMake Error at <home>/vcpkg/scripts/buildsystems/vcpkg.cmake:984 (message):
  vcpkg install failed.  See logs for more information:
  <home>/repositories/sonnet/build/macos-debug/vcpkg-manifest-install.log
Call Stack (most recent call first):
  /opt/homebrew/share/cmake/Modules/CMakeDetermineSystem.cmake:146 (include)
  CMakeLists.txt:19 (project)


CMake Error: CMake was unable to find a build program corresponding to "Ninja".  CMAKE_MAKE_PROGRAM is not set.  You probably need to select a different build tool.
-- Configuring incomplete, errors occurred!
```

No engine or CMake changes were made. Only this report is committed.

Privacy checks: `grep -n /Users/` found no home paths; literal searches for the cached account/team identifiers and collected personal names all returned 1 (no matches). `python3 tools/check_docs.py` passed.
