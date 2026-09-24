# Mac follow-up run: M9 (depth formats, iOS version, fresh-build failure)

## 0. Setup

```
git rev-parse HEAD
e78ac8054088cf5c4269dbca38c63eacc94c900d

git config user.name
Michael Pacheco

git config user.email
mdpgd95@gmail.com
```

```
sw_vers
ProductName:		macOS
ProductVersion:		26.7
BuildVersion:		25G229

xcodebuild -version
Xcode 26.3
Build version 17C529

echo "VULKAN_SDK=$VULKAN_SDK"
VULKAN_SDK=

"$VCPKG_ROOT/vcpkg" version | head -1
vcpkg package management program version 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8
```

`VULKAN_SDK` was not set in the environment.

## 1. The iPhone's iOS version

```
xcrun devicectl list devices
Name                  Hostname                                      Identifier                             State       Model
--------------------  -------------------------------------------   ------------------------------------   ---------   ------------------------------
<device name>         <device name>.coredevice.local                <device>                               connected   iPhone 15 Pro Max (iPhone16,2)
```

```
xcrun devicectl device info details --device <device> 2>&1 | grep -i -E 'osVersion|productVersion|buildVersion|marketingName|productType'
    • marketingName: iPhone 15 Pro Max
    • productType: iPhone16,2
    • thinningProductType: iPhone16,2
    • osVersionNumber: 27.0
```

The iOS version is **27.0**. The earlier run returned "19" from a different field; `osVersionNumber` is the correct one.

## 2. Depth formats on the iPhone and the Mac

MoltenVK 1.4.2 SHA-256:

```
shasum -a 256 build/moltenvk/MoltenVK-macos.tar build/moltenvk/MoltenVK-ios.tar
f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e  build/moltenvk/MoltenVK-macos.tar
b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1  build/moltenvk/MoltenVK-ios.tar
```

Both match.

### macOS (Apple M4 Max) — grep output

```
device: Apple M4 Max
  D32_SFLOAT           attachment yes, sampled yes, depth comparison yes, linear filter yes, transfer dst yes (0x40020000de01)
  D16_UNORM            attachment yes, sampled yes, depth comparison yes, linear filter yes, transfer dst yes (0x40020000de01)
result: PASS, 0 MISSING
```

Full output: `docs/agent-tasks/mac-m9-followup/macos-vkprobe.txt`

### iPhone (Apple A17 Pro GPU) — grep output

```
device: Apple A17 Pro GPU
  D32_SFLOAT           attachment yes, sampled yes, depth comparison yes, linear filter no, transfer dst yes (0x40020000ce01)
  D16_UNORM            attachment yes, sampled yes, depth comparison yes, linear filter yes, transfer dst yes (0x40020000de01)
result: PASS, 0 MISSING
```

Full output: `docs/agent-tasks/mac-m9-followup/iphone-vkprobe-console.txt`

### Deviation from the task's iOS build command

The task says `DEVELOPMENT_TEAM=<team>`. Two team IDs are in play on this machine. The signing certificate in the keychain belongs to one team (`<team-cert>`); the provisioning profile for `com.sonnet.agent.vkprobe` from the earlier run, found in `~/Library/Developer/Xcode/UserData/Provisioning Profiles/`, belongs to a second team (`<team-profile>`). The first attempt passed `<team-cert>` as `DEVELOPMENT_TEAM`; xcodebuild rejected it with "No Account for Team". The second attempt passed `<team-profile>`; xcodebuild found the profile, signed the app with the certificate it held, and the build succeeded. Everything else was as written.

## 3. The fresh `vk-bootstrap` build

### 3.1 Apple Clang header search path

```
echo | xcrun clang -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search list/p'
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

ls -la /usr/local/include/vulkan/vulkan_core.h
-rw-r--r--@ 1 root  wheel  1265235 Jan 30  2026 /usr/local/include/vulkan/vulkan_core.h
```

`/usr/local/include/vulkan/vulkan_core.h` has `VK_HEADER_VERSION 341`.

### 3.2 With `VULKAN_SDK` as set (empty)

Exit code: **1** (vcpkg reported `BUILD_FAILED`).

`FAILED:` line and compiler command from the build log:

```
FAILED: [code=1] CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o
/usr/bin/c++  -I~/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src -isystem ~/repositories/sonnet/build/vkb-check-a/arm64-osx/include -fPIC -g -std=gnu++17 -arch arm64 -Wall -Wextra -Wconversion -Wsign-conversion -MD -MT CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -MF CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o.d -o CMakeFiles/vk-bootstrap.dir/src/VkBootstrap.cpp.o -c ~/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp
```

First error:

```
In file included from ~/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.cpp:17:
In file included from ~/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrap.h:44:
~/vcpkg/buildtrees/vk-bootstrap/src/v1.4.357-335b7bd585.clean/src/VkBootstrapDispatch.h:9139:5: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 9139 |     PFN_vkGetLatencyTimingsLegacyNV fp_vkGetLatencyTimingsLegacyNV = nullptr;
      |     ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |     PFN_vkGetLatencyTimingsNV
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
```

### 3.3 With `VULKAN_SDK` explicitly unset

Exit code: **1** (same `BUILD_FAILED`). The `FAILED:` line, compiler command and first error are identical to 3.2; only the install root differs (`vkb-check-b` instead of `vkb-check-a`).

`VULKAN_SDK` was already unset, so both runs are equivalent. The cause is not `VULKAN_SDK`: `/usr/local/include` is the first directory in Apple Clang's default system include search path, and the vcpkg install root (`-isystem .../vkb-check-a/arm64-osx/include`) is added after it. The SDK's 1.4.341 headers in `/usr/local/include/vulkan` are found before vcpkg's 1.4.357 ones in both runs.
