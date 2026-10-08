# M9 and M10: mobile export, step by step

Archived from [the roadmap](../roadmap.md), where this was the body of M9 and M10. It is the record of what each step changed, what ADR-0018 and ADR-0019 got wrong and what the Galaxy S25 Ultra, the emulator, the Mac and the iPhone 15 Pro Max showed. The decisions are in [ADR-0018](../decisions/0018-mobile-export.md) and [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md); the behaviour is in [build.md](../build.md), [player.md](../player.md), [platform.md](../platform.md), [assets.md](../assets.md) and [rendering.md](../rendering.md).

## Vulkan 1.3 devices

The Galaxy S25 Ultra's driver is Vulkan 1.3.284, which the selector rejected. [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) is implemented in `rhi`, before any of the Android work, since it can be checked on the desktop:

1. The selector takes devices at 1.3 or later with the 1.0 to 1.3 features, then checks the four 1.4 features per device: through `VkPhysicalDeviceVulkan14Features` at 1.4, through their extensions and feature structures at 1.3. The first device with all four is taken, in vk-bootstrap's order; if none has them, the error names what each device lacks.
2. A device's version is its own, bounded by the one the instance asked for. VMA takes that version, and `DeviceInfo` reports it with `vulkan14FeaturesAsExtensions`; the device log line says when the features came as extensions. Dear ImGui's backend takes the device's version rather than 1.4.
3. `DeviceDesc::apiVersionCap` lowers the version the instance asks for. CTest runs `rhi_tests` a second time as `rhi_tests_vulkan_1_3`, with every test device capped at 1.3.

Checked on Lavapipe and on the RTX 4090, each with and without the cap: every `rhi_tests` case passes with validation on, and the capped runs report a 1.3 device with the features as extensions. The RTX 4090 skips the three headless swapchain cases, as before. Enabling `VkPhysicalDeviceVulkan14Features` under the cap instead fails validation at `vkCreateDescriptorSetLayout`, so the capped run does catch a 1.4 structure used on the 1.3 path. The phone itself waits for the first APK.

## Checked before the code

ADR-0018 was accepted with open questions. Questions 1 (Android), 3, 6, 7 and 8 were answered in it before acceptance. The rest were answered afterwards by a run on the Mac (macOS 26.7, Xcode 26.3 with the iOS 26.2 SDK) and an iPhone 15 Pro Max (`iPhone16,2`, A17 Pro), reported on `agents/mac-m9-questions`. The ADR itself stays as accepted.

- **Every port builds for `arm64-ios` (question 1).** The manifest with ADR-0018's changes installed all 29 ports, with Xcode's Apple Clang. Homebrew LLVM 22 does not work: with `CC` and `CXX` pointing at it, `ktx` failed, because its libc++ rejects the iOS 12.0 minimum the port compiles for ("The selected platform is no longer supported by libc++"). The iOS presets therefore take Apple Clang, whatever the environment says.
- **The `vulkan` stub port installs for iOS (question 2).** It pulls in `vulkan-loader:arm64-ios`, which builds in about five seconds and is never linked. ADR-0018 makes the `vulkan-memory-allocator-hpp` overlay conditional on the install failing. It did not fail, so there is no overlay. The unused loader in the iOS install tree is the whole cost.
- **The iOS deployment target is 16.3 (question 3), confirmed against Xcode's own headers.** Compiling each library feature against iOS 15.0, 16.3, 17.0, 18.0 and 26.0 gives these minimums: `std::format` of a `double` and floating-point `to_chars` need 16.3. Floating-point `from_chars` needs 26.0, later than the table in `availability.h` suggests (LLVM 20, which it dates to iOS 19). Integer `from_chars`, atomic waits, `std::filesystem` and `std::expected` all work from 15.0. This is why the player parses `--play` without floating-point `from_chars`.
- **Static MoltenVK works on both Apple platforms (question 4).** `vkprobe` links Khronos's MoltenVK 1.4.2 static library (both archives' SHA-256 matched) with `-Wl,-u,_vkGetInstanceProcAddr`. The only frameworks it needs beyond SDL3's are Metal, Foundation, QuartzCore, IOSurface and CoreGraphics, plus IOKit and AppKit on macOS and UIKit on iOS. On macOS, `otool -L` lists no Vulkan or MoltenVK library. With every Vulkan SDK variable cleared, the probe finds `vkGetInstanceProcAddr` in the process, SDL hands back the same pointer, and `dladdr` and `dlopen(RTLD_NOLOAD)` resolve it to the executable, so `Platform`'s loader pinning is harmless. The instance is created without portability enumeration, which MoltenVK does not offer. The same holds on the iPhone.
- **The iPhone meets the baseline (question 5).** The A17 Pro GPU reports Vulkan 1.4.357 through MoltenVK 1.4.2, and every required feature and limit is present. ASTC LDR and BC are both supported, as is `hostImageCopy`, and ASTC 4×4 and 6×6 and BC7 sample with linear filtering. `drawIndirectCount` is absent, as on the Mac.
- **The iOS commands work as ADR-0018 wrote them (question 9, iOS half).** These all worked: building with `xcodebuild -allowProvisioningUpdates -allowProvisioningDeviceRegistration`, `devicectl device install app`, `devicectl device process launch --console <bundle id> <arguments>` (the arguments arrived), and `devicectl device copy from --domain-type appDataContainer`. SDL's pref path on iOS is `Library/Application Support/<organisation>/<application>/` inside the app's data container. The first launch of an app signed by a personal team fails until the developer profile is trusted on the phone, under Settings > General > VPN & Device Management. That is a one-time step for whoever holds the phone.

A follow-up run (`agents/mac-m9-followup`) closed what the first run left open:

- **The iPhone can use `D32_SFLOAT` for the shadow cascades as they are.** Read bit by bit from `VkFormatProperties3`, the A17 Pro's `D32_SFLOAT` is a depth attachment and can be sampled, compared and copied into, but not linearly filtered. `D16_UNORM` has every bit. The M4 Max has every bit for both. The cascades are compared through a linear comparison sampler, and the Vulkan spec requires linear filtering only of a sampler that does not compare (`VUID-vkCmdDraw-magFilter-04553`). A comparison needs `SAMPLED_IMAGE_DEPTH_COMPARISON` (`VUID-vkCmdDraw-None-06479`), which the iPhone has. The only read of a depth image in the shaders is `forward.slang`'s `SampleCmpLevelZero`, so nothing changes for iOS. The rule this leaves: a depth image is never sampled without comparison through a linear filter, or iOS breaks.
- **The iPhone runs iOS 27.0** (`osVersionNumber` from `devicectl device info details`). The earlier "19" was another field.
- **Signing:** `DEVELOPMENT_TEAM` has to be the team of the provisioning profile Xcode made for the bundle identifier. That can differ from the team of the signing certificate in the keychain, and passing the certificate's team failed with "No Account for Team".

The Android half of question 9 waits for the first APK. The Galaxy S25 Ultra's Vulkan report is in ADR-0018's open question 6 and led to [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md).

## The Android build

The first Android step is the build alone, as ADR-0018's "Builds" section decides it ([build.md](../build.md#presets)):

