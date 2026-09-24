# Mac run: ADR-0018's open questions on macOS and an iPhone

Task branch `agents/mac-m9-questions`. It is not for merging. It is `docs/adr-m9-mobile` (the proposed [ADR-0018](../decisions/0018-mobile-export.md)) plus this file and `mac-m9-questions/`. ADR-0018 lists open questions that only a Mac, Xcode and an iPhone can answer. This run answers questions 1 and 2 for iOS, 3, 4, 5 and the iOS half of 9. No engine code is built or changed. The run uses a trial vcpkg manifest and a small probe program, `mac-m9-questions/probe/vkprobe.c`. The probe loads Vulkan the way ADR-0018 proposes for the player, with SDL3 finding a statically linked MoltenVK in the process. It prints every feature and limit the engine's device selection requires, and marks anything short as `MISSING`. On Linux it passes on an RTX 4090, RADV and Lavapipe.

Report **raw output**, not conclusions. Paste command output verbatim.

**Do not switch branches.** Write the report on this branch and push this branch.

**Do not commit** the Apple development team ID, the iPhone's identifier (UDID or CoreDevice id), or the phone's serial number. Write `<team>` and `<device>` in their place.

The iPhone is an iPhone 15 Pro Max. Sections 4 and 5 need it connected by USB, unlocked, trusting the Mac, with Developer Mode on (Settings > Privacy & Security > Developer Mode). They also need a signing team: Xcode > Settings > Accounts, where a personal team is enough. If either is missing, do sections 0 to 3, say which is missing, and stop there.

## 0. Setup

