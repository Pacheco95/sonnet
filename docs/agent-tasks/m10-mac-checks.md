# Mac run: PR #58's Mac-only checks (no iPhone)

Task branch `agents/m10-mac-checks`. It is not for merging. It is `feat/ios-build-scaffold` (PR [#58](https://github.com/Pacheco95/sonnet/pull/58), open and draft) plus this file. That branch already passed CI on `macos-latest` — configuring `ios-debug`/`ios-release`, building the player, linking `moltenvk`, compiling the launch storyboard and Info.plist, and packaging the app bundle — so the code itself is not in question here. This task exercises the three things CI could not, because none of them need an iPhone, only a Mac:

1. Whether linking MoltenVK into the player on every Apple platform actually closes [the open "macOS export needs the Vulkan SDK" roadmap entry](../roadmap.md#the-macos-export-needs-the-vulkan-sdk) — CI never ran the exported binary, only compiled it.
2. Whether `SONNET_IOS_BUNDLE` really lands a cooked game inside the app bundle — CI's `iOS` job built without one, so only the shader half of that copy step ever ran.
3. Whether Xcode's automatic signing actually succeeds with a real team — CI builds with `CODE_SIGNING_ALLOWED=NO`, which skips signing entirely rather than proving it works.

Report **raw output**, not conclusions. Paste command output verbatim.

**Do not switch branches.** Write the report on this branch and push this branch. Do not push to `feat/ios-build-scaffold` or touch PR #58 directly — if something here is broken, the report says so and a follow-up commit fixes it afterward.

**No engine or CMake changes are expected.** If a command fails in a way that looks like a bug in `apps/player/CMakeLists.txt`, `cmake/SonnetIOS.cmake` or `ports/moltenvk/`, do not edit it — paste the failure in full in the report and stop that section.

**Privacy.** The repository is public. Never commit or paste the Apple team ID or any personal name `codesign`/Xcode prints (your own or an organisation's). Write `<team>` and `<name>` in their place, in the report, in pasted output and in commit messages. Section 4 greps for them before committing.

Build with Apple Clang from Xcode, never Homebrew LLVM: unset `CC` and `CXX` in the shell for every command of this task. Homebrew LLVM's libc++ rejects the iOS targets ([build.md](../build.md#toolchains)).

## 0. Setup

1. `git fetch origin && git checkout agents/m10-mac-checks && git reset --hard origin/agents/m10-mac-checks`, then report `git rev-parse HEAD`.
2. Report `git config user.name` and `git config user.email`. They must be `Michael Pacheco` and `mdpgd95@gmail.com`. If they are not, stop and say so. Do not commit anything.
3. `unset CC CXX`, then report `sw_vers`, `xcodebuild -version`, `xcrun --sdk iphoneos --show-sdk-version`, `cmake --version | head -1`, and `"$VCPKG_ROOT/vcpkg" version | head -1`. Make sure `$VCPKG_ROOT` is the checkout the presets use.

## 1. The macOS export, with the Vulkan SDK's variables cleared

The failure this closes, from an export directory: `SDL_CreateWindow failed: Installed Vulkan Portability library doesn't implement the VK_KHR_surface extension`. It happened because the export carried no Vulkan driver and SDL fell back to whatever loader the machine had on its search path. The player now links MoltenVK statically, so it should get past window and device creation even with every SDK variable cleared.

1. Build the player and the cook tool:

   ```sh
   cmake --preset macos-debug
   cmake --build --preset macos-debug --target sonnet_player_app sonnet_cook_app
   ```

2. Cook the basic sample for macOS and assemble an export directory the way the editor's export dialog would (docs/player.md, "What an export is"): the player binary, `shaders/`, and one `.sbundle`, nothing else.

   ```sh
   ./build/macos-debug/apps/cook/sonnet_cook apps/samples/basic --platform macos --out build/macos-bundle
   mkdir -p build/macos-export
   cp build/macos-debug/apps/player/sonnet_player build/macos-export/
   cp -r build/macos-debug/apps/player/shaders build/macos-export/
   cp build/macos-bundle/game.sbundle build/macos-export/
   ```

3. Run it from that directory with every Vulkan SDK variable cleared, capturing a screenshot so the run has an unambiguous pass/fail (exit code, and a PNG on disk):

   ```sh
   cd build/macos-export
   env -u VK_ICD_FILENAMES -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_ADD_LAYER_PATH \
     -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH \
     ./sonnet_player --screenshot ../macos-export-screenshot.png --settle-frames 5 2>&1 | tee ../macos-export-run.log
   echo "exit: ${PIPESTATUS[0]}"
   cd ../..
   ls -la build/macos-export-screenshot.png
   ```

   Report the exit code, the full log, and whether the screenshot file exists. Passing means the `SDL_CreateWindow`/Vulkan Portability error from the roadmap entry is gone and the device log line names an Apple GPU through MoltenVK instead.

## 2. `SONNET_IOS_BUNDLE`, with a real cooked game

1. Cook the basic sample for iOS and configure the app bundle to carry it:

   ```sh
   ./build/macos-debug/apps/cook/sonnet_cook apps/samples/basic --platform ios --out build/ios-bundle
   cmake --preset ios-debug -DSONNET_IOS_BUNDLE="$PWD/build/ios-bundle/game.sbundle"
   cmake --build --preset ios-debug -- CODE_SIGNING_ALLOWED=NO
   ```

2. Find the built bundle and list its `Resources/`:

   ```sh
   find build/ios-debug -name sonnet_player.app
   find "$(find build/ios-debug -name sonnet_player.app)/Resources" -maxdepth 2
   ```

   Report both. It should list `shaders/` (with `.spv` files in it) and `game.sbundle` sitting next to it. If either is missing, that is what `sonnet_add_ios_bundle`'s `POST_BUILD` step (`cmake/SonnetIOS.cmake`) is supposed to copy in — report the listing and the full build output around the "CMake PostBuild Rules" phase, but do not edit the CMake files.

## 3. Automatic signing with a real team

Needs a signing team in Xcode (Xcode > Settings > Accounts); a personal (free) team is enough. No device is needed to build for "Any iOS Device" and sign it — only to install what comes out. If no team is available, skip this section and say so in the report.

```sh
export SONNET_TEAM=<your team id, never pasted into the report>
cmake --preset ios-release -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$SONNET_TEAM"
cmake --build --preset ios-release -- -allowProvisioningUpdates -allowProvisioningDeviceRegistration
codesign -dv --verbose=4 "$(find build/ios-release -name sonnet_player.app)" 2>&1
```

Report the `codesign` output and the build's exit code, with the team ID and any personal name replaced by `<team>` and `<name>` before it goes anywhere near the report or a paste.

## 4. Report

Write `docs/agent-tasks/m10-mac-checks-report.md` with:

- The commit hash, the `git config` identity and the versions from section 0.
- Section 1: the exit code, whether the screenshot was written, and the device log line naming the GPU. The full log is `build/macos-export-run.log`; do not commit it, quote the relevant lines in the report instead (it is a build artifact, not source).
- Section 2: both `find` listings, verbatim.
- Section 3: the `codesign` output and exit code, or why the section was skipped.
- Anything that did not go as this task says, and what you did instead.

Before committing, check the report for private data:

```sh
grep -n "/Users/" docs/agent-tasks/m10-mac-checks-report.md || echo "no home paths"
```

Then search the same file for the team ID and any personal name, typing each one in yourself: `grep -n -F "<the actual value>" docs/agent-tasks/m10-mac-checks-report.md`. Every search must find nothing. Replace anything found with its placeholder, then search again.

Commit only `docs/agent-tasks/m10-mac-checks-report.md` (and this file, unchanged) to this branch and push it. The commit message is `docs: report the M10 Mac-only checks`. Leave the checkout on this branch.