- **Manifest:** `shader-slang` is declared as a host tool and again as a desktop-only library, and `vulkan-loader` and `imgui` are desktop only. There is no `vulkan-memory-allocator-hpp` overlay, because the iOS install did not need one (above).
- **`slangc` comes from the host triplet.** vcpkg's toolchain searches the host triplet's tools only when `VCPKG_HOST_TRIPLET` is set, and it never sets it, so the first configure found no `slangc` under `arm64-android` and took the Vulkan SDK's from the environment. On a machine without the SDK it would have failed. `cmake/SonnetShaders.cmake` now adds `vcpkg_installed/<host triplet>/tools/shader-slang` to `CMAKE_PROGRAM_PATH` in a cross build.
- **Presets:** `android-debug` and `android-release`, with build presets and no test presets. They configure only `core` to `runtime` and the player.
- **The player is `libsonnet_player.so`.** `sonnet_add_executable` makes a shared library on Android. It exports `SDL_main`, the four `SDL_App*` callbacks and SDL's JNI entry points (`JNI_OnLoad`, `Java_org_libsdl_app_*`), which is what SDL's Java side loads.
- **CI:** an Android job configures `android-release` and builds the player library, with NDK r30 rather than the runner's default r27.3 that ADR-0018 names. r27.3 builds every port but stops at API 35 (`android-36 is above the maximum supported version 35`), and so do r28 and r29: each release branch's `meta/platforms.json` ends at 35, and its build refuses a sysroot that goes further. The ADR's `android-36` stays, and the supported NDK floor in [build.md](../build.md#toolchains) rises from r27 to r30.

Verified with NDK r30 (30.0.16248370, Clang 21) on Linux: the `arm64-android` install of the committed manifest has all 28 ports, the host `x64-linux` `shader-slang` among them. 26 were restored from the binary cache left by the ADR's own install run, and `joltphysics` and `ktx` were rebuilt. Both presets configure and build every target with no warnings and no change to engine code. Configuring with the Vulkan SDK's `PATH` and `CMAKE_PREFIX_PATH` in the environment, and again without them, picks the host triplet's `slangc` both times. The desktop is unchanged: the same ports for `x64-linux`, `slangc` from the same place, and all 13 suites pass on Lavapipe with GCC 14 and with Clang 22.

Still to do for the player on a phone, in ADR-0018's order: the APK (`cmake/SonnetAndroid.cmake`, SDL's Java sources, the manifest, debug signing), ASTC cooking, touch input, `Platform::openContent`, the swapchain's suspend and resume, and the capture in the player. None of them blocks the build.

## The Android APK

The second Android step packages the player, as ADR-0018's "Packaging" section decides it ([build.md](../build.md#android), [player.md](../player.md#running-on-android)):

- **`cmake/SonnetAndroid.cmake`** adapts SDL's `SdlAndroidFunctions.cmake` (zlib licence, attributed in the file) into `sonnet_add_apk`, with no Gradle: `aapt2` compiles and links the resources and manifest against `platforms/android-36`, `javac --release 17` compiles SDL's Java sources and the activity, `d8` dexes them, `zip` stores the stripped `libsonnet_player.so` and `assets/shaders/` uncompressed, `zipalign -P 16` puts the library on a 16 KB page, and `apksigner` signs with a debug keystore `keytool` generates once in the build directory. `sonnet_player_apk` is its own target; `sonnet_player_app` still builds alone. A cooked bundle is packaged as `assets/game.sbundle` only when `SONNET_ANDROID_BUNDLE` names one. The build does not cook.
- **The `sdl3` overlay port** installs SDL's Java sources into `share/sdl3/android-java/` for Android triplets, and turns off `SDL3.jar`, which SDL otherwise builds only when the environment happens to have a JDK and the Android SDK. Desktop triplets install the same files as before.
- **`apps/player/android/`** holds the manifest (package `io.github.pacheco95.sonnet`, API 36 minimum and target, `extractNativeLibs="false"`), `SonnetActivity` and a vector icon. `getLibraries()` returns `sonnet_player` alone. `getArguments()` splits the intent's `args` extra and logs the result under the `Sonnet` tag.
- **The manifest requires Vulkan 1.3, not 1.4.** ADR-0018 has `android.hardware.vulkan.version` `0x404000`, written before [ADR-0019](../decisions/0019-vulkan-1.3-devices-with-the-1.4-extensions.md) accepted 1.3 devices with the four 1.4 features as extensions. The Galaxy S25 Ultra reports 1.3.284, so it could not have installed a 1.4 APK. The manifest asks for `0x403000` with `required="true"` and leaves the features to the device selector. The ADR's text stays as accepted.
- **CI:** the Android job builds the APK after the player library with the runner's JDK 17 (`JAVA_HOME` pinned to `JAVA_HOME_17_X64` and checked) and build-tools 36.1.0, verifies its signature, alignment and badging, and uploads it as `sonnet_player-android`.

Verified on Linux with NDK r30, build-tools 36.1.0 and JDK 25 (`--release 17`):