1. `git fetch origin && git checkout agents/mac-m9-questions && git reset --hard origin/agents/mac-m9-questions`, then report `git rev-parse HEAD`.
2. Report `sw_vers`, `xcodebuild -version`, `xcrun --sdk iphoneos --show-sdk-version`, `cmake --version | head -1`, and `"$VCPKG_ROOT/vcpkg" version | head -1`. Make sure `$VCPKG_ROOT` is the checkout the macOS presets use.
3. Report which Vulkan libraries the Mac has outside the SDK. This explains which library SDL found in the export failure recorded in [roadmap.md](../roadmap.md#the-macos-export-needs-the-vulkan-sdk): `ls -la /usr/local/lib/libvulkan* /usr/local/lib/libMoltenVK* /opt/homebrew/lib/libvulkan* /opt/homebrew/lib/libMoltenVK* 2>&1`, and `ls /usr/local/share/vulkan/icd.d /opt/homebrew/share/vulkan/icd.d 2>&1`.
4. With the iPhone connected: `xcrun devicectl list devices`. Report the model and iOS version columns only.

## 1. Questions 1 and 2: every port for `arm64-ios`

`mac-m9-questions/manifest/` is `vcpkg.json` with ADR-0018's changes: `shader-slang` as a host tool plus a desktop-only entry, and `vulkan-loader` and `imgui` desktop-only. Its `vcpkg-configuration.json` points at the repository's `ports/` and `triplets/`.

1. `mkdir -p build && "$VCPKG_ROOT/vcpkg" install --x-manifest-root=docs/agent-tasks/mac-m9-questions/manifest --x-install-root=build/m9-ios --triplet arm64-ios --keep-going 2>&1 | tee build/m9-ios.log`
2. Report the exit code, every `Installing N/M ...` line, and every line containing `BUILD_FAILED`, `error:` or `failed`.
3. For each port that failed, paste the last 80 lines of each log file vcpkg names for it.
4. Question 2 in particular: report the result lines for `vulkan-loader:arm64-ios` and `vulkan:arm64-ios`, and `cat "$VCPKG_ROOT/buildtrees/vulkan/vulkan-arm64-ios.cmake.log"` if it exists.
5. The same manifest for the desktop, to show the changes cost macOS nothing: `"$VCPKG_ROOT/vcpkg" install --x-manifest-root=docs/agent-tasks/mac-m9-questions/manifest --x-install-root=build/m9-osx --triplet arm64-osx 2>&1 | tail -5`, and `ls build/m9-osx/*/tools/shader-slang/ | head`.

## 2. Question 3: the iOS deployment target

1. Find the libc++ availability table Xcode uses: `find "$(xcode-select -p)" -path '*c++/v1/__configuration/availability.h' 2>/dev/null`. For each file found, paste every line containing `IPHONE_OS_VERSION_MIN_REQUIRED` with the two lines after it (`grep -n -A2`), and every line matching `^#define _LIBCPP_AVAILABILITY_HAS_`.
2. Compile `mac-m9-questions/probe/libcxx.cpp` once per feature and iOS version. It has one feature per macro and compiles cleanly with GCC 14, so any error is availability:

   ```sh
   for t in 15.0 16.3 17.0 18.0 26.0; do
     for f in FORMAT_DOUBLE TO_CHARS_FLOAT FROM_CHARS_FLOAT FROM_CHARS_INT PRINT ATOMIC_WAIT FILESYSTEM EXPECTED; do
       if xcrun --sdk iphoneos clang++ -std=c++23 -target arm64-apple-ios$t -fsyntax-only -DFEATURE_$f \
            docs/agent-tasks/mac-m9-questions/probe/libcxx.cpp 2> build/libcxx-$t-$f.txt; then echo "ios$t $f ok"
       else echo "ios$t $f ERROR: $(grep -m1 error: build/libcxx-$t-$f.txt)"; fi
     done
   done
   ```

   Paste the output in full.

## 3. Question 4 on macOS: static MoltenVK, no SDK

1. Download the pinned release and check it. The expected SHA-256 sums were taken on the Linux machine that wrote this task.

   ```sh
   mkdir -p build/moltenvk && cd build/moltenvk
   gh release download v1.4.2 -R KhronosGroup/MoltenVK -p MoltenVK-macos.tar -p MoltenVK-ios.tar --clobber
   shasum -a 256 MoltenVK-macos.tar MoltenVK-ios.tar
   # expected: f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e  MoltenVK-macos.tar
   #           b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1  MoltenVK-ios.tar
   mkdir -p macos ios && tar xf MoltenVK-macos.tar -C macos && tar xf MoltenVK-ios.tar -C ios && cd ../..
   ```

   Report the `shasum` output.
2. Build the probe for macOS:

   ```sh
   cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=arm64-osx \
     -DMOLTENVK_DIR="$PWD/build/moltenvk/macos/MoltenVK"
   cmake --build build/vkprobe-macos
   ```

   If the link fails on undefined symbols, paste the error. Add the `-framework` each missing symbol belongs to in `probe/CMakeLists.txt`, in the `if(APPLE)` block, and rebuild until it links. Report the final list of frameworks. That list is part of question 4, so commit the edited `CMakeLists.txt`.
3. `otool -L build/vkprobe-macos/vkprobe`. Paste it. No `libvulkan` or `MoltenVK` library should be listed.
4. Run the probe with every SDK variable cleared, as in the failure report: `env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_ADD_LAYER_PATH -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH ./build/vkprobe-macos/vkprobe --probe-argument 2>&1 | tee docs/agent-tasks/mac-m9-questions/macos-vkprobe.txt`. Report the exit code. The output file is committed with the report.
5. For comparison, the same run of the SDK's `vulkaninfo --summary 2>&1 | head -40`, with the environment as it normally is.

## 4. Questions 4 and 5 on the iPhone

1. Build the probe for iOS with the team exported, and a bundle identifier of your own that is unique to the team:

   ```sh
   export SONNET_DEVELOPMENT_TEAM=<team>
   cmake -S docs/agent-tasks/mac-m9-questions/probe -B build/vkprobe-ios -G Xcode \
     -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
     -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=arm64-ios \
     -DMOLTENVK_DIR="$PWD/build/moltenvk/ios/MoltenVK" -DVKPROBE_BUNDLE_ID=<your unique id>
   cmake --build build/vkprobe-ios --config Release -- -allowProvisioningUpdates
   ```

   Report the output of any step that fails, and any frameworks you had to add as in 3.2.
2. `xcrun devicectl device install app --device <device> build/vkprobe-ios/Release-iphoneos/vkprobe.app`. Report the output, with the identifiers replaced.
3. Launch it with its console attached and an argument: `xcrun devicectl device process launch --device <device> --console <bundle id> --probe-argument 2>&1 | tee docs/agent-tasks/mac-m9-questions/iphone-vkprobe-console.txt`. The probe exits by itself. If `--console` is not accepted, run the same command without it and say so.
4. Copy the probe's file out of the app's container. The console shows the path it wrote to on a `writing ...` line. Use the part of that path after the container's root: `xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier <bundle id> --source "<path inside the container>" --destination docs/agent-tasks/mac-m9-questions/iphone-vkprobe.txt`. If this command form fails, report its output and what worked instead.
5. If the app launched but no console output arrived and no file could be copied, open the device's log in Console.app, filter on `vkprobe`, launch the app from its icon, and paste what appears.

## 5. Report

Write `docs/agent-tasks/mac-m9-questions-report.md` with:

- The versions from section 0 and the commit hash.
- Section 1: a list of every port with ok or failed for `arm64-ios`, the failures' log excerpts verbatim, and the two lines question 2 asks about.
- Section 2: the availability lines verbatim, and the feature-by-version output as a table.
- Section 3: the `shasum`, `otool` and link output, the final framework list, and the probe's result line. The full probe output is in `macos-vkprobe.txt`.
- Section 4: every command's output with identifiers replaced, and the probe's result line from the iPhone. The full output is in `iphone-vkprobe.txt` or the console file.
- Anything that did not go as this task says, and what you did instead.

Commit the report, the probe outputs and any edit to `probe/CMakeLists.txt` to this branch, and push it. The commit message is `docs: report the Mac run of the M9 open questions`. Leave the checkout on this branch.
