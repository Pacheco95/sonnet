# Mac run: M9 follow-up (depth formats, iOS version, fresh-build failure)

Task branch `agents/mac-m9-followup`. It is not for merging. It is `docs/adr-m9-mobile` plus the earlier Mac task and report (`mac-m9-questions`), this file and `mac-m9-followup/`. The earlier run left three things open, recorded in [roadmap.md](../roadmap.md#checked-before-the-code):

1. **The iPhone's `D32_SFLOAT`.** `vkprobe` reported it "not usable" on the iPhone 15 Pro Max, but it tested three bits together. The shadow cascades need the format as a depth attachment that is sampled with depth comparison. The Vulkan spec asks `SAMPLED_IMAGE_DEPTH_COMPARISON` of a comparison sampler and `SAMPLED_IMAGE_FILTER_LINEAR` only of a sampler that does not compare (`VUID-vkCmdDraw-None-06479`, `VUID-vkCmdDraw-magFilter-04553`). The probe now prints each bit of `D32_SFLOAT` and `D16_UNORM` separately, from `VkFormatProperties3`.
2. **The iPhone's iOS version**, which the earlier report gave as "19".
3. **A fresh `vk-bootstrap` build for `arm64-osx` failed** with `unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'`. It is suspected to be the Vulkan SDK's 1.4.341 headers in `/usr/local/include/vulkan` shadowing the manifest's 1.4.357 ones, either through the compiler's default search path or through `VULKAN_SDK`. This run tells the two apart. See [roadmap.md](../roadmap.md#a-fresh-macos-build-fails-where-the-vulkan-sdk-installed-its-headers-system-wide).

Report **raw output**, not conclusions. Paste command output verbatim.

**Do not switch branches.** Write the report on this branch and push this branch.

**Privacy.** The repository is public. Never commit or paste the Apple team ID, the iPhone's UDID or CoreDevice identifier, its serial number, **its device name** (it is a person's name), or `/Users/<name>` paths. Write `<team>`, `<device>`, `<device name>` and `~` in their place, in the report, in pasted output and in commit messages. Section 4 greps for them before committing.

Sections 1 and 2 need the iPhone 15 Pro Max connected by USB, unlocked and trusting this Mac, with Developer Mode on and the developer profile trusted (Settings > General > VPN & Device Management), as for the earlier run. Section 3 does not need it. If the iPhone is not available, do section 3 and say so.

Build with Apple Clang from Xcode, never Homebrew LLVM: unset `CC` and `CXX` in the shell for every command of this task. Homebrew LLVM 22's libc++ rejects the iOS targets.

## 0. Setup

1. `git fetch origin && git checkout agents/mac-m9-followup && git reset --hard origin/agents/mac-m9-followup`, then report `git rev-parse HEAD`.
2. Report `git config user.name` and `git config user.email`. They must be `Michael Pacheco` and `mdpgd95@gmail.com`. If they are not, stop and say so. Do not commit anything.
3. `unset CC CXX`, then report `sw_vers`, `xcodebuild -version`, `echo "VULKAN_SDK=$VULKAN_SDK"`, and `"$VCPKG_ROOT/vcpkg" version | head -1`.

## 1. The iPhone's iOS version

`xcrun devicectl list devices`, to find the device. Then `xcrun devicectl device info details --device <device> 2>&1 | grep -i -E 'osVersion|productVersion|buildVersion|marketingName|productType'`. Paste only those lines, with identifiers replaced.

## 2. Depth formats on the iPhone and the Mac

The probe is `docs/agent-tasks/mac-m9-questions/probe/`. Its new lines start with ` depth formats`.

1. Download and check MoltenVK 1.4.2, as the earlier task did. The expected SHA-256 sums are unchanged.

   ```sh
   mkdir -p build/moltenvk && cd build/moltenvk
   gh release download v1.4.2 -R KhronosGroup/MoltenVK -p MoltenVK-macos.tar -p MoltenVK-ios.tar --clobber
   shasum -a 256 MoltenVK-macos.tar MoltenVK-ios.tar
   # expected: f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e  MoltenVK-macos.tar
   #           b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1  MoltenVK-ios.tar
   mkdir -p macos ios && tar xf MoltenVK-macos.tar -C macos && tar xf MoltenVK-ios.tar -C ios && cd ../..
   ```