- Both `android-debug` and `android-release` build the APK: 34 MB and 14 MB, nearly all of it the library.
- `apksigner verify` passes with the v3 scheme, the only one a `minSdkVersion` of 36 needs.
- `aapt2 dump badging` shows the package, `minSdkVersion` and `targetSdkVersion` 36, `uses-feature: name='android.hardware.vulkan.version' version='4206592'` (`0x403000`), `native-code: 'arm64-v8a'` and `application-debuggable`. Both presets are debuggable, since RelWithDebInfo counts as debuggable, so `run-as` works on both.
- `zipalign -c -P 16 -v 4` passes, with the library at offset 49152, three 16 KB pages.
- `unzip -v` lists `lib/arm64-v8a/libsonnet_player.so`, every `assets/shaders/*.spv` and, when given, `assets/game.sbundle` as `Stored`.
- A configure without `ANDROID_HOME`, or with a build-tools version that is not installed, fails with a message naming it.
- The desktop is unaffected: `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) rebuilt `sdl3` at the new port-version, installed the same files, and build with no warnings, and all 13 suites pass on Lavapipe.

**An emulator can stand in for the phone for packaging, install, launch and arguments, not for rendering.** Probed with emulator 37.1.11 and the `android-36;google_apis_ps16k;x86_64` image (Android 16 with 16 KB pages) under KVM:

- `-gpu host` crashed the emulator (SIGSEGV) a minute into boot, twice, once with the Vulkan SDK's environment cleared. Before crashing it took the RTX 4090 and logged `guestVulkanMaxApiVersion: 1.3.0`, so host mode would offer the guest 1.3 at most.
- `-gpu swiftshader_indirect` boots in 24 s. `pm list features` has `android.hardware.vulkan.version=4206592` (1.3) and `vulkan.level=1`. `cmd gpu vkjson` reports an instance at 1.4.0 and one device, "SwiftShader Device (Subzero)", at **1.3.0**, with ASTC LDR but **none** of `VK_KHR_push_descriptor`, `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_maintenance5` and `VK_KHR_maintenance6`. ADR-0019's selector rejects it.
- The arm64-only APK installs, from both presets, through ARM translation. The image's `abilist` is `x86_64,arm64-v8a`, and Berberis does the translating. The library loads in place from `base.apk!/lib/arm64-v8a`, which confirms it is stored and page-aligned. `am start` with `--es args` launches it, and the activity logs the arguments split as intended (`[my game.sbundle, --flag]`). SDL runs `SDL_main`, which returns after about 0.6 s. Berberis logs the Vulkan entry points it has no wrapper for (`vkGetPhysicalDeviceDescriptorSizeEXT` and others). The reason for the exit is not visible, since the engine's log does not reach logcat yet, and routing stdout through `wrap.<package>` disables the native bridge. The selector rejecting SwiftShader is the likely cause. `run-as` works.
- One APK signed in one build directory cannot update an install from the other (`INSTALL_FAILED_UPDATE_INCOMPATIBLE`), since each has its own debug key. `adb uninstall` first.

**On the Galaxy S25 Ultra** (SM-S938B, Android 16, API 36, 4 KB pages), which answers the Android half of ADR-0018's question 9:

- `pm list features` reports `android.hardware.vulkan.version=4206592`, exactly `0x403000`. The manifest's 1.3 requirement matches it, and ADR-0018's `0x404000` would have refused the install.
- The `android-release` APK installs with `adb install --user 0` (the phone has a second user profile, which plain `pm` commands trip over). The library loads in place from `base.apk!/lib/arm64-v8a`.
- `am start` with `--es args` passes the arguments (`[my game.sbundle, --flag]`). `run-as io.github.pacheco95.sonnet` reaches the app's data directory.
- With spdlog's Android sink added to a local build for this one run (not committed), the engine's log shows how far the player gets. The Adreno 830 is taken as a "Vulkan 1.3.284 device ... with the 1.4 features as extensions", so ADR-0019's path works on the device it was written for. The swapchain is created (1080×2340, 5 images, `R8G8B8A8Unorm`, Mailbox), and the job system starts with 7 workers. Startup then fails with `cannot open ./shaders/cluster.spv`, since the shaders are in the APK and nothing reads them from there yet. `Platform::openContent` is therefore the next blocker, and the logcat sink, which that diagnosis needed, should come with it.

Still to do before the basic sample runs on the phone: ASTC cooking and `CookPlatform::android`, `Platform::openContent` (the player cannot read the APK's assets without it), spdlog's logcat sink, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player. After those, the CI job cooks the sample into the APK.

## Reading content from the APK

The third Android step reads the content the APK stores, as ADR-0018's "Packaging" section decides, and puts the engine's log in logcat ([platform.md](../platform.md#paths)):

- **The logcat sink.** On Android the entry point adds spdlog's `android_sink_mt` through `core::Log::addSink` before the first line is logged, under the tag `Sonnet`, the one `SonnetActivity` logs its arguments under. The console sink stays, `core` stays platform-agnostic, and `platform` links `liblog` itself on Android. It already reached the link through spdlog's and SDL's interface libraries.
- **`Platform::openContent`** returns a `ContentStream`, a seekable, read-only stream over `SDL_IOStream` with `read`, `readExactly`, `seek`, `tell`, `size` and `readAll`. `Content.h` forward-declares `SDL_IOStream`, so SDL stays out of the header. A relative path resolves against the content root, which is the APK's `assets/` on Android and `basePath()` elsewhere. An absolute path is an ordinary file everywhere. On Android, SDL looks a relative path up in the app's internal storage before the assets. `openContent` is static, since it needs nothing SDL initialises.
- **The readers.** `Bundle` holds a `ContentStream` instead of an `std::ifstream`, and checks a payload's span against the stream's size before allocating. `Bundle::open` and `AssetDatabase::openBundle` still take a path, because `openContent` is static and every desktop caller passes an absolute path. The renderer reads its shaders through `openContent`. `Game` passes the relative `shaders` and so no longer takes the `Platform`, while the editor, the cook and the tests pass the absolute `basePath() / "shaders"` as before. With no argument the player opens `game.sbundle` from the content root. It makes an argument absolute first, so on desktop an argument is still a path from the working directory. Import, cook, `Project` and the editor's files stay on `core::readFile`.
- **No new dependency.** `assets` and `renderer` reach `platform` through `rhi`, which links it publicly.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings. All 13 suites pass on Lavapipe, including the five new `openContent` cases in `platform_tests`, and `runtime_tests` loads the basic sample as a folder and as a bundle.
- The desktop player runs the basic sample from a project folder, from a relative bundle argument given in another working directory, and from `game.sbundle` beside a copied binary started from `/`. An export the editor wrote (`Editor::exportProject` with the real player: 27 assets, 22 support files) runs the same way.
- `android-debug` and `android-release` build with no warnings. `libsonnet_player.so` lists `liblog.so` as `NEEDED`.

**On the emulator** (`sonnet36`, `-gpu swiftshader_indirect`), since the phone was not connected:

- `adb logcat -s Sonnet` shows the engine's log from its first line (`Sonnet 0.10.0`), with spdlog's levels as logcat's (`I`, `D`, `W`, `F`). The log reaches the device selector, which rejects SwiftShader: `Missing feature VkPhysicalDeviceVulkan11Features::shaderDrawParameters`. That is a 1.1 feature, checked before the four 1.4 extensions the APK step expected to be the reason. The player then exits with a failure.
- The device is rejected before anything reads the shaders or the bundle. A probe added to a local build for this one run (not committed) showed that the reads work from the APK. It read `shaders/cluster.spv` through `openContent`, 41812 bytes, the size `unzip -v` lists. It also opened the packaged `assets/game.sbundle` (the basic sample, cooked by the Linux `sonnet_cook`) with its 27 assets, and read its start scene.
- A bundle pushed to `/data/local/tmp` and copied with `run-as ... cp` into `files/` opens when `--es args` gives its absolute path. A missing absolute path is an `Io` error naming it.

**On the Galaxy S25 Ultra** (Android 16, the `android-debug` APK with the basic sample cooked by the Linux `sonnet_cook` and packaged as `assets/game.sbundle`):

- `adb logcat -s Sonnet` shows the engine's log from its first line. The Adreno 830 is taken on ADR-0019's path ("Vulkan 1.3.284 device ... with the 1.4 features as extensions"), the swapchain is created (1080×2340, 5 images, `R8G8B8A8Unorm`, Mailbox), and the job system starts with 7 workers.
- The shaders now load from the APK. The earlier `cannot open ./shaders/cluster.spv` is gone, and startup gets one step further, into `Renderer::createPipelines`.
- **There the process dies with a SIGSEGV**, a null-pointer read inside the driver's shader compiler (`/vendor/lib64/libllvm-qgl.so`), called from `vkCreateComputePipelines` through `VulkanDevice::createComputePipeline`. Nothing is logged first, since the crash is in the driver. `cluster.spv` is the first module the renderer builds, and its one pipeline is the compute pipeline `light clustering`, so that pipeline is the likely one. The crash happens before the bundle is opened, so the basic sample does not reach the screen. This step does not investigate it, as scoped. It is the next blocker on the phone.
- A bundle pushed to `/data/local/tmp` and copied with `run-as ... cp` into `files/` gets its absolute path through `--es args` (the log shows `arguments: [/data/user/0/io.github.pacheco95.sonnet/files/pushed.sbundle]`). The run then stops at the same crash, since `Game` builds the renderer before it opens the bundle. The emulator run above showed that the bundle opens by absolute path.

Still to do before the basic sample runs on the phone: the Adreno compiler crash in `createComputePipeline` ([the next step](#the-adreno-shader-compiler)), then ASTC cooking and `CookPlatform::android`, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

## The Adreno shader compiler

The fourth Android step finds and works around the crash above.

**Finding the pipeline.** `Renderer::createPipelines` now logs each module and each pipeline at debug level before creating it, so the last line before a driver crash names the pipeline. On the phone it is `pipeline "light clustering" from cluster`. On Android, SDL reads a relative path from the app's internal storage before the APK, so a module copied into `files/shaders/` with `run-as` replaces the packaged one without a rebuild. That made every experiment below a copy and a restart:

- A compute module that does not read `frame` builds, and the next pipeline, `cull`, crashes at the same address. So the crash is not about clustering. It is about how `FrameConstants` is declared.
- **Debug information is ruled out.** The module built with `-O2` and no debug information (what Release ships) crashes the same way, as do `-g0` and `-g1`.
- **Validity is ruled out.** `spirv-val --target-env vulkan1.3 --scalar-block-layout` accepts every module. They declare SPIR-V 1.6, which the device takes as a 1.3 device, and they use only the `Shader` and `PhysicalStorageBufferAddresses` capabilities, both of which the device has. The engine requires `bufferDeviceAddress` and `scalarBlockLayout`.
- **A minimal repro.** A uniform block with a pointer to a `uint`, a scalar array or an array of matrices builds, but a block with a pointer to a struct crashes. Slang emits `OpTypeForwardPointer` for every pointer to a struct it has not emitted yet, and defines the pointer after the block that uses it. The same module, with the pointer and its struct defined before the block and nothing else changed, builds. So does one that keeps the `OpTypeForwardPointer` but defines everything in order. What the driver cannot take is a struct member whose pointer type is only declared forward, not the instruction itself.

With the types reordered, `light clustering` fails differently: `vkCreateComputePipelines` returns `VK_ERROR_UNKNOWN`. Reducing again:

| Read through `Light *lights` | Adreno 830 |
|---|---|
| `lights[i].position` (the first member) | builds |
| `lights[0].range`, a whole `lights[1]` | builds |
| `lights[i].range`, `lights[i].color`, a whole `lights[i]`, `clusters[i].lights[0]` | `VK_ERROR_UNKNOWN` |
| `lights[i].range` as one `OpPtrAccessChain lights i 1` | builds |

A pointer to a struct computed with a dynamic index cannot be read past its first member or loaded whole. Slang writes `lights[i].range` as an `OpPtrAccessChain` to the element followed by an `OpAccessChain` to the member. Decorating the struct `Block`, as glslang does for a `buffer_reference`, doesn't help. Neither does glslang's shape, a block with a runtime array and one chain through it, and Slang lowers an unsized array behind a pointer to an `OpPtrAccessChain` anyway. No Slang option or source form avoids either shape, and the `shader-slang` port installs prebuilt binaries, so Slang cannot be patched through an overlay port either.

**The fix** is `tools/spirv_for_adreno.py`, which the Android build runs over every module after `slangc` ([rendering.md](../rendering.md#shaders)). It folds chained access chains on a buffer pointer into one chain from the root pointer, splits a whole struct or array loaded through one into a load per member with an `OpCompositeConstruct`, removes the chains left unused, and sorts the types so no pointer is used before it is defined. It rewrites 7 of the 11 engine modules, and the output passes `spirv-val`. The desktop is unchanged: its modules are not rewritten, and the editor's screenshots of the basic sample's main scene and of the playground after `--play 3` are byte-identical with the rewritten modules in place of the originals. The editor was checked to read those files by giving it a truncated one. The rewrite needs no C++ and no device check. Its test, `renderer_spirv_for_adreno`, runs it over the engine's modules on every desktop build and checks that neither shape is left, that a second run changes nothing, and that `spirv-val` accepts the result when the SDK is installed.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked by the Linux `sonnet_cook` packaged:

- All 29 pipelines from the 11 modules build. The bundle opens (27 assets), the game plays `Basic` with 15 entities, the render graph allocates its targets at 1080×2340, and **the basic sample renders**: lit and shadowed, the sky from the environment, bloom and tonemapping, with the crate turning. Nothing is logged at warning level or above, apart from the missing validation layer, which is expected.
- A bundle pushed to `/data/local/tmp`, copied into `files/` and given through `--es args` as `/data/user/0/io.github.pacheco95.sonnet/files/game.sbundle` opens and plays the same way.
- `android-release`, with the same bundle, also plays.

Still to do before the phone runs a game as the desktop does: ASTC cooking and `CookPlatform::android`, touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

## ASTC texture cooking

The fifth Android step cooks a phone's textures as ASTC, as ADR-0018's "Textures" section decides ([assets.md](../assets.md#textures)):

- **`rhi`** has `ASTC4x4Unorm`, `ASTC4x4Srgb`, `ASTC6x6Unorm` and `ASTC6x6Srgb`, and `DeviceInfo::astcSupported`, enabled where `textureCompressionASTC_LDR` is present, apart from BC, since vk-bootstrap enables a feature structure only when all of it is there. `formatSupported` says whether a device samples a format, and `createImage` asserts it. The device's log line lists `BC` and `ASTC`, and the renderer's debug line for each texture names its format ([rendering.md](../rendering.md#the-rhi-module-today)).
- **Loading.** `readKtx2` takes the device's `DeviceInfo`. It transcodes UASTC to BC7 with block compression, to ASTC 4×4 with ASTC and no BC, and to RGBA8 with neither, uploads an ASTC file as it is, and refuses a format the device cannot sample, which falls back as a failed import does.
- **The cook.** `CookPlatform` has `android` and `ios`, taken by `sonnet_cook --platform` and the export dialog. For them the cook asks `AssetDatabase::mobileTexture`, which encodes a compressed texture's RGBA8 levels, decoded from the source, with `ktxTexture2_CompressAstcEx`: 6×6 perceptual for sRGB, 4×4 for linear, medium quality, no normal-map mode. It caches the result as `<uuid>.astc.ktx2` under the UASTC entry's freshness rule. Uncompressed textures stay RGBA8 and the environment RGBA16F. Android and iOS cook the same bytes.
- **`Game::open`** refuses a bundle whose platform's textures the device cannot sample, before anything in it loads, and names the platform. A desktop bundle runs everywhere and a mobile one needs `astcSupported` (`assets::canRun`, [player.md](../player.md#opening-a-game)).
- **Export.** Exporting for Android or iOS writes the bundle alone, and the dialog says the editor builds no APK or app bundle ([editor.md](../editor.md#export)).
- **CI.** The Android job builds `sonnet_cook` for the host, cooks the basic sample for `android` and packages it into the APK ([build.md](../build.md#continuous-integration)).

**Tests.** `assets_tests` encodes FlightHelmet's glass-and-plastic base colour (sRGB, 6×6) and normal map (linear, 4×4), downscaled to 512×512, decodes them back with `ktxTexture2_DecodeAstc` and holds each above a PSNR floor. At 2048×2048 the base colour measures 48.7 dB, ADR-0018's figure. At 512×512 the two measure 45.4 dB and 48.8 dB, the same with GCC 14 and Clang 22, and the floors are 44.4 dB and 47.7 dB. The test uses FlightHelmet (CC0) rather than DamagedHelmet, whose textures come from an original under CC BY-NC 4.0 as well as the CC BY 4.0 that ADR-0018 names. The ADR measured this base colour too. A mobile cook of the basic sample has ASTC texture payloads (read from the KTX2 header, not transcoded) and names `android`. A second mobile cook rewrites no cache entry and gives iOS the same bytes, and a newer source is encoded again. The test that proved the second cook read the cache found a bug on the way: the freshness check's default sidecar time was the filesystem clock's epoch, which libstdc++ puts in 2174, so a glTF image was never fresh. `runtime_tests` has the null device, which reports BC and no ASTC as a desktop GPU does, refuse an Android and an iOS bundle and open the Linux one. `rhi_tests` uploads a solid-colour image of 13×7 texels with every mip level and samples its first and last levels, in BC7 and in ASTC 4×4 and 6×6. The ASTC cases skip without `astcSupported`. The RTX 4090, RADV and Lavapipe all report `textureCompressionASTC_LDR = false`, as ADR-0018 says, so neither this machine nor CI runs them. They run on Apple silicon and phones.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The desktop is unchanged. A Linux cook of the basic sample is byte-identical to the one packaged in the previous step. The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.
- The basic sample's Android bundle is 702 367 bytes against the Linux one's 682 714. Its three textures are small, so the first cook, which encodes them, takes 0.38 s and a second one 0.32 s.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` by the Linux `sonnet_cook`:

- The device's line ends `BC, ASTC`: **the phone has BC as well as ASTC**, as ADR-0018's question 6 recorded. So a Linux bundle's UASTC was transcoded to BC7 on it, not to RGBA8, both before this step and after it. The ASTC 4×4 target applies to a device with ASTC and no BC.
- The bundle opens ("cooked for android"), and the textures upload as ASTC: the checker and the crate's base colour as `ASTC6x6Srgb`, the crate's normal map as `ASTC4x4Unorm`. The sample plays with 15 entities, and nothing is logged at warning level or above but the missing validation layer.
- `adb exec-out screencap -p` shows the sample drawn as with the Linux bundle, compared by eye: the same lighting, shadows, sky and checker. Only the crate's turn and the reed's sway differ, with the moment of the capture.
- The Linux bundle, copied into `files/` to override the packaged one, uploads its textures as `BC7Srgb` and `BC7Unorm`.
- A build of this step with `blockCompressionSupported` forced off, for one run and not committed, transcoded the same Linux bundle to `ASTC4x4Srgb` and `ASTC4x4Unorm` and drew the same frame. That is the path a phone without BC takes.

A mobile bundle's ASTC is stored without supercompression, while the desktop's UASTC has zstd, so a texture can take more of the APK than it does of a desktop bundle. The crate's flat 128×128 normal map is 22 KB of ASTC 4×4 against 0.8 KB of UASTC. On the GPU a colour texture takes less than half: 3.56 bits per texel against BC7's 8. zstd over ASTC is a possible later change, which `readKtx2` would read as it is.