2. macOS: build as before, then run it with the Vulkan SDK variables cleared and save its output:

   ```sh
   cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=arm64-osx \
     -DMOLTENVK_DIR="$PWD/build/moltenvk/macos/MoltenVK"
   cmake --build build/vkprobe-macos
   env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_ADD_LAYER_PATH \
     -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH ./build/vkprobe-macos/vkprobe 2>&1 \
     | tee docs/agent-tasks/mac-m9-followup/macos-vkprobe.txt | grep -E 'device:|D32_SFLOAT|D16_UNORM|result'
   ```

3. iOS: build, install and launch as before, with the bundle identifier of the earlier run:

   ```sh
   export SONNET_DEVELOPMENT_TEAM=<team>
   cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-ios -G Xcode \
     -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=arm64-ios \
     -DMOLTENVK_DIR="$PWD/build/moltenvk/ios/MoltenVK" -DVKPROBE_BUNDLE_ID=com.sonnet.agent.vkprobe
   xcodebuild -project build/vkprobe-ios/vkprobe.xcodeproj -scheme vkprobe -configuration Release \
     -destination "id=<device>" -allowProvisioningUpdates -allowProvisioningDeviceRegistration \
     DEVELOPMENT_TEAM=<team> build
   xcrun devicectl device install app --device <device> build/vkprobe-ios/Release-iphoneos/vkprobe.app
   xcrun devicectl device process launch --device <device> --console com.sonnet.agent.vkprobe 2>&1 \
     | tee docs/agent-tasks/mac-m9-followup/iphone-vkprobe-console.txt | grep -E 'device:|D32_SFLOAT|D16_UNORM|result'
   ```

   If `xcodebuild` puts the app elsewhere, use the path it prints. Report the grep output of both runs.

## 3. The fresh `vk-bootstrap` build

`mac-m9-followup/vkb-manifest/` is a manifest with `vk-bootstrap` alone and the repository's overlays. `VCPKG_BINARY_SOURCES=clear` stops vcpkg from restoring a cached build, so the port really compiles.

1. Show where Apple Clang looks for headers: `echo | xcrun clang -x c++ -E -v - 2>&1 | sed -n '/search starts here/,/End of search list/p'`, and `ls -la /usr/local/include/vulkan/vulkan_core.h 2>&1`.
2. As the shell normally is, with `VULKAN_SDK` as set: `VCPKG_BINARY_SOURCES=clear "$VCPKG_ROOT/vcpkg" install --x-manifest-root=docs/agent-tasks/mac-m9-followup/vkb-manifest --x-install-root=build/vkb-check-a --triplet arm64-osx 2>&1 | tail -30`. Report the exit code. If it failed, open the build log vcpkg names and paste the first `FAILED:` line and the full compiler command after it, which shows the include directories in order, plus the first error.
3. The same with `VULKAN_SDK` unset: `env -u VULKAN_SDK VCPKG_BINARY_SOURCES=clear "$VCPKG_ROOT/vcpkg" install --x-manifest-root=docs/agent-tasks/mac-m9-followup/vkb-manifest --x-install-root=build/vkb-check-b --triplet arm64-osx 2>&1 | tail -30`. Report the same things.

If 3.2 fails and 3.3 passes, `VULKAN_SDK` is the cause. If both fail, the `FAILED:` commands show whether `/usr/local/include` comes before vcpkg's include directory. Do not change any file in `ports/` or `triplets/` to work around it.

## 4. Report

Write `docs/agent-tasks/mac-m9-followup-report.md` with:

- The commit hash, the `git config` identity and the versions from section 0.
- Section 1's lines.
- Section 2: the `D32_SFLOAT` and `D16_UNORM` lines and the result line for the Mac and the iPhone, verbatim, and any command that did not work as written.
- Section 3: every command's exit code, the search list, and the `FAILED:` command and first error of each failing run, verbatim.
- Anything that did not go as this task says, and what you did instead.

Before committing, check the files you are adding for private data:

```sh
grep -rn "/Users/" docs/agent-tasks/mac-m9-followup* || echo "no home paths"
```

Then search the same files for the team ID, the device name and the device identifiers, typing each one in yourself: `grep -rn -F "<the actual value>" docs/agent-tasks/mac-m9-followup*`. Every search must find nothing. Replace anything found with its placeholder, then search again. Then commit the report and the two probe outputs to this branch and push it. The commit message is `docs: report the Mac follow-up run for M9`. Leave the checkout on this branch.