Still to do before the phone runs a game as the desktop does: touch input, the swapchain's suspend and resume with the lifecycle, and the capture in the player.

## Touch input

The sixth Android step gives the game the fingers, as ADR-0018's "Where it lives" decides for `platform` and `scripting`:

- **`platform`** translates SDL's finger events into `TouchDown`, `TouchMotion` and `TouchUp`, with SDL's finger id, a cancelled finger ending as a lifted one. Positions are in window coordinates, SDL's fraction of the window times its size, which is where SDL puts the mouse it synthesises from the same finger ([platform.md](../platform.md#events)). `InputState` keeps up to ten fingers in the order they went down, each with this frame's motion, which `beginFrame` zeroes like the mouse's.
- **Touches and the mouse.** The synthesised mouse stays on, so the first finger is still the left button. A finger reaches `InputState` twice, as a touch and as the mouse, and each report stays whole: SDL moves the mouse for the first finger only, the move to where a finger lands keeps its position with a zero delta rather than counting as motion from where the last finger lifted, and the touches SDL makes from a mouse (on by default on Android and iOS) or a pen are dropped, since those arrive as the mouse already.
- **`scripting`.** `input.touches()` returns `{id, position, delta}` for each finger, with the mouse's coordinates and table shape. Lua had no way to turn a screen point into a world ray, so `camera.ray(point)` returns `{origin, direction}` through a point of the view, for `physics.raycast` ([scripting.md](../scripting.md#camera)). The application hands the runtime the camera it draws through and the view's size every frame: the player the scene camera over its window, the editor its viewport camera over the image. The unprojection is the editor gizmo's, moved into `renderer::rayDirection` so both use it.
- **The editor** hands a touchscreen's fingers to the game as it does the mouse, relative to the viewport image, and only a finger that lands on the image ([editor.md](../editor.md#play-mode)). Dear ImGui and the viewport see only the synthesised mouse.
- **`player.lua`** rolls the ball towards the point on the ground under the first finger held, with the same force as a key, alongside W, A, S, D and Space.

**Tests.** `platform_tests` translates synthetic finger events (two fingers down at once, motion, up and cancel) in a headless window of known size, drops a mouse's and a pen's touches, and zeroes a finger's landing motion. `InputState` touches appear, move with a motion that resets each frame and end, and end with the focus. `scripting_tests` reads `input.touches()` with its fields and types and `camera.ray` through a known view, and `renderer_tests` checks the ray through the centre and a corner of a known camera and where a pitched camera's centre meets the ground.

Verified on Linux: `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe. The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` with `--scene scenes/playground.scene.json`, since the player takes no `--scene` yet:

- The player draws the playground in portrait at 1080×2340. The window's creation logs the 1280×720 it asked for, and Android resizes it to the surface: from then on its size and pixel size are both 1080×2340, so positions are pixels on the phone.
- A finger held with `adb shell input motionevent DOWN 300 1100`, moved a pixel at a time for about three seconds and lifted, arrived as one touch at (300.3, 1100.5) with the synthesised mouse at the same point and the left button down. A debug line in `player.lua`, for the run only, showed the ray from the scene camera meeting the ground at (−2.42, 0, −2.42) and the ball rolling from (0, 0.5, 5) to (−2.43, 0.5, −0.02) while the finger was held. `adb exec-out screencap -p` frames before, during and after show the ball leaving the bottom of the screen and reaching the finger.
- An `adb shell input swipe` over three seconds moved the ground point from (−2.4, −2.4) to (1.4, 4.6), and the ball followed it.
- Nothing was logged at warning level or above but the missing validation layer, and there was no native crash.

ADR-0018's check 5 needs a person's hand on the device. The `adb` run above was the agent's. Michael then confirmed the check by hand on the Galaxy S25 Ultra: a finger held on the playground rolls the ball towards it. ADR-0018 says "window pixels" for a touch's position. The engine uses window coordinates, the mouse's, which are pixels on Android and logical coordinates on a high-density desktop display.

Still to do before the phone runs a game as the desktop does: the swapchain's suspend and resume with the lifecycle, and the capture in the player.

## The mobile lifecycle

The seventh Android step lets the player go to the background and come back, as ADR-0018's "Where it lives" decides for `platform`, `rhi` and the player:

- **`platform`** translates SDL's lifecycle events into `WillEnterBackground`, `DidEnterForeground`, `LowMemory` and `Terminating`. `DidEnterBackground` and `WillEnterForeground` get no type, since on Android each comes straight after its partner and on iOS nothing is left to do at either ([platform.md](../platform.md#background-and-foreground)).
- **`rhi`.** `ISwapchain::suspend` waits for the device to go idle and releases the images, the swapchain and the surface. `resume` creates the surface and the swapchain again from the window and returns an `Error` rather than throwing into the event. While suspended `acquire` returns nothing, as for a minimised window. A surface lost before the suspend, which Android can do (below), is caught in `acquire` and present: one warning, no image, and the `suspend` and `resume` that follow recover it. Both transitions log at `info`, the resume with its extent. The null device's swapchain has the same calls ([rendering.md](../rendering.md#suspend-and-resume)).
- **`audio`.** `IAudioDevice::pause` and `resume` stop and start miniaudio's device, so nothing is heard in the background and every sound carries on where it was. Without an output device nothing is mixed while paused ([audio.md](../audio.md#pausing)).
- **The player** waits for idle, suspends the swapchain and pauses the audio on `WillEnterBackground`, and resumes both on `DidEnterForeground`, where it also restarts its frame clock. `LowMemory` and `Terminating` are logged. The editor, which runs only on the desktop, is unchanged ([player.md](../player.md#the-lifecycle)).

**What SDL's source and the phone said**, where ADR-0018 assumed:

- **The surface goes before the event.** ADR-0018 says the events arrive "before the OS takes the surface away". On Android they do not: SDL's `surfaceDestroyed` queues the pause for the native thread and releases the `ANativeWindow` in the same call, on the UI thread, without waiting for a Vulkan window. In the two trips logged with SDL's own lines, `surfaceDestroyed()` came 3 and 6 ms before the engine heard `WillEnterBackground`. Once in ten trips, on the screen-off, a frame fell in between, and `vkAcquireNextImageKHR` returned `VK_ERROR_SURFACE_LOST_KHR`. The swapchain logged the warning, drew nothing that frame and recovered through the suspend and resume. Before this step the recreate path in `acquire` would have thrown on a lost surface.
- **Nothing iterates in the background.** With `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` at its default, SDL's event pump blocks until the resume, so `SDL_AppIterate` is not called. The player counted no frame in any of the ten background periods.
- **The window keeps its size.** 1080×2340 before and after, with no `WindowResized`, in portrait.
- **`LowMemory` comes every time.** SDL maps every `onTrimMemory` to it, and Android trims a hidden application's UI.
- **`dt`.** The frame clock clamps to 0.1 s and the world to four fixed steps a frame, so without the restart the first frame back would have simulated 1/15 s of the time away. With it, that frame simulates its own time.

**Tests.** `platform_tests` translates the four events and drops the other two. `rhi_tests` suspends a Lavapipe headless swapchain after three frames, acquires nothing while it is suspended, even after a resize request, resumes it at 320×200 and draws three more frames, and checks that a second `suspend` and a `resume` without a suspend do nothing more, all with validation silent. Both cases skip where the other swapchain cases skip, which includes the RTX 4090. The null device's swapchain gets the same checks. `audio_tests` pauses a quarter-second sound a tenth of a second in, mixes nothing for a second, and hears its remaining 0.15 s after the resume. The player's handling is in `apps/player/main.cpp`, which no test reaches, so `runtime_tests` plays the basic sample through the same steps on Lavapipe instead: frames that simulate while acquiring nothing and mixing nothing, then draw into the present pass again, with nothing warned about.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The editor's `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to `main`'s on the RTX 4090.
- The desktop player minimised and restored twice through the window manager (GNOME on X11) behaves as `main`'s does: the same swapchain lines, nothing warned about, the scene drawn after each restore and a clean exit. No lifecycle event arrives there.

**On the Galaxy S25 Ultra**, with the `android-debug` APK and the basic sample cooked for `android` with `--scene scenes/playground.scene.json`:

- **Ten trips to the background**, sent by `adb`: seven with the home button and `am start` to come back (two of them 65 s long, the second on a probe bundle, below), two pairs of `KEYCODE_APP_SWITCH` (the second press switches to the previous app, so each pair is one trip), and a screen-off with `KEYCODE_SLEEP`. After `KEYCODE_WAKEUP` the phone stayed on its lock screen with the player in the background until it was unlocked by hand, and then the player came back.
- **Every trip logged** `entering the background`, `swapchain suspended` and `audio paused`, then `back from the background after N s, 0 frames in it`, `swapchain resumed at 1080x2340` and `audio resumed`, with no error. The one warning was the lost surface above, on the screen-off. `adb logcat -b crash` stayed empty throughout. Suspending took about 5 ms and resuming about 3 ms, the audio's stop and start 10 to 20 ms more. The audio output was miniaudio's own device (`audio ready: 48000 Hz, 2 channels, output device`), stopped and started with no warning.
- **`adb exec-out screencap -p` frames** before and after each trip show the playground drawing, with the sweeper turned further and the cube pile it knocks over moved on. In the 65 s trip the ball, rolling towards a held finger when the player left, carried its momentum on after the return.
- **Time away is not simulated.** With a debug line in `player.lua` for one run only (not committed), in a bundle copied into `files/` to override the packaged one and removed afterwards, the ball jumped from the ground at game time 11.92 s and was at 1.48 m, the top of the jump, when the player went to the background at 12.34 s. After 64.8 s away, its first frame back was at 12.36 s, with a `dt` of 15 ms and the ball still at 1.48 m. It then fell and landed at 13.01 s: a jump of about 1.1 s of game time across 65 s of real time.

ADR-0018's check 4 needs a person's hand on the device. The runs above were the agent's, through `adb`, apart from unlocking the phone after the screen-off. Michael then confirmed the check by hand on the Galaxy S25 Ultra: the player goes to the background and comes back, still drawing.

Still to do before the phone runs a game as the desktop does: the capture in the player.

## The capture in the player

The eighth Android step gives the player the editor's capture, as ADR-0018's "Device checks and reports" decides, and runs the device checks with it ([player.md](../player.md#capture-runs)):

- **`runtime` has the capture.** The flag table, its parser, the run that steps a capture through its frames (`CaptureRun`, driven through `ICaptureTarget`) and the screenshots' readback and PNG writing (`Screenshots`, with stb's writer) moved from `editor` into `runtime`. Each flag in the table says whether the player takes it: `--screenshot`, `--scene`, `--play`, `--shading-term` and `--settle-frames` yes, `--screenshot-window` and `--select` no. The editor keeps its API as a thin layer, `editor::CaptureRun` stepping the shared run through the editor, and its tests are unchanged. ADR-0018 says `editor` already linked `runtime`. It did not: it linked `audio`, `scripting` and `ui`. It links `runtime` now, which comes earlier in the module order.
- **`--play` without floating-point `from_chars`.** `runtime::parseSeconds` reads the integer and fraction digits and the exponent itself, and scales them by a power of ten in a double. It takes exactly what `from_chars` took before: an optional minus, digits with at most one point, an exponent only when it is whole, and a value in float's range that is finite and not negative. So `2.5`, `.5`, `5.`, `1e3` and `-0` are accepted, and `+1`, ` 1`, `1e`, `0x1`, `inf`, `-0.5`, `1e39` and `1e-50` are refused. A fuzz run for this step, not committed, put seven million inputs through it and through floating-point `from_chars`, with GCC 14 and again with Clang 22: random strings over digits, `.`, `e`, `E`, `+`, `-`, `x` and space, and plausible decimals and exponents. It gave no difference in what was accepted and none in a value's bits.
- **The player.** `sonnet_player [game] [capture flags]` in any order, with the game optional and `game.sbundle` from the content root without one, on a phone too. `--scene` alone makes a plain run of another scene, which replaces cooking with `sonnet_cook --scene`, since a default cook keeps every scene. A capture run opens the game paused (`GameDesc::paused`), so the world waits for the assets and draws the settle frames before it simulates, as the editor does before it plays. A relative output resolves against `Platform::prefPath("sonnet", "player")`, which on Android is the app's `files/` (SDL ignores the names there). The run logs the device and its Vulkan version, then the frame times over its last hundred frames. Those are the mean CPU time of the simulation and recording, the mean time between frames, and each pass's GPU time, leaving out the frame that copies the screenshot. A mistake in the arguments is logged before any window opens, since a phone shows only the log. The log's last line, `exit ok` or `exit with failure`, was already the platform's and is how a phone reports the exit code.

**Tests.** `runtime_tests` parses the player's five flags with the game anywhere among them, refuses the editor's two with a pointer to `--help`, and makes `--scene` alone a plain run for the player and an error for the editor. It checks that the two usages come from one table, and puts `--play` through 18 accepted values, three minus zeros and 29 refused ones. A relative path resolves under the preferences directory and an absolute one stays. The frame times average the last hundred frames of a ring. On Lavapipe, a capture parsed as the player parses it (`--play 0.25 --shading-term albedo --screenshot shots/albedo.png`) runs the basic sample from a paused game to `Done`. It lands under `prefPath("sonnet", "runtime_tests")` at the window's 640×480 with something lit in the middle, and nothing is warned about. A cooked bundle takes `--scene scenes/playground.scene.json`, and a scene it lacks fails the run with nothing written. That is as far as the fixtures go: `apps/player/main.cpp`, which parses the real `argv`, resolves against the real `prefPath` and logs the last line, is reached by no test, as for the lifecycle. `editor_tests` pass unchanged. `tools/check_docs.py` reads the table from `runtime` and holds [editor.md](../editor.md#screenshots) to all seven flags and [player.md](../player.md#capture-runs) to the player's five.

Verified on Linux:

- `build/linux-debug` (GCC 14) and `build/clang22` (Clang 22) build with no warnings, and all 14 suites pass on Lavapipe.
- The editor's `--help`, and its error messages and exit code for six bad command lines, are the same as `main`'s. Its `--screenshot` of the main scene, and of the playground after `--play 3`, are byte-identical to those of a `main` build in a worktree, on the RTX 4090.
- The desktop player's captures exit 0 with nothing warned about, and two playground runs with `--play 3` are byte-identical. They are not the editor's images of the same flags, and cannot be. The editor draws through its fly camera into its 968×662 viewport. The player draws through the scene's camera into its 1280×720 window.

**On the Galaxy S25 Ultra** (Android 16, portrait), with the `android-debug` APK and the basic sample cooked for `android` by the Linux `sonnet_cook` and packaged, all four checks the agent can do:

1. **The Vulkan description** (`adb shell cmd gpu vkjson`): the Adreno 830 at 1.3.284, with the same driver as ADR-0018's question 6 recorded (0x80320040). Every feature `VulkanDevice` requires is there: `multiDrawIndirect`, `drawIndirectFirstInstance`, `samplerAnisotropy`, `shaderDrawParameters`, the thirteen 1.2 features, `dynamicRendering`, `synchronization2` and `shaderDemoteToHelperInvocation`. So are the four 1.4 extensions and the swapchain. The limits cover what the engine needs: 256 bytes of push constants against 128, seven descriptor sets against two, and 16 777 216 of each update-after-bind descriptor kind against the bindless set's largest, 4 224 sampled images. `vkjson` lists the four extensions but not their feature bits, which the device selector checks when the player starts ("with the 1.4 features as extensions"). **Pass.**
2. **The launcher:** started by the agent with `adb shell monkey -p io.github.pacheco95.sonnet -c android.intent.category.LAUNCHER 1`, which sends the launcher icon's intent, not by a hand on the icon. The player drew the basic sample's start scene (`adb exec-out screencap -p`), and `adb logcat -b crash` stayed empty. **Pass.**
3. **Capture runs**, each started with `am start -S` and its arguments in `args`: the start scene with `--play 3` for `final`, `albedo` and `normal`, and the playground with `--scene scenes/playground.scene.json --play 3`. Each wrote a 1080×2340 PNG to `/data/data/io.github.pacheco95.sonnet/files/`, and each log ends in `exit ok`, with no error and no native crash. The one warning in each is the debug APK's missing validation layer, the exception every phone run above has had. `adb shell run-as ... cat` and `adb exec-out run-as ... cat` both pulled the PNGs byte for byte, matching `md5sum` on the phone. Compared by eye with the desktop player's captures of the same arguments, the portrait window shows the middle of the same view. Lighting, shadows, sky, bloom and checker match, the albedo has full colour rather than MoltenVK's old red-only reads, the normal term's colours match, and the playground's ball, spawned cube and sweeper stand where Linux has them after 3 s. A second `final` run, from an APK rebuilt at the end of the step, wrote the same bytes as the first. A run with a scene the bundle lacks, and one with the editor's `--select`, end in `exit with failure` with the reason on the line before. `--es args '--scene scenes/playground.scene.json'` alone plays the playground from the packaged bundle, without a special cook. **Pass**, the validation-layer warning aside.
4. **The background and back:** confirmed by Michael by hand in [the previous step](#the-mobile-lifecycle). This step changed nothing there.
5. **A finger on the playground:** confirmed by Michael by hand in [the touch input step](#touch-input).
6. **The frame times** of check 3, below. **Pass.**

The phone's screen stayed on through the runs, with "stay awake while charging" already set, so no run waited in the background.

| Capture run (Debug builds) | Galaxy S25 Ultra, 1080×2340: CPU / apart / GPU (forward) | RTX 4090, 1280×720: CPU / apart / GPU (forward) |
|---|---|---|
| start scene, `final` | 2.04 / 5.27 / 4.96 ms (3.38 ms) | 0.98 / 2.78 / 0.18 ms (0.030 ms) |
| start scene, `albedo` | 2.36 / 6.64 / 5.46 ms (3.96 ms) | 0.96 / 2.78 / 0.19 ms (0.032 ms) |
| start scene, `normal` | 2.36 / 6.69 / 5.56 ms (4.07 ms) | 0.96 / 2.78 / 0.19 ms (0.032 ms) |
| playground | 4.50 / 7.61 / 5.54 ms (3.99 ms) | 1.47 / 2.78 / 0.17 ms (0.023 ms) |

On the phone the forward pass is 68 to 73 % of the GPU frame, then FXAA (0.31–0.39 ms), the first bloom downsample (about 0.3 ms) and tone mapping (0.15 ms): the full-screen work at 2.5 million pixels. Every shadow cascade and the culling pass together stay under 0.3 ms. Frames came 5.3 to 7.6 ms apart, just above the GPU time and well above the CPU's 2 to 4.5 ms, so the phone is GPU-bound in these runs. The desktop and Mac numbers recorded above measure something else: [M7](../roadmap.md#m7-gpu-driven-rendering)'s `renderer_tests "[benchmark]"`, ten thousand draws and a hundred lights at 1080p in Release. That is 0.50 ms of GPU time a frame on the RTX 4090 and 2.1 ms on the M4 Max ([macOS could not create a device](roadmap-closed-work.md#macos-could-not-create-a-device)). The Mac has not made a capture run yet, which is M10's Mac check.

**M9's "done".** "Done when the basic sample runs on an Android 16 device." ADR-0018 defines running as its six checks, and on the Galaxy S25 Ultra all six now pass: 1, 2, 3 and 6 by the agent in this step, and 4 and 5 by Michael by hand. That holds in portrait only. Turned to landscape, the swapchain takes the surface's 90° pre-transform and the scene is drawn rotated and stretched ([#47](https://github.com/Pacheco95/sonnet/issues/47)). Every run here kept auto-rotate off and the phone in portrait. The checks do not ask for landscape, so the criterion holds as the ADR words it. But a player on a phone got a broken image by turning it, which is not a game that runs, so M9 stayed open until #47 was fixed ([Rotation](#rotation)).

What ADR-0018 got wrong, besides the NDK floor, the manifest's version and touch coordinates recorded above:

- `editor` did not link `runtime`.
- The debug APK always logs the missing validation layer at warning level, so check 3's "no warning" has that one exception on any phone.
- `am start` hands new arguments to a player that is running only with `-S`, which stops it first, and `adb shell` needs the whole command in one pair of quotes so that the `args` extra stays one string.
- SDL's pref path on Android is `/data/data/<package>/files/`, the same directory as `/data/user/0/<package>/files/` for the first user.
- The desktop and Mac numbers check 6 sets the phone's beside are a benchmark of another workload, so this step measured the desktop player's capture runs as well.

## Rotation

The last Android step fixes [#47](https://github.com/Pacheco95/sonnet/issues/47): turned to landscape, the player drew the scene rotated and stretched. vk-bootstrap takes the surface's current transform as the swapchain's pre-transform when none is set, and in landscape the S25 reports `Rotate90` with a 2340×1080 extent. The swapchain told Android its frames were already rotated, and the compositor turned them again. The swapchain now asks for the identity pre-transform where the surface supports it and lets the compositor rotate. With identity, every present in landscape reports suboptimal, which recreated the swapchain every frame when the fix was first tried: 321 times in 4.4 s. So a suboptimal whose surface extent is the swapchain's and whose transform alone differs keeps the swapchain ([rendering.md](../rendering.md#rotation)). The change is in `rhi` alone. The window already had the landscape size, so the renderer, the capture and the touches needed nothing.

Verified on Linux: `build/linux-debug` (GCC 14) builds with no warnings and all 14 suites pass on Lavapipe. The editor's viewport `--screenshot` of the playground is byte-identical to one from `main`. Its `--screenshot-window` is not, and neither are two window captures from the same build, which differ in the same few digits of text.

**On the Galaxy S25 Ultra**, with the playground cooked for `android`, packaged into the `android-debug` APK and turned with `adb shell settings put system user_rotation` (auto-rotate off):

- **Every orientation is upright and undistorted**: portrait, both landscapes and upside down, from `adb exec-out screencap -p`, with the whole playground in view in landscape.
- **The swapchain is created once per turn**: portrait to landscape made one 2340×1080 swapchain, and landscape back to portrait one 1080×2340 swapchain. Landscape to the other landscape and portrait to upside down made none. In each orientation, no further swapchain appeared in the log.
- **The compositor rotates in hardware**: `dumpsys SurfaceFlinger` lists the player's layer as `DEVICE` composition with `ROT_90` in landscape and `ROT_180` upside down, so the rotation costs no GPU pass.
- **Check 5 in landscape**: a finger held with `adb shell input motionevent DOWN` on the left of the board pulled the ball to the point under it.
- **Check 4 in landscape**: a trip to the recents screen and back, which stays in landscape, suspended the swapchain and resumed it at 2340×1080, upright. The home screen is portrait only, so a trip there comes back in portrait, which also resumed correctly.

With the scene upright in every orientation, and ADR-0018's six checks passing, M9 is done.

## M10: iOS export, detail

## The build

`ios-debug` and `ios-release` (the Xcode generator, `arm64-ios`, iOS 16.3) and the `moltenvk` overlay port landed in [PR #58](https://github.com/Pacheco95/sonnet/pull/58), the same shape as M9's Android presets: `cmake/SonnetIOS.cmake`'s `sonnet_add_ios_bundle()` for the app bundle, `apps/player/ios/` for its `Info.plist` and launch storyboard, and an iOS CI job on `macos-latest`. MoltenVK is linked into the player on every Apple platform, not just iOS, which is also what closed [the macOS export gap](roadmap-closed-work.md#the-macos-export-needs-the-vulkan-sdk) below.

Two bugs surfaced only once a real Mac ran the build, both the same root cause and neither caught by the presets' first green CI run: under the Xcode generator, a custom command's `COMMAND` arguments never get Xcode's own per-platform build setting (`${EFFECTIVE_PLATFORM_NAME}`) substituted by the script-phase shell that is supposed to do it, no matter which generator expression asks for the target's bundle or output directory. `cmake/SonnetIOS.cmake`'s `SONNET_IOS_BUNDLE` copy and `cmake/SonnetShaders.cmake`'s own copy of the compiled shaders both built their destination that way and both landed their files next to a directory literally named `RelWithDebInfo${EFFECTIVE_PLATFORM_NAME}` instead of `RelWithDebInfo-iphoneos`. Neither was caught by CI's first pass, which never gave `sonnet_player_app` a cooked bundle to place. Both are fixed the same way: build the destination from pieces CMake resolves on its own (`CMAKE_CURRENT_BINARY_DIR` and `$<CONFIG>`) plus a hardcoded `-iphoneos`, which is exact since this project never targets the simulator (`ports/moltenvk` ships no simulator slice). The iOS CI job now cooks a bundle and asserts the built `.app`'s contents directly, so a regression here fails CI instead of needing a Mac to catch it.

## The Mac-only checks

Before a phone was available, `agents/m10-mac-checks` ran what only needed the Mac: the exported macOS player, with every Vulkan SDK variable cleared, got past window and device creation and wrote a screenshot, closing [the macOS export gap](roadmap-closed-work.md#the-macos-export-needs-the-vulkan-sdk); `SONNET_IOS_BUNDLE` with a real cooked bundle landed `shaders/` and `game.sbundle` inside the built `.app`, confirming the fix above; and a build signed with a real Apple Development team, `codesign -dv` confirmed, closing over CI's `CODE_SIGNING_ALLOWED=NO` shortcut.

## The device checks

`agents/m10-device-checks` ran ADR-0018's six checks on an iPhone 15 Pro Max (iOS 27.0, the same phone [Checked before the code](#checked-before-the-code) probed), with `ios-release`, a real cooked bundle and a real signing team:

1. **The Vulkan description**: `Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC` from the device-selection log line, matching what `vkprobe` already found for this phone. **Pass.**
2. **The launcher**: installed and launched through `devicectl`, the start scene drawn with checkerboard, shadows, sky and every primitive in place; Michael then tapped the icon by hand for a fresh launch and confirmed the same. **Pass.**
3. **Capture runs**, `--play 3` for `final`, `albedo` and `normal` on the start scene and one of the playground: all four exited 0 and wrote correct PNGs — `albedo` and `normal` in full colour, not MoltenVK's old red-only reads, and the playground's ball, stacked boxes and sweeper mid-motion. Each log carries an exception the same shape as M9's check 3 had: a warning neither new nor iOS-specific, `[mvk-warn] ... Blending is enabled for attachment with format VK_FORMAT_R32_UINT`, from the id pass's picking buffer, also seen on the Mac-only run. **Pass, that warning aside** ([#59](https://github.com/Pacheco95/sonnet/issues/59) tracks it).
4. **The background and back**: confirmed by Michael by hand. The console shows `entering the background`, `swapchain suspended`, `audio paused`, 92 frames away, then `swapchain resumed`, `audio resumed`, and the scene still drawing.
5. **A finger on the playground**: confirmed by Michael by hand — the ball rolls toward the touch point.
6. **The frame times** of check 3, below. **Pass.**

| Capture run (Release build) | iPhone 15 Pro Max, 1290×2796: CPU / apart / GPU (forward) |
|---|---|
| start scene, `final` | 0.50 / 16.67 / 11.29 ms (6.95 ms) |
| start scene, `albedo` | 0.51 / 16.67 / 11.92 ms (7.34 ms) |
| start scene, `normal` | 0.49 / 16.67 / 10.53 ms (6.40 ms) |
| playground | 0.90 / 16.67 / 11.75 ms (5.42 ms) |

With ADR-0018's six checks passing, the R32_UINT warning tracked rather than blocking, M10 is done.
