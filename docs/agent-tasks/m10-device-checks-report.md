# M10 on-device checks

Report branch: `agents/m10-device-checks`, based on `origin/feat/ios-build-scaffold`. Report only; no engine changes.

Command output below is verbatim except privacy substitutions: `<team>` (also the personal signing identity), `<device>`, `<device name>`, and `~`. `CC` and `CXX` were unset for every command. Exit statuses are recorded separately from command output.

## Source commit

```sh
unset CC CXX
git rev-parse HEAD
```

```text
7942c533f3bb580679b88e8c101165f27c4eddd4
```

Exit status: `0`.

## Git identity: name

```sh
unset CC CXX
git config user.name
```

```text
Michael Pacheco
```

Exit status: `0`.

## Git identity: email

```sh
unset CC CXX
git config user.email
```

```text
mdpgd95@gmail.com
```

Exit status: `0`.

## macOS version

```sh
unset CC CXX
sw_vers
```

```text
ProductName:		macOS
ProductVersion:		26.7
BuildVersion:		25G229
```

Exit status: `0`.

## Xcode version

```sh
unset CC CXX
xcodebuild -version
```

```text
Xcode 26.3
Build version 17C529
```

Exit status: `0`.

## Device

Device discovery used `xcrun devicectl list devices`; model and OS were read with `xcrun devicectl device info details --device <device>`. Only the requested model and OS fields are reproduced.

```text
marketingName: iPhone 15 Pro Max
osVersionNumber: 27.0
```

## Deviations and prerequisites

- The host cook executable and `build/macos-release/CMakeCache.txt` did not exist. The prompt’s `cmake -e --build` form was corrected to `cmake --build`; a configure was required first. Both compiler paths explicitly select Apple Clang.
- The first host build failed on system Vulkan headers. The retry sets the Xcode SDK explicitly, following this Mac’s local preset configuration; no source or third-party files were changed.
- The two referenced prior Mac reports are absent at the source commit. The existing baseline evidence in `docs/roadmap.md` was read; no Mac export checks were repeated.
- The malformed albedo/normal and copy commands in the prompt are interpreted using `docs/player.md`: `--shading-term albedo`, `--shading-term normal`, and the full app container source path.
- Device setup reported paired, wired, and Developer Mode enabled. Installation succeeded; the first launch was denied by iOS (verbatim below). A phone-side profile trust step was requested.
- During setup, the console redactor missed a device hostname and a local path in a Python exception. The filter was corrected. Those private strings are excluded from this report.
- Existing untracked `release.log` and `relwithdebinfo.log` were left untouched.

## Host configure

```sh
unset CC CXX
cmake --preset macos-release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
```

```text
-- Running vcpkg install
Detecting compiler hash for triplet arm64-osx...
Compiler found: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++
The following packages will be built and installed:
    catch2:arm64-osx@3.16.0
  * egl-registry:arm64-osx@2025-05-27
    fastgltf:arm64-osx@0.9.0
    flecs:arm64-osx@4.1.6
  * fmt:arm64-osx@12.2.0#1
    glm:arm64-osx@1.0.3
    imgui[core,docking-experimental,sdl3-binding,vulkan-binding]:arm64-osx@1.92.9#3 -- ~/repositories/sonnet/./ports/imgui
    joltphysics[core,rtti]:arm64-osx@5.6.0#1
    ktx:arm64-osx@4.4.2#3 -- ~/repositories/sonnet/./ports/ktx
    lua[core,cpp]:arm64-osx@5.5.1#1 -- ~/repositories/sonnet/./ports/lua
    miniaudio:arm64-osx@0.11.25
    moltenvk:arm64-osx@1.4.2 -- ~/repositories/sonnet/./ports/moltenvk
    nlohmann-json:arm64-osx@3.12.0#3 -- ~/repositories/sonnet/./ports/nlohmann-json
  * opengl-registry:arm64-osx@2026-08-03
  * pthreads:arm64-osx@3.0.0#14
    sdl3[core,vulkan]:arm64-osx@3.4.16#2 -- ~/repositories/sonnet/./ports/sdl3
    shader-slang:arm64-osx@2026.7.1#1
  * simdjson[core,deprecated,exceptions,threads,utf8-validation]:arm64-osx@4.6.8
    sol2:arm64-osx@3.5.0#1
    spdlog[core,fmt,tz-offset]:arm64-osx@1.17.0#1
    stb:arm64-osx@2024-07-29#1
    tracy[core,crash-handler]:arm64-osx@0.13.1#1
  * vcpkg-cmake:arm64-osx@2025-08-07
  * vcpkg-cmake-config:arm64-osx@2026-07-21
    vk-bootstrap:arm64-osx@1.4.357
  * vulkan:arm64-osx@2023-12-17
    vulkan-headers:arm64-osx@1.4.357.0
    vulkan-loader:arm64-osx@1.4.357.0
  * vulkan-memory-allocator:arm64-osx@3.4.0
    vulkan-memory-allocator-hpp:arm64-osx@3.4.0
  * zstd:arm64-osx@1.5.7
Additional packages (*) will be modified to complete this operation.
Restored 31 package(s) from ~/.cache/vcpkg/archives in 2.3 s. Use --debug to see more details.
Installing 1/31 vcpkg-cmake-config:arm64-osx@2026-07-21...
vcpkg-cmake-config:arm64-osx@2026-07-21 package ABI: 3e1d4f52b8ee537f7c2dfd936f8845f7f72e485c4a58c7dcbd0d7731647cd9aa
Elapsed time to handle vcpkg-cmake-config:arm64-osx: 2.23 ms
Installing 2/31 vcpkg-cmake:arm64-osx@2025-08-07...
vcpkg-cmake:arm64-osx@2025-08-07 package ABI: 0d9cda2594756cf215135d67d181a08b56935a9d88ca11a5cd8ddb8e008927a6
Elapsed time to handle vcpkg-cmake:arm64-osx: 2.29 ms
Installing 3/31 catch2:arm64-osx@3.16.0...
catch2:arm64-osx@3.16.0 package ABI: 4933c7b0c5132bf1bfc6c5231aced832bb2a95cdcd5c2b828d26973c9507e752
Elapsed time to handle catch2:arm64-osx: 48.8 ms
Installing 4/31 simdjson[core,deprecated,exceptions,threads,utf8-validation]:arm64-osx@4.6.8...
simdjson[core,deprecated,exceptions,threads,utf8-validation]:arm64-osx@4.6.8 package ABI: ed71b28107b42bed268e6dcd0e4ff9ea0e337a8f5889c14531d78f05d7875319
Elapsed time to handle simdjson:arm64-osx: 5.14 ms
Installing 5/31 fastgltf:arm64-osx@0.9.0...
fastgltf:arm64-osx@0.9.0 package ABI: 7d22d3310ab15253eb9ceb177802ccdfa722a8804c7eb99dfdedbd908a587e8c
Elapsed time to handle fastgltf:arm64-osx: 5.56 ms
Installing 6/31 flecs:arm64-osx@4.1.6...
flecs:arm64-osx@4.1.6 package ABI: f85d2fdc90cf79dbc198e743283506fe5e29e21650642ca70fdc4c916430f1ed
Elapsed time to handle flecs:arm64-osx: 34.7 ms
Installing 7/31 glm:arm64-osx@1.0.3...
glm:arm64-osx@1.0.3 package ABI: 05c603e07c33157355343d67c7c57ffdacd3a114d773aec806740b78ac781564
Elapsed time to handle glm:arm64-osx: 105 ms
Installing 8/31 vulkan-headers:arm64-osx@1.4.357.0...
vulkan-headers:arm64-osx@1.4.357.0 package ABI: 58a25a91f9b4e645493e739ef3719e5871bad337c5a17f75b172532487832777
Elapsed time to handle vulkan-headers:arm64-osx: 19.9 ms
Installing 9/31 sdl3[core,vulkan]:arm64-osx@3.4.16#2...
sdl3[core,vulkan]:arm64-osx@3.4.16#2 package ABI: 9afe9acb02780b5c06a93a1357720a7be822c7becb3934fbc3aa2df4181f940c
Elapsed time to handle sdl3:arm64-osx: 26 ms
Installing 10/31 imgui[core,docking-experimental,sdl3-binding,vulkan-binding]:arm64-osx@1.92.9#3...
imgui[core,docking-experimental,sdl3-binding,vulkan-binding]:arm64-osx@1.92.9#3 package ABI: 9f813a0576904618f8b2f323cc90b75456202f2bdfb8f8dcf7d362066549a3c6
Elapsed time to handle imgui:arm64-osx: 7.37 ms
Installing 11/31 joltphysics[core,rtti]:arm64-osx@5.6.0#1...
joltphysics[core,rtti]:arm64-osx@5.6.0#1 package ABI: 16065675834173ddec6e1381d218eba02dac222675d0402f4df2089b6425447b
Elapsed time to handle joltphysics:arm64-osx: 95.1 ms
Installing 12/31 zstd:arm64-osx@1.5.7...
zstd:arm64-osx@1.5.7 package ABI: 8e485bdcaba327e165e9d1607165f219a248ff7c058a9d928215edde370c3234
Elapsed time to handle zstd:arm64-osx: 6.55 ms
Installing 13/31 egl-registry:arm64-osx@2025-05-27...
egl-registry:arm64-osx@2025-05-27 package ABI: 6104f3861a769d5fd5dab7e829db7f6c11a70794804fc67601ca0a4be8d10df0
Elapsed time to handle egl-registry:arm64-osx: 4.73 ms
Installing 14/31 opengl-registry:arm64-osx@2026-08-03...
opengl-registry:arm64-osx@2026-08-03 package ABI: 7f1e81efd03e1379f025e793c968532365615baf23b3820bc808f348d81155a3
Elapsed time to handle opengl-registry:arm64-osx: 11.5 ms
Installing 15/31 ktx:arm64-osx@4.4.2#3...
ktx:arm64-osx@4.4.2#3 package ABI: c02ce2ae884929a03aec6ccd458b4df7640355f1cbb925eda0dac361280b975d
Elapsed time to handle ktx:arm64-osx: 10.8 ms
Installing 16/31 lua[core,cpp]:arm64-osx@5.5.1#1...
lua[core,cpp]:arm64-osx@5.5.1#1 package ABI: 50af2624b87c7691a23df177b962ccedbe8317479219a8a6c0d726c95e71ac38
Elapsed time to handle lua:arm64-osx: 6.78 ms
Installing 17/31 miniaudio:arm64-osx@0.11.25...
miniaudio:arm64-osx@0.11.25 package ABI: a131a04e1e9687aa52d9d1a28a97aee7251c6985c81b8f5e34f9e2bf6da201b8
Elapsed time to handle miniaudio:arm64-osx: 2.89 ms
Installing 18/31 moltenvk:arm64-osx@1.4.2...
moltenvk:arm64-osx@1.4.2 package ABI: 6b0ca342b520afc82c85429ad3b2438c475819483ee4434262cc43729b7b0dfb
Elapsed time to handle moltenvk:arm64-osx: 2.98 ms
Installing 19/31 nlohmann-json:arm64-osx@3.12.0#3...
nlohmann-json:arm64-osx@3.12.0#3 package ABI: b1cbba71ef623bc5d7d22ac74806100aeccfc922d6989ba56b7767cf15757e67
Elapsed time to handle nlohmann-json:arm64-osx: 22.3 ms
Installing 20/31 shader-slang:arm64-osx@2026.7.1#1...
shader-slang:arm64-osx@2026.7.1#1 package ABI: b5a58bd1b9c5c1d20cbf29b9411de3caebe2fea76e772db7cc3100d04528d1c1
Elapsed time to handle shader-slang:arm64-osx: 18.2 ms
Installing 21/31 sol2:arm64-osx@3.5.0#1...
sol2:arm64-osx@3.5.0#1 package ABI: ca8e7adaa85815b712542c926eef8bb6e1e83d638d350b828eb4d95c560c84a4
Elapsed time to handle sol2:arm64-osx: 34.5 ms
Installing 22/31 fmt:arm64-osx@12.2.0#1...
fmt:arm64-osx@12.2.0#1 package ABI: 4655193d221bb08626e989d8a2774ab384f49f05b5ff01b3cc94b176adf0ccb5
Elapsed time to handle fmt:arm64-osx: 10.7 ms
Installing 23/31 spdlog[core,fmt,tz-offset]:arm64-osx@1.17.0#1...
spdlog[core,fmt,tz-offset]:arm64-osx@1.17.0#1 package ABI: a73fef1858d5c1da06a6af2c04e6e239392ca846bc2180910832ac2fdadd2b61
Elapsed time to handle spdlog:arm64-osx: 32.2 ms
Installing 24/31 stb:arm64-osx@2024-07-29#1...
stb:arm64-osx@2024-07-29#1 package ABI: 2aebd50316a183002f90ab283f1fc41b768826f51f94d9fff1e947ea785632db
Elapsed time to handle stb:arm64-osx: 9.85 ms
Installing 25/31 pthreads:arm64-osx@3.0.0#14...
pthreads:arm64-osx@3.0.0#14 package ABI: bfbcd3eac61a5db2bdea97e3af8bce7beaac0547641c1888de2a6a69241f9389
Elapsed time to handle pthreads:arm64-osx: 1.81 ms
Installing 26/31 tracy[core,crash-handler]:arm64-osx@0.13.1#1...
tracy[core,crash-handler]:arm64-osx@0.13.1#1 package ABI: 10a4c63c98a40e33651f2059b9910bf28c63212e6441a392031972b989050c75
Elapsed time to handle tracy:arm64-osx: 18.2 ms
Installing 27/31 vk-bootstrap:arm64-osx@1.4.357...
vk-bootstrap:arm64-osx@1.4.357 package ABI: 79457d0c5311c76094920c94263e2b64ce1df78bd01d309561064565405c8e4d
Elapsed time to handle vk-bootstrap:arm64-osx: 6.81 ms
Installing 28/31 vulkan-loader:arm64-osx@1.4.357.0...
vulkan-loader:arm64-osx@1.4.357.0 package ABI: 6336d3ed980432b56d68cc5b2a0bcde472aa25a04e0d72deb3828be0efd02786
Elapsed time to handle vulkan-loader:arm64-osx: 7.28 ms
Installing 29/31 vulkan-memory-allocator:arm64-osx@3.4.0...
vulkan-memory-allocator:arm64-osx@3.4.0 package ABI: efdaa68309d12b6e0b75337c4b5ead59d3453d6fd9c4aa6c4ca8792ad7b0087c
Elapsed time to handle vulkan-memory-allocator:arm64-osx: 4.39 ms
Installing 30/31 vulkan:arm64-osx@2023-12-17...
vulkan:arm64-osx@2023-12-17 package ABI: f670818bc33d4a5dd38b94e65ca8de045c203122ef8dee4934f4bf5e67dddafa
Elapsed time to handle vulkan:arm64-osx: 3.17 ms
Installing 31/31 vulkan-memory-allocator-hpp:arm64-osx@3.4.0...
vulkan-memory-allocator-hpp:arm64-osx@3.4.0 package ABI: da80f771bbb7ce8ee3c963a0940bca9f791beeadcda2a7436fc6a582ae3cbf21
Elapsed time to handle vulkan-memory-allocator-hpp:arm64-osx: 6.51 ms
Installed contents are licensed to you by owners. Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Some packages did not declare an SPDX license. Check the `copyright` file for each package for more information about their licensing.
Packages installed in this vcpkg installation declare the following licenses:
(Apache-2.0 OR MIT)
(BSD-3-Clause OR GPL-2.0-only)
(MIT OR CC-PDDC)
(Unlicense OR MIT-0)
Apache-2.0
BSD-3-Clause
BSL-1.0
CC0-1.0
MIT
Zlib
catch2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Catch2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

catch2 provides pkg-config modules:

  # A modern, C++-native test framework for C++14 and above (links in default main)
  catch2-with-main

  # A modern, C++-native, test framework for C++14 and above
  catch2

fastgltf provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(fastgltf CONFIG REQUIRED)
  target_link_libraries(main PRIVATE fastgltf::fastgltf)

The package flecs provides CMake targets:

    find_package(flecs CONFIG REQUIRED)
    target_link_libraries(main PRIVATE $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)

The package glm provides CMake targets:

    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm)

    # Or use the header-only version
    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm-header-only)

Vulkan-Headers provides official find_package support:

    find_package(VulkanHeaders CONFIG)
    target_link_libraries(main PRIVATE Vulkan::Headers)

sdl3 provides CMake targets:

  find_package(SDL3 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE SDL3::SDL3)

imgui provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(imgui CONFIG REQUIRED)
  target_link_libraries(main PRIVATE imgui::imgui)

joltphysics provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Jolt CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Jolt::Jolt)

ktx provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Ktx CONFIG REQUIRED)
  target_link_libraries(main PRIVATE KTX::ktx)

lua provides CMake integration for the C library:

  find_package(Lua REQUIRED)
  target_include_directories(main PRIVATE ${LUA_INCLUDE_DIR})
  target_link_libraries(main PRIVATE ${LUA_LIBRARIES})

lua[cpp] provides a C++ library with exception handling:

  find_package(unofficial-lua)
  target_link_libraries(main PRIVATE unofficial::lua::lua-cpp)

miniaudio is header-only and can be used from CMake via:

  find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
  target_include_directories(main PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

moltenvk provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-moltenvk CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::moltenvk::moltenvk)

The package nlohmann-json provides CMake targets:

    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(main PRIVATE nlohmann_json::nlohmann_json)

The package nlohmann-json can be configured to not provide implicit conversions via a custom triplet file:

    set(nlohmann-json_IMPLICIT_CONVERSIONS OFF)

For more information, see the docs here:
    
    https://json.nlohmann.me/api/macros/json_use_implicit_conversions/

shader-slang provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(slang CONFIG REQUIRED)
  # note: 1 additional targets are not displayed.
  target_link_libraries(main PRIVATE slang::gfx slang::slang slang::slang-llvm slang::slang-glslang)

sol2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(sol2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE sol2::sol2)

sol2 provides pkg-config modules:

  # C++ <-> Lua Wrapper Library
  sol2

The package spdlog provides CMake targets:

    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog)

    # Or use the header-only version
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog_header_only)

The package stb provides CMake targets:

    find_package(Stb REQUIRED)
    target_include_directories(main PRIVATE ${Stb_INCLUDE_DIR})
tracy provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Tracy CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Tracy::TracyClient)

vk-bootstrap provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(vk-bootstrap CONFIG REQUIRED)
  target_link_libraries(main PRIVATE vk-bootstrap::vk-bootstrap vk-bootstrap::vk-bootstrap-compiler-warnings)

The package vulkan-loader provides the vulkan loader.
Please be aware of https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderApplicationInterface.md#bundling-the-loader-with-an-application

vulkan-memory-allocator-hpp provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)

All requested installations completed successfully in: 581 ms
-- Running vcpkg install - done
-- The CXX compiler identification is AppleClang 17.0.0.17000604
-- The C compiler identification is AppleClang 17.0.0.17000604
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/clang++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/clang - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Sonnet 0.11.0: rhi=Vulkan editor=ON player=ON tests=ON samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Found Python3: ~/.pyenv/versions/3.13.7/bin/python3.13 (found version "3.13.7") found components: Interpreter
-- Found Stb: ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include
-- Found nlohmann_json: ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version "3.12.0")
-- Configuring done (10.3s)
-- Generating done (0.1s)
-- Build files have been written to: ~/repositories/sonnet/build/macos-release
```

Exit status: `0`.

## Host build: first attempt

```sh
unset CC CXX
cmake --build --preset macos-release --target sonnet_cook_app
```

```text
[1/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Error.cpp.o
[2/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/File.cpp.o
[3/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlEntryPoint.cpp.o
[4/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Content.cpp.o
[5/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/InputState.cpp.o
[6/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/JobSystem.cpp.o
[7/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Camera.cpp.o
[8/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Mesh.cpp.o
[9/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlWindow.cpp.o
[10/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlEvents.cpp.o
[11/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Input.cpp.o
[12/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Picker.cpp.o
[13/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Uuid.cpp.o
[14/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Animation.cpp.o
[15/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Assert.cpp.o
[16/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Log.cpp.o
[17/59] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Version.cpp.o
[18/59] Linking CXX static library modules/core/libsonnet_core.a
[19/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Primitives.cpp.o
[20/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Texture.cpp.o
[21/59] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Platform.cpp.o
[22/59] Linking CXX static library modules/platform/libsonnet_platform.a
[23/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/RenderTarget.cpp.o
[24/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/RenderGraph.cpp.o
[25/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Asset.cpp.o
[26/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch
[27/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/MeshCook.cpp.o
[28/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Json.cpp.o
[29/59] slangc debug.slang
[30/59] slangc depth.slang
[31/59] slangc cluster.slang
[32/59] slangc cull.slang
[33/59] slangc forward.slang
[34/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Ktx2.cpp.o
[35/59] slangc ibl.slang
[36/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Cook.cpp.o
[37/59] slangc id.slang
[38/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanTypes.cpp.o
[39/59] slangc outline.slang
[40/59] slangc skin.slang
[41/59] slangc skybox.slang
[42/59] slangc post.slang
[43/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/Device.cpp.o
[44/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/ImageImporter.cpp.o
[45/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Project.cpp.o
[46/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Bundle.cpp.o
[47/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/GltfImporter.cpp.o
[48/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VmaImpl.cpp.o
[49/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o
FAILED: [code=1] modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o 
/usr/bin/clang++ -DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_EXPLICIT_CTOR -DGLM_FORCE_RADIANS -DSONNET_ASSERTS_ENABLED=1 -DSONNET_ENABLE_TRACY=1 -DSONNET_ENABLE_VALIDATION=0 -DSONNET_MODULE=\"rhi\" -DSONNET_RHI_VULKAN=1 -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DTRACY_ENABLE -DVK_NO_PROTOTYPES -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0 -I~/repositories/sonnet/modules/rhi/include -I~/repositories/sonnet/modules/rhi/src -I~/repositories/sonnet/modules/platform/include -I~/repositories/sonnet/modules/core/include -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/tracy -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/share/unofficial-vulkan-memory-allocator-hpp/../../include -O2 -g -DNDEBUG -std=c++2b -arch arm64 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wcast-align -Wunused -Wformat=2 -Wno-c2y-extensions -fmacro-prefix-map=~/repositories/sonnet/= -Werror -Winvalid-pch -Xclang -include-pch -Xclang ~/repositories/sonnet/build/macos-release/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch -Xclang -include -Xclang ~/repositories/sonnet/build/macos-release/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx -MD -MT modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o -MF modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o.d -o modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o -c ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9139:5: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 9139 |     PFN_vkGetLatencyTimingsLegacyNV fp_vkGetLatencyTimingsLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9402:5: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 9402 |     PFN_vkGetSleepStatusLegacyNV fp_vkGetSleepStatusLegacyNV = nullptr;
      |     ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9493:5: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 9493 |     PFN_vkLatencySleepLegacyNV fp_vkLatencySleepLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9536:5: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 9536 |     PFN_vkQueueNotifyOutOfBandLegacyNV fp_vkQueueNotifyOutOfBandLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9674:5: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 9674 |     PFN_vkSetLatencyMarkerLegacyNV fp_vkSetLatencyMarkerLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9684:5: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 9684 |     PFN_vkSetLatencySleepModeLegacyNV fp_vkSetLatencySleepModeLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9714:5: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 9714 |     PFN_vkShutdownLatencyDeviceLegacyNV fp_vkShutdownLatencyDeviceLegacyNV = nullptr;
      |     ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:2954:59: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 2954 |         fp_vkGetLatencyTimingsLegacyNV = reinterpret_cast<PFN_vkGetLatencyTimingsLegacyNV>(procAddr(device, "vkGetLatencyTimingsLegacyNV"));
      |                                                           ^
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3113:56: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 3113 |         fp_vkGetSleepStatusLegacyNV = reinterpret_cast<PFN_vkGetSleepStatusLegacyNV>(procAddr(device, "vkGetSleepStatusLegacyNV"));
      |                                                        ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3168:54: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 3168 |         fp_vkLatencySleepLegacyNV = reinterpret_cast<PFN_vkLatencySleepLegacyNV>(procAddr(device, "vkLatencySleepLegacyNV"));
      |                                                      ^
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3195:62: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 3195 |         fp_vkQueueNotifyOutOfBandLegacyNV = reinterpret_cast<PFN_vkQueueNotifyOutOfBandLegacyNV>(procAddr(device, "vkQueueNotifyOutOfBandLegacyNV"));
      |                                                              ^
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3281:58: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 3281 |         fp_vkSetLatencyMarkerLegacyNV = reinterpret_cast<PFN_vkSetLatencyMarkerLegacyNV>(procAddr(device, "vkSetLatencyMarkerLegacyNV"));
      |                                                          ^
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3287:61: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 3287 |         fp_vkSetLatencySleepModeLegacyNV = reinterpret_cast<PFN_vkSetLatencySleepModeLegacyNV>(procAddr(device, "vkSetLatencySleepModeLegacyNV"));
      |                                                             ^
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3305:63: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 3305 |         fp_vkShutdownLatencyDeviceLegacyNV = reinterpret_cast<PFN_vkShutdownLatencyDeviceLegacyNV>(procAddr(device, "vkShutdownLatencyDeviceLegacyNV"));
      |                                                               ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:5995:56: error: too few arguments to function call, expected 3, have 2
 5995 |         fp_vkGetLatencyTimingsLegacyNV(device, pTimings);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                 ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6357:43: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'VkSemaphore' (aka 'VkSemaphore_T *')
 6357 |         fp_vkLatencySleepLegacyNV(device, signalSemaphore, value);
      |                                           ^~~~~~~~~~~~~~~
/usr/local/include/vulkan/vulkan_core.h:105:1: note: 'VkSemaphore_T' is not defined, but forward declared here; conversion would be valid if it was derived from 'VkSwapchainKHR_T'
  105 | VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
      | ^
/usr/local/include/vulkan/vulkan_core.h:56:74: note: expanded from macro 'VK_DEFINE_NON_DISPATCHABLE_HANDLE'
   56 |         #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
      |                                                                          ^
<scratch space>:87:1: note: expanded from here
   87 | VkSemaphore_T
      | ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanSwapchain.cpp:10:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6406:50: error: cannot initialize a parameter of type 'const VkOutOfBandQueueTypeInfoNV *' with an lvalue of type 'uint32_t' (aka 'unsigned int')
 6406 |         fp_vkQueueNotifyOutOfBandLegacyNV(queue, queueType);
      |                                                  ^~~~~~~~~
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6560:47: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'uint64_t' (aka 'unsigned long long')
 6560 |         fp_vkSetLatencyMarkerLegacyNV(device, frameID, marker);
      |                                               ^~~~~~~
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6570:83: error: too many arguments to function call, expected 3, have 4
 6570 |         fp_vkSetLatencySleepModeLegacyNV(device, lowLatencyMode, lowLatencyBoost, minimumIntervalUs);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                                          ^~~~~~~~~~~~~~~~~
19 errors generated.
[50/59] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Renderer.cpp.o
[51/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanCommandList.cpp.o
[52/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o
FAILED: [code=1] modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o 
/usr/bin/clang++ -DGLM_ENABLE_EXPERIMENTAL -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DGLM_FORCE_EXPLICIT_CTOR -DGLM_FORCE_RADIANS -DSONNET_ASSERTS_ENABLED=1 -DSONNET_ENABLE_TRACY=1 -DSONNET_ENABLE_VALIDATION=0 -DSONNET_MODULE=\"rhi\" -DSONNET_RHI_VULKAN=1 -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DTRACY_ENABLE -DVK_NO_PROTOTYPES -DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 -DVULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0 -I~/repositories/sonnet/modules/rhi/include -I~/repositories/sonnet/modules/rhi/src -I~/repositories/sonnet/modules/platform/include -I~/repositories/sonnet/modules/core/include -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/tracy -isystem ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/share/unofficial-vulkan-memory-allocator-hpp/../../include -O2 -g -DNDEBUG -std=c++2b -arch arm64 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough -Wcast-align -Wunused -Wformat=2 -Wno-c2y-extensions -fmacro-prefix-map=~/repositories/sonnet/= -Werror -Winvalid-pch -Xclang -include-pch -Xclang ~/repositories/sonnet/build/macos-release/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch -Xclang -include -Xclang ~/repositories/sonnet/build/macos-release/modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx -MD -MT modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o -MF modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o.d -o modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o -c ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9139:5: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 9139 |     PFN_vkGetLatencyTimingsLegacyNV fp_vkGetLatencyTimingsLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9402:5: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 9402 |     PFN_vkGetSleepStatusLegacyNV fp_vkGetSleepStatusLegacyNV = nullptr;
      |     ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9493:5: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 9493 |     PFN_vkLatencySleepLegacyNV fp_vkLatencySleepLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9536:5: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 9536 |     PFN_vkQueueNotifyOutOfBandLegacyNV fp_vkQueueNotifyOutOfBandLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9674:5: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 9674 |     PFN_vkSetLatencyMarkerLegacyNV fp_vkSetLatencyMarkerLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9684:5: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 9684 |     PFN_vkSetLatencySleepModeLegacyNV fp_vkSetLatencySleepModeLegacyNV = nullptr;
      |     ^
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:9714:5: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 9714 |     PFN_vkShutdownLatencyDeviceLegacyNV fp_vkShutdownLatencyDeviceLegacyNV = nullptr;
      |     ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:2954:59: error: unknown type name 'PFN_vkGetLatencyTimingsLegacyNV'; did you mean 'PFN_vkGetLatencyTimingsNV'?
 2954 |         fp_vkGetLatencyTimingsLegacyNV = reinterpret_cast<PFN_vkGetLatencyTimingsLegacyNV>(procAddr(device, "vkGetLatencyTimingsLegacyNV"));
      |                                                           ^
/usr/local/include/vulkan/vulkan_core.h:22800:26: note: 'PFN_vkGetLatencyTimingsNV' declared here
 22800 | typedef void (VKAPI_PTR *PFN_vkGetLatencyTimingsNV)(VkDevice device, VkSwapchainKHR swapchain, VkGetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3113:56: error: unknown type name 'PFN_vkGetSleepStatusLegacyNV'
 3113 |         fp_vkGetSleepStatusLegacyNV = reinterpret_cast<PFN_vkGetSleepStatusLegacyNV>(procAddr(device, "vkGetSleepStatusLegacyNV"));
      |                                                        ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3168:54: error: unknown type name 'PFN_vkLatencySleepLegacyNV'; did you mean 'PFN_vkLatencySleepNV'?
 3168 |         fp_vkLatencySleepLegacyNV = reinterpret_cast<PFN_vkLatencySleepLegacyNV>(procAddr(device, "vkLatencySleepLegacyNV"));
      |                                                      ^
/usr/local/include/vulkan/vulkan_core.h:22798:30: note: 'PFN_vkLatencySleepNV' declared here
 22798 | typedef VkResult (VKAPI_PTR *PFN_vkLatencySleepNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepInfoNV* pSleepInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3195:62: error: unknown type name 'PFN_vkQueueNotifyOutOfBandLegacyNV'; did you mean 'PFN_vkQueueNotifyOutOfBandNV'?
 3195 |         fp_vkQueueNotifyOutOfBandLegacyNV = reinterpret_cast<PFN_vkQueueNotifyOutOfBandLegacyNV>(procAddr(device, "vkQueueNotifyOutOfBandLegacyNV"));
      |                                                              ^
/usr/local/include/vulkan/vulkan_core.h:22801:26: note: 'PFN_vkQueueNotifyOutOfBandNV' declared here
 22801 | typedef void (VKAPI_PTR *PFN_vkQueueNotifyOutOfBandNV)(VkQueue queue, const VkOutOfBandQueueTypeInfoNV* pQueueTypeInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3281:58: error: unknown type name 'PFN_vkSetLatencyMarkerLegacyNV'; did you mean 'PFN_vkSetLatencyMarkerNV'?
 3281 |         fp_vkSetLatencyMarkerLegacyNV = reinterpret_cast<PFN_vkSetLatencyMarkerLegacyNV>(procAddr(device, "vkSetLatencyMarkerLegacyNV"));
      |                                                          ^
/usr/local/include/vulkan/vulkan_core.h:22799:26: note: 'PFN_vkSetLatencyMarkerNV' declared here
 22799 | typedef void (VKAPI_PTR *PFN_vkSetLatencyMarkerNV)(VkDevice device, VkSwapchainKHR swapchain, const VkSetLatencyMarkerInfoNV* pLatencyMarkerInfo);
       |                          ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3287:61: error: unknown type name 'PFN_vkSetLatencySleepModeLegacyNV'; did you mean 'PFN_vkSetLatencySleepModeNV'?
 3287 |         fp_vkSetLatencySleepModeLegacyNV = reinterpret_cast<PFN_vkSetLatencySleepModeLegacyNV>(procAddr(device, "vkSetLatencySleepModeLegacyNV"));
      |                                                             ^
/usr/local/include/vulkan/vulkan_core.h:22797:30: note: 'PFN_vkSetLatencySleepModeNV' declared here
 22797 | typedef VkResult (VKAPI_PTR *PFN_vkSetLatencySleepModeNV)(VkDevice device, VkSwapchainKHR swapchain, const VkLatencySleepModeInfoNV* pSleepModeInfo);
       |                              ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:3305:63: error: unknown type name 'PFN_vkShutdownLatencyDeviceLegacyNV'
 3305 |         fp_vkShutdownLatencyDeviceLegacyNV = reinterpret_cast<PFN_vkShutdownLatencyDeviceLegacyNV>(procAddr(device, "vkShutdownLatencyDeviceLegacyNV"));
      |                                                               ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:5995:56: error: too few arguments to function call, expected 3, have 2
 5995 |         fp_vkGetLatencyTimingsLegacyNV(device, pTimings);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                 ^
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6357:43: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'VkSemaphore' (aka 'VkSemaphore_T *')
 6357 |         fp_vkLatencySleepLegacyNV(device, signalSemaphore, value);
      |                                           ^~~~~~~~~~~~~~~
/usr/local/include/vulkan/vulkan_core.h:105:1: note: 'VkSemaphore_T' is not defined, but forward declared here; conversion would be valid if it was derived from 'VkSwapchainKHR_T'
  105 | VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
      | ^
/usr/local/include/vulkan/vulkan_core.h:56:74: note: expanded from macro 'VK_DEFINE_NON_DISPATCHABLE_HANDLE'
   56 |         #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
      |                                                                          ^
<scratch space>:87:1: note: expanded from here
   87 | VkSemaphore_T
      | ^
In file included from ~/repositories/sonnet/modules/rhi/src/VulkanDevice.cpp:12:
In file included from ~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrap.h:44:
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6406:50: error: cannot initialize a parameter of type 'const VkOutOfBandQueueTypeInfoNV *' with an lvalue of type 'uint32_t' (aka 'unsigned int')
 6406 |         fp_vkQueueNotifyOutOfBandLegacyNV(queue, queueType);
      |                                                  ^~~~~~~~~
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6560:47: error: cannot initialize a parameter of type 'VkSwapchainKHR' (aka 'VkSwapchainKHR_T *') with an lvalue of type 'uint64_t' (aka 'unsigned long long')
 6560 |         fp_vkSetLatencyMarkerLegacyNV(device, frameID, marker);
      |                                               ^~~~~~~
~/repositories/sonnet/build/macos-release/vcpkg_installed/arm64-osx/include/VkBootstrapDispatch.h:6570:83: error: too many arguments to function call, expected 3, have 4
 6570 |         fp_vkSetLatencySleepModeLegacyNV(device, lowLatencyMode, lowLatencyBoost, minimumIntervalUs);
      |         ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                                          ^~~~~~~~~~~~~~~~~
19 errors generated.
[53/59] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/NullDevice.cpp.o
[54/59] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/AssetDatabase.cpp.o
[55/59] Building CXX object apps/cook/CMakeFiles/sonnet_cook_app.dir/main.cpp.o
ninja: build stopped: subcommand failed.
```

Exit status: `1`.

## Host configure: explicit Xcode SDK

```sh
unset CC CXX
cmake --preset macos-release -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
```

```text
-- Running vcpkg install
Detecting compiler hash for triplet arm64-osx...
Compiler found: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++
The following packages are already installed:
    catch2:arm64-osx@3.16.0
  * egl-registry:arm64-osx@2025-05-27
    fastgltf:arm64-osx@0.9.0
    flecs:arm64-osx@4.1.6
  * fmt:arm64-osx@12.2.0#1
    glm:arm64-osx@1.0.3
    imgui[core,vulkan-binding,sdl3-binding,docking-experimental]:arm64-osx@1.92.9#3
    joltphysics[core,rtti]:arm64-osx@5.6.0#1
    ktx:arm64-osx@4.4.2#3
    lua[core,cpp]:arm64-osx@5.5.1#1
    miniaudio:arm64-osx@0.11.25
    moltenvk:arm64-osx@1.4.2
    nlohmann-json:arm64-osx@3.12.0#3
  * opengl-registry:arm64-osx@2026-08-03
  * pthreads:arm64-osx@3.0.0#14
    sdl3[core,vulkan]:arm64-osx@3.4.16#2
    shader-slang:arm64-osx@2026.7.1#1
  * simdjson[core,utf8-validation,threads,exceptions,deprecated]:arm64-osx@4.6.8
    sol2:arm64-osx@3.5.0#1
    spdlog[core,tz-offset,fmt]:arm64-osx@1.17.0#1
    stb:arm64-osx@2024-07-29#1
    tracy[core,crash-handler]:arm64-osx@0.13.1#1
  * vcpkg-cmake:arm64-osx@2025-08-07
  * vcpkg-cmake-config:arm64-osx@2026-07-21
    vk-bootstrap:arm64-osx@1.4.357
  * vulkan:arm64-osx@2023-12-17
    vulkan-headers:arm64-osx@1.4.357.0
    vulkan-loader:arm64-osx@1.4.357.0
  * vulkan-memory-allocator:arm64-osx@3.4.0
    vulkan-memory-allocator-hpp:arm64-osx@3.4.0
  * zstd:arm64-osx@1.5.7
catch2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Catch2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

catch2 provides pkg-config modules:

  # A modern, C++-native test framework for C++14 and above (links in default main)
  catch2-with-main

  # A modern, C++-native, test framework for C++14 and above
  catch2

fastgltf provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(fastgltf CONFIG REQUIRED)
  target_link_libraries(main PRIVATE fastgltf::fastgltf)

The package flecs provides CMake targets:

    find_package(flecs CONFIG REQUIRED)
    target_link_libraries(main PRIVATE $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)

The package glm provides CMake targets:

    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm)

    # Or use the header-only version
    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm-header-only)

Vulkan-Headers provides official find_package support:

    find_package(VulkanHeaders CONFIG)
    target_link_libraries(main PRIVATE Vulkan::Headers)

sdl3 provides CMake targets:

  find_package(SDL3 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE SDL3::SDL3)

imgui provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(imgui CONFIG REQUIRED)
  target_link_libraries(main PRIVATE imgui::imgui)

joltphysics provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Jolt CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Jolt::Jolt)

ktx provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Ktx CONFIG REQUIRED)
  target_link_libraries(main PRIVATE KTX::ktx)

lua provides CMake integration for the C library:

  find_package(Lua REQUIRED)
  target_include_directories(main PRIVATE ${LUA_INCLUDE_DIR})
  target_link_libraries(main PRIVATE ${LUA_LIBRARIES})

lua[cpp] provides a C++ library with exception handling:

  find_package(unofficial-lua)
  target_link_libraries(main PRIVATE unofficial::lua::lua-cpp)

miniaudio is header-only and can be used from CMake via:

  find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
  target_include_directories(main PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

moltenvk provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-moltenvk CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::moltenvk::moltenvk)

The package nlohmann-json provides CMake targets:

    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(main PRIVATE nlohmann_json::nlohmann_json)

The package nlohmann-json can be configured to not provide implicit conversions via a custom triplet file:

    set(nlohmann-json_IMPLICIT_CONVERSIONS OFF)

For more information, see the docs here:
    
    https://json.nlohmann.me/api/macros/json_use_implicit_conversions/

shader-slang provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(slang CONFIG REQUIRED)
  # note: 1 additional targets are not displayed.
  target_link_libraries(main PRIVATE slang::gfx slang::slang slang::slang-llvm slang::slang-glslang)

sol2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(sol2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE sol2::sol2)

sol2 provides pkg-config modules:

  # C++ <-> Lua Wrapper Library
  sol2

The package spdlog provides CMake targets:

    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog)

    # Or use the header-only version
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog_header_only)

The package stb provides CMake targets:

    find_package(Stb REQUIRED)
    target_include_directories(main PRIVATE ${Stb_INCLUDE_DIR})
tracy provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Tracy CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Tracy::TracyClient)

vk-bootstrap provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(vk-bootstrap CONFIG REQUIRED)
  target_link_libraries(main PRIVATE vk-bootstrap::vk-bootstrap vk-bootstrap::vk-bootstrap-compiler-warnings)

The package vulkan-loader provides the vulkan loader.
Please be aware of https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderApplicationInterface.md#bundling-the-loader-with-an-application

vulkan-memory-allocator-hpp provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)

All requested installations completed successfully in: 164 us
-- Running vcpkg install - done
-- Sonnet 0.11.0: rhi=Vulkan editor=ON player=ON tests=ON samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Configuring done (5.0s)
-- Generating done (0.1s)
-- Build files have been written to: ~/repositories/sonnet/build/macos-release
```

Exit status: `0`.

## Host build: retry

```sh
unset CC CXX
cmake --build --preset macos-release --target sonnet_cook_app
```

```text
[1/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Input.cpp.o
[2/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Camera.cpp.o
[3/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Uuid.cpp.o
[4/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlEvents.cpp.o
[5/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/InputState.cpp.o
[6/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Assert.cpp.o
[7/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Mesh.cpp.o
[8/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Log.cpp.o
[9/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Version.cpp.o
[10/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlEntryPoint.cpp.o
[11/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/JobSystem.cpp.o
[12/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/Error.cpp.o
[13/48] Building CXX object modules/core/CMakeFiles/sonnet_core.dir/src/File.cpp.o
[14/48] Linking CXX static library modules/core/libsonnet_core.a
[15/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Animation.cpp.o
[16/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Content.cpp.o
[17/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/Platform.cpp.o
[18/48] Building CXX object modules/platform/CMakeFiles/sonnet_platform.dir/src/SdlWindow.cpp.o
[19/48] Linking CXX static library modules/platform/libsonnet_platform.a
[20/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Primitives.cpp.o
[21/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Texture.cpp.o
[22/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Picker.cpp.o
[23/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/RenderTarget.cpp.o
[24/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/RenderGraph.cpp.o
[25/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/cmake_pch.hxx.pch
[26/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/MeshCook.cpp.o
[27/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Asset.cpp.o
[28/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Json.cpp.o
[29/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Ktx2.cpp.o
[30/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Cook.cpp.o
[31/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanTypes.cpp.o
[32/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/Device.cpp.o
[33/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Project.cpp.o
[34/48] Building CXX object apps/cook/CMakeFiles/sonnet_cook_app.dir/main.cpp.o
[35/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VmaImpl.cpp.o
[36/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/ImageImporter.cpp.o
[37/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/Bundle.cpp.o
[38/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanCommandList.cpp.o
[39/48] Building CXX object modules/renderer/CMakeFiles/sonnet_renderer.dir/src/Renderer.cpp.o
[40/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/GltfImporter.cpp.o
[41/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/NullDevice.cpp.o
[42/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o
[43/48] Building CXX object modules/assets/CMakeFiles/sonnet_assets.dir/src/AssetDatabase.cpp.o
[44/48] Building CXX object modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o
[45/48] Linking CXX static library modules/rhi/libsonnet_rhi.a
[46/48] Linking CXX static library modules/renderer/libsonnet_renderer.a
[47/48] Linking CXX static library modules/assets/libsonnet_assets.a
[48/48] Linking CXX executable apps/cook/sonnet_cook
```

Exit status: `0`.

## Cook

```sh
unset CC CXX
./build/macos-release/apps/cook/sonnet_cook apps/samples/basic --platform ios --out build/ios-bundle
```

```text
[20:50:02.067] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "offscreen" (headless)
[20:50:02.068] [info] [assets] [Project.cpp:39] opened project "Basic" at ~/repositories/sonnet/apps/samples/basic
[20:50:02.069] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:50:02.069] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:50:02.069] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:50:02.069] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:50:02.069] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:50:02.070] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:50:02.070] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:50:02.070] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:50:02.070] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[20:50:02.070] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:50:02.070] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:50:02.071] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:50:02.071] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:50:02.071] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:50:02.071] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:50:02.071] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:50:02.071] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:50:02.071] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from ~/repositories/sonnet/build/macos-release/apps/cook/shaders
[20:50:02.071] [debug] [core] [JobSystem.cpp:78] job system started with 0 workers
[20:50:02.074] [info] [assets] [AssetDatabase.cpp:210] asset database: 27 assets in ~/repositories/sonnet/apps/samples/basic
[20:50:02.075] [debug] [assets] [GltfImporter.cpp:436] ~/repositories/sonnet/apps/samples/basic/assets/models/crate.glb: 2 meshes, 2 materials, 2 images, 2 nodes, 0 skins, 0 animations
[20:50:02.087] [debug] [renderer] [Renderer.cpp:577] texture "crate/image 0": 128x128 BC7Srgb, 8 levels
[20:50:02.087] [debug] [renderer] [Renderer.cpp:577] texture "crate/image 1": 128x128 BC7Unorm, 8 levels
[20:50:02.087] [debug] [renderer] [Renderer.cpp:522] mesh "crate/CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:50:02.087] [debug] [renderer] [Renderer.cpp:522] mesh "crate/BallMesh": 561 vertices, 960 triangles, 1 submeshes
[20:50:02.087] [debug] [assets] [GltfImporter.cpp:436] ~/repositories/sonnet/apps/samples/basic/assets/models/beacon.glb: 2 meshes, 2 materials, 0 images, 3 nodes, 0 skins, 1 animations
[20:50:02.087] [debug] [renderer] [Renderer.cpp:522] mesh "beacon/LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:50:02.087] [debug] [renderer] [Renderer.cpp:522] mesh "beacon/HaloMesh": 325 vertices, 528 triangles, 1 submeshes
[20:50:02.088] [debug] [assets] [GltfImporter.cpp:436] ~/repositories/sonnet/apps/samples/basic/assets/models/reed.glb: 1 meshes, 1 materials, 0 images, 6 nodes, 1 skins, 1 animations
[20:50:02.088] [debug] [renderer] [Renderer.cpp:522] mesh "reed/StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:50:02.089] [debug] [renderer] [Renderer.cpp:577] texture "pending": 1x1 R8G8B8A8Srgb, 1 levels
[20:50:02.094] [info] [assets] [Bundle.cpp:334] wrote build/ios-bundle/game.sbundle (27 assets, 4 files, 703249 bytes)
[20:50:02.094] [info] [assets] [Cook.cpp:195] cooked "Basic" for ios: 27 assets, 4 scenes and prefabs, 0 warnings
build/ios-bundle/game.sbundle: 27 assets, 4 scenes and prefabs, 699662 bytes
meshes: 1025 vertices welded to 1021
[20:50:02.094] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 BC7Srgb, 7 levels
```

Exit status: `0`.

## iOS configure

```sh
unset CC CXX
cmake --preset ios-release -DSONNET_IOS_BUNDLE="$PWD/build/ios-bundle/game.sbundle" -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<team>
```

```text
-- Running vcpkg install
Detecting compiler hash for triplet arm64-osx...
Compiler found: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++
Detecting compiler hash for triplet arm64-ios...
Compiler found: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++
The following packages are already installed:
    catch2:arm64-ios@3.16.0
  * egl-registry:arm64-ios@2025-05-27
    fastgltf:arm64-ios@0.9.0
    flecs:arm64-ios@4.1.6
  * fmt:arm64-ios@12.2.0#1
    glm:arm64-ios@1.0.3
    joltphysics[core,rtti]:arm64-ios@5.6.0#1
    ktx:arm64-ios@4.4.2#3
    lua[core,cpp]:arm64-ios@5.5.1#1
    miniaudio:arm64-ios@0.11.25
    moltenvk:arm64-ios@1.4.2
    nlohmann-json:arm64-ios@3.12.0#3
  * opengl-registry:arm64-ios@2026-08-03
  * pthreads:arm64-ios@3.0.0#14
    sdl3[core,vulkan]:arm64-ios@3.4.16#2
    shader-slang:arm64-osx@2026.7.1#1
  * simdjson[core,utf8-validation,threads,exceptions,deprecated]:arm64-ios@4.6.8
    sol2:arm64-ios@3.5.0#1
    spdlog[core,tz-offset,fmt]:arm64-ios@1.17.0#1
    stb:arm64-ios@2024-07-29#1
    tracy[core,crash-handler]:arm64-ios@0.13.1#1
  * vcpkg-cmake:arm64-osx@2025-08-07
  * vcpkg-cmake-config:arm64-osx@2026-07-21
    vk-bootstrap:arm64-ios@1.4.357
  * vulkan:arm64-ios@2023-12-17
    vulkan-headers:arm64-ios@1.4.357.0
  * vulkan-loader:arm64-ios@1.4.357.0
  * vulkan-memory-allocator:arm64-ios@3.4.0
    vulkan-memory-allocator-hpp:arm64-ios@3.4.0
  * zstd:arm64-ios@1.5.7
catch2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Catch2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

catch2 provides pkg-config modules:

  # A modern, C++-native test framework for C++14 and above (links in default main)
  catch2-with-main

  # A modern, C++-native, test framework for C++14 and above
  catch2

fastgltf provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(fastgltf CONFIG REQUIRED)
  target_link_libraries(main PRIVATE fastgltf::fastgltf)

The package flecs provides CMake targets:

    find_package(flecs CONFIG REQUIRED)
    target_link_libraries(main PRIVATE $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)

The package glm provides CMake targets:

    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm)

    # Or use the header-only version
    find_package(glm CONFIG REQUIRED)
    target_link_libraries(main PRIVATE glm::glm-header-only)

joltphysics provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Jolt CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Jolt::Jolt)

ktx provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Ktx CONFIG REQUIRED)
  target_link_libraries(main PRIVATE KTX::ktx)

lua provides CMake integration for the C library:

  find_package(Lua REQUIRED)
  target_include_directories(main PRIVATE ${LUA_INCLUDE_DIR})
  target_link_libraries(main PRIVATE ${LUA_LIBRARIES})

lua[cpp] provides a C++ library with exception handling:

  find_package(unofficial-lua)
  target_link_libraries(main PRIVATE unofficial::lua::lua-cpp)

miniaudio is header-only and can be used from CMake via:

  find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")
  target_include_directories(main PRIVATE ${MINIAUDIO_INCLUDE_DIRS})

moltenvk provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-moltenvk CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::moltenvk::moltenvk)

The package nlohmann-json provides CMake targets:

    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(main PRIVATE nlohmann_json::nlohmann_json)

The package nlohmann-json can be configured to not provide implicit conversions via a custom triplet file:

    set(nlohmann-json_IMPLICIT_CONVERSIONS OFF)

For more information, see the docs here:
    
    https://json.nlohmann.me/api/macros/json_use_implicit_conversions/

sdl3 provides CMake targets:

  find_package(SDL3 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE SDL3::SDL3)

shader-slang provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(slang CONFIG REQUIRED)
  # note: 1 additional targets are not displayed.
  target_link_libraries(main PRIVATE slang::gfx slang::slang slang::slang-llvm slang::slang-glslang)

sol2 provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(sol2 CONFIG REQUIRED)
  target_link_libraries(main PRIVATE sol2::sol2)

sol2 provides pkg-config modules:

  # C++ <-> Lua Wrapper Library
  sol2

The package spdlog provides CMake targets:

    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog)

    # Or use the header-only version
    find_package(spdlog CONFIG REQUIRED)
    target_link_libraries(main PRIVATE spdlog::spdlog_header_only)

The package stb provides CMake targets:

    find_package(Stb REQUIRED)
    target_include_directories(main PRIVATE ${Stb_INCLUDE_DIR})
tracy provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(Tracy CONFIG REQUIRED)
  target_link_libraries(main PRIVATE Tracy::TracyClient)

Vulkan-Headers provides official find_package support:

    find_package(VulkanHeaders CONFIG)
    target_link_libraries(main PRIVATE Vulkan::Headers)

vk-bootstrap provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(vk-bootstrap CONFIG REQUIRED)
  target_link_libraries(main PRIVATE vk-bootstrap::vk-bootstrap vk-bootstrap::vk-bootstrap-compiler-warnings)

vulkan-memory-allocator-hpp provides CMake targets:

  # this is heuristically generated, and may not be correct
  find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
  target_link_libraries(main PRIVATE unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp)

All requested installations completed successfully in: 163 us
-- Running vcpkg install - done
-- Sonnet 0.11.0: rhi=Vulkan editor=OFF player=ON tests=OFF samples=ON tracy=ON validation=ON sanitizers=OFF tsan=OFF coverage=OFF
-- Configuring done (1.5s)
-- Generating done (0.1s)
-- Build files have been written to: ~/repositories/sonnet/build/ios-release
```

Exit status: `0`.

## iOS build and signing

```sh
unset CC CXX
cmake --build --preset ios-release -- -allowProvisioningUpdates -allowProvisioningDeviceRegistration
```

```text
Command line invocation:
    /Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild -project sonnet.xcodeproj build -configuration RelWithDebInfo -parallelizeTargets -hideShellScriptEnvironment -allowProvisioningUpdates -allowProvisioningDeviceRegistration -target ALL_BUILD

ComputePackagePrebuildTargetDependencyGraph

CreateBuildRequest

SendProjectDescription

CreateBuildOperation

ComputeTargetDependencyGraph
note: Building targets in dependency order
note: Target dependency graph (15 targets)
    Target 'ALL_BUILD' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_physics' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_scripting' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_audio' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_runtime' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_player_app' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_samples' in project 'sonnet'
    Target 'sonnet_samples' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
    Target 'sonnet_player_app' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_physics' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_scripting' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_audio' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_runtime' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_player_app_shaders' in project 'sonnet'
    Target 'sonnet_player_app_shaders' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
    Target 'sonnet_runtime' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_physics' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_scripting' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_audio' in project 'sonnet'
    Target 'sonnet_audio' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
    Target 'sonnet_scripting' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_physics' in project 'sonnet'
    Target 'sonnet_physics' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_world' in project 'sonnet'
    Target 'sonnet_world' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_assets' in project 'sonnet'
    Target 'sonnet_assets' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_renderer' in project 'sonnet'
    Target 'sonnet_renderer' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_rhi' in project 'sonnet'
    Target 'sonnet_rhi' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_platform' in project 'sonnet'
    Target 'sonnet_platform' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
        ➜ Explicit dependency on target 'sonnet_core' in project 'sonnet'
    Target 'sonnet_core' in project 'sonnet'
        ➜ Explicit dependency on target 'ZERO_CHECK' in project 'sonnet'
    Target 'ZERO_CHECK' in project 'sonnet' (no dependencies)

GatherProvisioningInputs

CreateBuildDescription

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang -v -E -dM -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk -x c -c /dev/null

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang -v -E -dM -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk -x c++ -c /dev/null

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/usr/bin/ibtool --version --output-format xml1

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang -v -E -dM -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk -x c -c /dev/null

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang -v -E -dM -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk -x objective-c++ -c /dev/null

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld -version_details

ExecuteExternalTool /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/libtool -V

Build description signature: 0f677d583119b68f54cd011db39c60ad
Build description path: ~/repositories/sonnet/build/ios-release/build/XCBuildData/0f677d583119b68f54cd011db39c60ad.xcbuilddata
note: Run script build phase 'Generate CMakeFiles/ZERO_CHECK' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ZERO_CHECK' from project 'sonnet')
PhaseScriptExecution Generate\ CMakeFiles/ZERO_CHECK ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ZERO_CHECK.build/Script-75823BFB16E7F2FFF9F564E9.sh (in target 'ZERO_CHECK' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ZERO_CHECK.build/Script-75823BFB16E7F2FFF9F564E9.sh
make: `~/repositories/sonnet/build/ios-release/CMakeFiles/cmake.check_cache' is up to date.

ClangStatCache /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang-stat-cache /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk /var/folders/3x/k8mdqx5n45l8wdtrwch2lgp40000gn/C/com.apple.DeveloperTools/26.3-17C529/Xcode/SDKStatCaches.noindex/iphoneos26.2-23C57-3794476bd08197c3e2abd9bb477ef7f7.sdkstatcache
    cd ~/repositories/sonnet/build/ios-release/sonnet.xcodeproj
    /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang-stat-cache /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk -o /var/folders/3x/k8mdqx5n45l8wdtrwch2lgp40000gn/C/com.apple.DeveloperTools/26.3-17C529/Xcode/SDKStatCaches.noindex/iphoneos26.2-23C57-3794476bd08197c3e2abd9bb477ef7f7.sdkstatcache

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B525CFB24FDB0F68820885A5.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B525CFB24FDB0F68820885A5.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-767C5435F89E1844616FAA19.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-767C5435F89E1844616FAA19.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-F1B8640BAECD07D8765868CD.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-F1B8640BAECD07D8765868CD.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-DE595B9D029608D8F9C9708D.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-DE595B9D029608D8F9C9708D.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-698EA52E5A1ED4D7B2607A49.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-698EA52E5A1ED4D7B2607A49.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63C557A310E5235C3DD544E1.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63C557A310E5235C3DD544E1.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63B4B0A6E3EFFE893965787E.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63B4B0A6E3EFFE893965787E.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-54EA6E32695DE2C8C860FF17.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-54EA6E32695DE2C8C860FF17.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-37174C8F4E5685C2712E60B6.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-37174C8F4E5685C2712E60B6.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-06914C1F8E4EFB0D5C9A7D05.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-06914C1F8E4EFB0D5C9A7D05.sh

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-405CDAC2B291A70E632AA2C4.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-405CDAC2B291A70E632AA2C4.sh

PhaseScriptExecution Generate\ apps/player/shaders/cluster.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63C557A310E5235C3DD544E1.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63C557A310E5235C3DD544E1.sh

PhaseScriptExecution Generate\ apps/player/shaders/cull.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-54EA6E32695DE2C8C860FF17.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-54EA6E32695DE2C8C860FF17.sh

PhaseScriptExecution Generate\ apps/player/shaders/debug.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-767C5435F89E1844616FAA19.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-767C5435F89E1844616FAA19.sh

PhaseScriptExecution Generate\ apps/player/shaders/depth.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-698EA52E5A1ED4D7B2607A49.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-698EA52E5A1ED4D7B2607A49.sh

PhaseScriptExecution Generate\ apps/player/shaders/forward.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63B4B0A6E3EFFE893965787E.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-63B4B0A6E3EFFE893965787E.sh

PhaseScriptExecution Generate\ apps/player/shaders/ibl.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B525CFB24FDB0F68820885A5.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B525CFB24FDB0F68820885A5.sh

PhaseScriptExecution Generate\ apps/player/shaders/id.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-37174C8F4E5685C2712E60B6.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-37174C8F4E5685C2712E60B6.sh

PhaseScriptExecution Generate\ apps/player/shaders/outline.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-DE595B9D029608D8F9C9708D.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-DE595B9D029608D8F9C9708D.sh

PhaseScriptExecution Generate\ apps/player/shaders/post.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-F1B8640BAECD07D8765868CD.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-F1B8640BAECD07D8765868CD.sh

PhaseScriptExecution Generate\ apps/player/shaders/skin.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-06914C1F8E4EFB0D5C9A7D05.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-06914C1F8E4EFB0D5C9A7D05.sh

PhaseScriptExecution Generate\ apps/player/shaders/skybox.spv ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-405CDAC2B291A70E632AA2C4.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-405CDAC2B291A70E632AA2C4.sh

PhaseScriptExecution Generate\ apps/player/CMakeFiles/sonnet_player_app_shaders ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B833CD59AA348D2FB119152B.sh (in target 'sonnet_player_app_shaders' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_player_app_shaders.build/Script-B833CD59AA348D2FB119152B.sh

note: Run script build phase 'CMake PostBuild Rules' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app' from project 'sonnet')
ProcessProductPackaging "" ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    
    Entitlements:
    
    {
    "application-identifier" = "<team>.io.github.pacheco95.sonnet";
    "com.apple.developer.team-identifier" = <team>;
    "get-task-allow" = 1;
}
    
    builtin-productPackagingUtility -entitlements -format xml -o ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent

ProcessProductPackagingDER ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent.der (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    /usr/bin/derq query -f xml -i ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent -o ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent.der --raw

CompileStoryboard ~/repositories/sonnet/apps/player/ios/LaunchScreen.storyboard (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    /Applications/Xcode.app/Contents/Developer/usr/bin/ibtool --errors --warnings --notices --module sonnet_player --output-partial-info-plist ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/LaunchScreen-SBPartialInfo.plist --auto-activate-custom-fonts --target-device iphone --minimum-deployment-target 16.3 --output-format human-readable-text ~/repositories/sonnet/apps/player/ios/LaunchScreen.storyboard --compilation-directory ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos

ProcessInfoPlistFile ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app/Info.plist ~/repositories/sonnet/build/ios-release/apps/player/CMakeFiles/sonnet_player_app.dir/Info.plist (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    builtin-infoPlistUtility ~/repositories/sonnet/build/ios-release/apps/player/CMakeFiles/sonnet_player_app.dir/Info.plist -producttype com.apple.product-type.application -genpkginfo ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app/PkgInfo -expandbuildsettings -format binary -platform iphoneos -additionalcontentfile ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/LaunchScreen-SBPartialInfo.plist -requiredArchitecture arm64 -o ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app/Info.plist

LinkStoryboards (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    /Applications/Xcode.app/Contents/Developer/usr/bin/ibtool --errors --warnings --notices --module sonnet_player --target-device iphone --minimum-deployment-target 16.3 --output-format human-readable-text --link ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/LaunchScreen.storyboardc

WriteAuxiliaryFile ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/Script-E497154619C417021372C8E1.sh (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    write-file ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/Script-E497154619C417021372C8E1.sh

PhaseScriptExecution CMake\ PostBuild\ Rules ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/Script-E497154619C417021372C8E1.sh (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/Script-E497154619C417021372C8E1.sh

CodeSign ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    
    Signing Identity:     "<team>"
    Provisioning Profile: "iOS Team Provisioning Profile: io.github.pacheco95.sonnet"
                          (58693f2d-0a0f-4a5e-b559-45f4a984e1b1)
    
    /usr/bin/codesign --force --sign 6715A6596C55DD8623777E0881D231AF8223DA1D --entitlements ~/repositories/sonnet/build/ios-release/build/sonnet_player_app.build/RelWithDebInfo-iphoneos/sonnet_player.app.xcent --timestamp\=none --generate-entitlement-der ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app
~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app: replacing existing signature

Validate ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app (in target 'sonnet_player_app' from project 'sonnet')
    cd ~/repositories/sonnet
    builtin-validationUtility ~/repositories/sonnet/build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app -shallow-bundle -infoplist-subpath Info.plist

PhaseScriptExecution Generate\ apps/samples/CMakeFiles/sonnet_samples ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_samples.build/Script-2673E7D87A1C7A6B6DD5CCC7.sh (in target 'sonnet_samples' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/sonnet_samples.build/Script-2673E7D87A1C7A6B6DD5CCC7.sh

note: Run script build phase 'Generate CMakeFiles/ALL_BUILD' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'ALL_BUILD' from project 'sonnet')
PhaseScriptExecution Generate\ CMakeFiles/ALL_BUILD ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ALL_BUILD.build/Script-65814F6636F930831F18EBC5.sh (in target 'ALL_BUILD' from project 'sonnet')
    cd ~/repositories/sonnet
    /bin/sh -c ~/repositories/sonnet/build/ios-release/build/sonnet.build/RelWithDebInfo-iphoneos/ALL_BUILD.build/Script-65814F6636F930831F18EBC5.sh
Build all projects

note: Run script build phase 'Generate apps/player/CMakeFiles/sonnet_player_app_shaders' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_player_app_shaders' from project 'sonnet')
note: Run script build phase 'Generate apps/samples/CMakeFiles/sonnet_samples' will be run during every build because the option to run the script phase "Based on dependency analysis" is unchecked. (in target 'sonnet_samples' from project 'sonnet')
** BUILD SUCCEEDED **

```

Exit status: `0`.

## Installation

```sh
unset CC CXX
xcrun devicectl device install app --device <device> build/ios-release/apps/player/RelWithDebInfo-iphoneos/sonnet_player.app
```

```text
20:50:39  Acquired tunnel connection to device.
20:50:39  Enabling developer disk image services.
20:50:39  Acquired usage assertion.
App installed:
• bundleID: io.github.pacheco95.sonnet
• installationURL: file:///private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/
• launchServicesIdentifier: unknown
• databaseUUID: 0B069218-333F-4028-99FB-C51DE116A3A2
• databaseSequenceNumber: 2204
• options: 
```

Exit status: `0`.

## First launch attempt

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --screenshot check1.png --settle-frames 5
```

```text
20:50:52  Acquired tunnel connection to device.
20:50:52  Enabling developer disk image services.
20:50:52  Acquired usage assertion.
ERROR: The application failed to launch. (com.apple.dt.CoreDeviceError error 10002 (0x2712))
       BundleIdentifier = io.github.pacheco95.sonnet
       ----------------------------------------
           The request to open "io.github.pacheco95.sonnet" failed. (FBSOpenApplicationServiceErrorDomain error 1 (0x01))
           FBSOpenApplicationRequestID = 0x5ecc
           BSErrorCodeDescription = RequestDenied
           NSLocalizedFailureReason = The request was denied by service delegate (SBMainWorkspace) for reason: Security ("Unable to launch io.github.pacheco95.sonnet because it has an invalid code signature, inadequate entitlements or its profile has not been explicitly trusted by the user").
       ----------------------------------------
               The operation couldn’t be completed. Unable to launch io.github.pacheco95.sonnet because it has an invalid code signature, inadequate entitlements or its profile has not been explicitly trusted by the user. (FBSOpenApplicationErrorDomain error 3 (0x03))
               BSErrorCodeDescription = Security
               NSLocalizedFailureReason = Unable to launch io.github.pacheco95.sonnet because it has an invalid code signature, inadequate entitlements or its profile has not been explicitly trusted by the user.
```

Exit status: `1`.

## Check 1: Vulkan description

**PASS — player device-selection recheck.** The selected GPU, API version, driver version, and ASTC support match the prior iPhone evidence in [the roadmap](../roadmap.md#checked-before-the-code). The player selects its device with the required baseline features before logging this line. This run does not print numerical descriptor limits; those remain the prior probe evidence, not a fresh limit dump.

```text
[20:53:04.683] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
```

Compared with [rendering.md](../rendering.md#vulkan-baseline): Vulkan 1.4.357 takes the core 1.4 path; successful selection rechecks the required shader draw parameters, indirect draws, anisotropy, Vulkan 1.2 descriptor/buffer/timeline/scalar features, Vulkan 1.3 rendering/synchronization/demote features, and four Vulkan 1.4 features. The log lists `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_push_descriptor`, `VK_KHR_maintenance5`, and `VK_KHR_maintenance6`. `hostImageCopy` is optional. No new claim about numerical limits is inferred from the selection line.

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --screenshot check1.png --settle-frames 5
```

```text
20:53:03  Acquired tunnel connection to device.
20:53:03  Enabling developer disk image services.
20:53:03  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:53:03.914] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:53:04.005 sonnet_player[11809:2497998] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:53:04.005] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:53:04.019] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[20:53:04.022] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilities2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:53:04.031] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[20:53:04.683] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x134765600) on screen Main Screen.
[20:53:04.685] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:53:04.685] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:53:04.686] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:53:04.686] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:53:04.746] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:53:04.746] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:53:04.746] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:53:04.783] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:53:04.783] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:53:04.810] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:53:04.810] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:53:04.810] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:53:04.867] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:53:04.868] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:53:04.868] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:53:05.052] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:53:05.052] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:53:05.148] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:53:05.148] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:53:05.148] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:53:05.148] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:53:05.148] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:53:05.148] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:53:05.148] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:53:05.472] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:53:05.472] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:53:05.511] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:53:05.511] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:53:05.511] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:53:05.511] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:53:05.511] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:53:05.511] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:53:05.511] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:53:05.635] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:53:05.635] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:53:05.798] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:53:05.799] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:53:05.961] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:53:05.961] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:53:06.130] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:53:06.130] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:53:06.244] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[20:53:06.244] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:53:06.244] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[20:53:06.361] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.370] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:53:06.370] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.371] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.371] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:53:06.371] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.371] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.371] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:53:06.371] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:53:06.371] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:06.371] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:53:06.371] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:53:06.371] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[20:53:06.441] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:53:06.442] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:53:06.442] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:53:06.522] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:53:06.522] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:53:06.595] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:53:06.595] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:53:06.662] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:53:06.662] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:53:06.739] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:53:06.739] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:53:06.805] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:53:06.805] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:53:06.805] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:53:06.841] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:53:06.842] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:53:06.842] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:53:06.993] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:53:06.993] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:53:06.993] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:53:06.993] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:53:06.997] [debug] [world] [World.cpp:103] world ready with 13 components
[20:53:06.999] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:53:07.000] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:53:07.000] [debug] [world] [Animation.cpp:59] animation ready
[20:53:07.141] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:53:07.142] [info] [player] [main.cpp:94] capture run on "Apple A17 Pro GPU", Vulkan 1.4.357, writing /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/check1.png
[20:53:07.142] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:53:07.142] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:53:07.144] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:53:07.152] [info] [player] [main.cpp:207] back from the background after 422516.8 s, 0 frames in it
[20:53:07.152] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
2026-09-28 20:53:07.160 sonnet_player[11809:2497998] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x134674000>.
2026-09-28 20:53:07.160 sonnet_player[11809:2497998] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x134674000>.
[20:53:07.156] [info] [player] [main.cpp:207] back from the background after 422516.8 s, 0 frames in it
[20:53:07.156] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:53:07.163] [info] [player] [main.cpp:194] entering the background
[20:53:07.203] [info] [rhi] [VulkanSwapchain.cpp:164] swapchain suspended
[20:53:07.240] [info] [audio] [MiniaudioDevice.cpp:132] audio paused
[20:53:07.247] [info] [player] [main.cpp:194] entering the background
[20:53:07.248] [debug] [rhi] [VulkanSwapchain.cpp:155] swapchain already suspended
[20:53:07.248] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:53:07.248] [debug] [renderer] [Renderer.cpp:522] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[20:53:07.249] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:53:07.249] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:53:07.249] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:53:07.249] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:53:07.249] [debug] [renderer] [Renderer.cpp:522] mesh "CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:53:07.249] [debug] [renderer] [Renderer.cpp:577] texture "image 0": 128x128 ASTC6x6Srgb, 8 levels
[20:53:07.250] [debug] [renderer] [Renderer.cpp:577] texture "image 1": 128x128 ASTC4x4Unorm, 8 levels
[20:53:07.250] [debug] [renderer] [Renderer.cpp:522] mesh "BallMesh": 559 vertices, 960 triangles, 1 submeshes
[20:53:07.250] [debug] [renderer] [Renderer.cpp:522] mesh "LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:53:07.250] [debug] [renderer] [Renderer.cpp:522] mesh "HaloMesh": 323 vertices, 528 triangles, 1 submeshes
[20:53:07.250] [debug] [renderer] [Renderer.cpp:522] mesh "StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:53:07.251] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:53:13.977] [info] [player] [main.cpp:207] back from the background after 6.7 s, 405 frames in it
[20:53:13.982] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:53:13.982] [info] [rhi] [VulkanSwapchain.cpp:192] swapchain resumed at 1290x2796
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x1352f5140) on screen Main Screen.
[20:53:14.068] [info] [audio] [MiniaudioDevice.cpp:145] audio resumed
[20:53:14.074] [info] [player] [main.cpp:207] back from the background after 6.8 s, 405 frames in it
[20:53:14.074] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:53:14.076] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:53:14.077] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:53:14.078] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:53:14.099 sonnet_player[11809:2497998] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x134674000>.
[20:53:14.370] [info] [runtime] [Screenshot.cpp:75] screenshot 1290x2796 written to /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/check1.png
[20:53:14.372] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.20 ms a frame, 16.80 ms apart; GPU 0.979 ms: skinning 0.002 ms, cull 0.020 ms, shadow cascade 0 0.010 ms, shadow cascade 1 0.028 ms, shadow cascade 2 0.021 ms, shadow cascade 3 0.020 ms, depth 0.029 ms, light clustering 0.007 ms, forward 0.767 ms, bloom down 0 0.020 ms, bloom down 1 0.002 ms, bloom down 2 0.003 ms, bloom down 3 0.010 ms, bloom down 4 0.001 ms, bloom up 3 0.000 ms, bloom up 2 0.001 ms, bloom up 1 0.004 ms, bloom up 0 0.003 ms, tonemap 0.005 ms, fxaa 0.008 ms, present 0.017 ms
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 44 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[20:53:14.436] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`.

## Check 2: installs and draws

**PASS — installation, capture inspection, and launcher check by Michael Pacheco.** The agent inspected [check1.png](m10-device-checks/check1.png): checkerboard ground, coloured cube, sphere, capsule, cylinder, animated reed and beacon, sky and shadows are visible. It is not a blank frame. The capture is 1290 × 2796.

After the persistent playground was stopped, Michael Pacheco tapped Sonnet’s icon for a fresh launch. Verbatim observation:

```text
Yes, Sonnet opens and shows the rotating cube
```

The icon check was repeated because the first response (“It loaded the home screen. Name confirmed”) was ambiguous and the user could not recall which screen it meant. The fresh observation above is the evidence used for this check.

```sh
unset CC CXX
xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/check1.png" --destination docs/agent-tasks/m10-device-checks/check1.png
```

```text
20:53:35  Acquired tunnel connection to device.
20:53:35  Enabling developer disk image services.
20:53:35  Acquired usage assertion.
File received from Device
~/repositories/sonnet/docs/agent-tasks/m10-device-checks/check1.png
```

Exit status: `0`.

## Check 3: four capture runs

**FAIL — all four runs exited 0 and produced PNGs, but each logged warnings.** Each has four `[warn] [rhi]` lines and four `[mvk-warn]` lines reporting unsupported blending for `VK_FORMAT_R32_UINT`. Every run also prints the indirect-input Info.plist diagnostic and unbalanced UIKit appearance-transition diagnostics. These are retained below, not waived.

The agent inspected all four PNGs. Final shows the start scene with shading and shadows; albedo shows distinct material colours; normal shows direction-dependent colours; playground shows the yellow ball, stacked and airborne boxes, pink sweeper and checkerboard ground. No Linux reference captures were present in this checkout, so no direct Linux image comparison was performed.

### final

[PNG](m10-device-checks/final.png)

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --play 3 --screenshot final.png
```

```text
20:53:35  Acquired tunnel connection to device.
20:53:35  Enabling developer disk image services.
20:53:35  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:53:36.591] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:53:36.600 sonnet_player[11815:2498725] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:53:36.600] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:53:36.610] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilit[20:53:36.610] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
ies2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:53:36.615] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[20:53:36.618] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[20:53:36.621] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x10735d580) on screen Main Screen.
[20:53:36.621] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:53:36.621] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:53:36.621] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:53:36.624] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:53:36.624] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:53:36.624] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:53:36.624] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:53:36.624] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:53:36.625] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:53:36.625] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:53:36.625] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:53:36.625] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:53:36.625] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:53:36.625] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:53:36.626] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:53:36.626] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:53:36.627] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:53:36.627] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:53:36.627] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:53:36.627] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:53:36.627] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:53:36.627] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:53:36.627] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:53:36.630] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:53:36.630] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:53:36.630] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:53:36.630] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:53:36.630] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:53:36.630] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:53:36.631] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:53:36.631] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:53:36.631] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:53:36.631] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:53:36.631] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:53:36.631] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:53:36.631] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:53:36.632] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:53:36.632] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:53:36.632] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:53:36.632] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:53:36.632] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[20:53:36.633] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:53:36.633] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:36.633] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:36.633] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:53:36.633] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[20:53:36.634] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:36.634] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:53:36.634] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:53:36.634] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:36.634] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:53:36.634] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:53:36.634] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:53:36.634] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:53:36.634] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:53:36.634] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[20:53:36.634] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:53:36.634] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:53:36.634] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:53:36.635] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:53:36.635] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:53:36.635] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:53:36.635] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:53:36.636] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:53:36.636] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:53:36.636] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:53:36.636] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:53:36.636] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:53:36.636] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:53:36.636] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:53:36.637] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:53:36.637] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:53:36.637] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:53:36.637] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:53:36.637] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:53:36.637] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:53:36.637] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:53:36.640] [debug] [world] [World.cpp:103] world ready with 13 components
[20:53:36.640] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:53:36.641] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:53:36.641] [debug] [world] [Animation.cpp:59] animation ready
[20:53:36.750] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:53:36.751] [info] [player] [main.cpp:94] capture run on "Apple A17 Pro GPU", Vulkan 1.4.357, writing /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/final.png
[20:53:36.751] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:53:36.751] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:53:36.752] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:53:36.762] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:53:36.762] [debug] [renderer] [Renderer.cpp:522] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[20:53:36.762] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:53:36.762] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:577] texture "image 0": 128x128 ASTC6x6Srgb, 8 levels
[20:53:36.763] [debug] [renderer] [Renderer.cpp:577] texture "image 1": 128x128 ASTC4x4Unorm, 8 levels
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "BallMesh": 559 vertices, 960 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "HaloMesh": 323 vertices, 528 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:522] mesh "StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:53:36.763] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:53:36.763] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:53:36.763] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:53:36.768 sonnet_player[11815:2498725] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x107268000>.
2026-09-28 20:53:36.768 sonnet_player[11815:2498725] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x107268000>.
[20:53:36.961] [debug] [audio] [MiniaudioDevice.cpp:258] game.sbundle: 96000 frames, 1 channels at 48000 Hz
[20:53:40.180] [info] [runtime] [Screenshot.cpp:75] screenshot 1290x2796 written to /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/final.png
[20:53:40.180] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.50 ms a frame, 16.67 ms apart; GPU 11.289 ms: skinning 0.045 ms, cull 0.175 ms, shadow cascade 0 0.318 ms, shadow cascade 1 0.437 ms, shadow cascade 2 0.218 ms, shadow cascade 3 0.147 ms, depth 0.250 ms, light clustering 0.145 ms, forward 6.952 ms, bloom down 0 0.765 ms, bloom down 1 0.047 ms, bloom down 2 0.023 ms, bloom down 3 0.021 ms, bloom down 4 0.029 ms, bloom up 3 0.037 ms, bloom up 2 0.034 ms, bloom up 1 0.030 ms, bloom up 0 0.258 ms, tonemap 0.381 ms, fxaa 0.423 ms, present 0.556 ms
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 44 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[20:53:40.249] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`.

```sh
unset CC CXX
xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/final.png" --destination docs/agent-tasks/m10-device-checks/final.png
```

```text
20:54:47  Acquired tunnel connection to device.
20:54:47  Enabling developer disk image services.
20:54:47  Acquired usage assertion.
File received from Device
~/repositories/sonnet/docs/agent-tasks/m10-device-checks/final.png
```

Exit status: `0`.

### albedo

[PNG](m10-device-checks/albedo.png)

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --play 3 --shading-term albedo --screenshot albedo.png
```

```text
20:54:01  Acquired tunnel connection to device.
20:54:01  Enabling developer disk image services.
20:54:01  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:54:02.241] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:54:02.249 sonnet_player[11816:2499125] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:54:02.249] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:54:02.260] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilit[20:54:02.260] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
ies2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:54:02.265] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[20:54:02.268] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x10b36d480) on screen Main Screen.
[20:54:02.270] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:54:02.270] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:54:02.270] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:54:02.270] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:54:02.272] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:54:02.272] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:54:02.272] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:54:02.273] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:54:02.273] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:54:02.273] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:54:02.273] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:54:02.273] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:54:02.273] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:54:02.274] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:54:02.274] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:54:02.274] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:54:02.274] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:02.275] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:02.275] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:54:02.275] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:54:02.275] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:02.275] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:02.275] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:54:02.275] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:54:02.278] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:54:02.278] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:54:02.279] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:54:02.279] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:54:02.279] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:54:02.279] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:54:02.279] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:54:02.279] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:54:02.279] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:54:02.280] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:54:02.280] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:54:02.280] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:54:02.280] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:54:02.280] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:54:02.280] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:54:02.281] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:54:02.281] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:54:02.281] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[20:54:02.281] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:54:02.281] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.282] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.282] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:54:02.282] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[20:54:02.282] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.282] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:54:02.282] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:54:02.282] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.282] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:54:02.282] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:54:02.282] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.282] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:54:02.282] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:54:02.282] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:02.283] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:54:02.283] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:54:02.283] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:54:02.283] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:54:02.283] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:54:02.284] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:54:02.284] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:54:02.284] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:54:02.284] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:54:02.284] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:54:02.284] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:54:02.285] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:54:02.285] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:54:02.285] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:54:02.285] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:54:02.285] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:54:02.285] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:54:02.286] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:54:02.286] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:02.286] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:02.286] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:54:02.288] [debug] [world] [World.cpp:103] world ready with 13 components
[20:54:02.289] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:54:02.289] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:54:02.289] [debug] [world] [Animation.cpp:59] animation ready
[20:54:02.395] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:54:02.395] [info] [player] [main.cpp:94] capture run on "Apple A17 Pro GPU", Vulkan 1.4.357, writing /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/albedo.png
[20:54:02.395] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:54:02.395] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:54:02.397] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:54:02.401] [info] [player] [main.cpp:207] back from the background after 422572.0 s, 0 frames in it
[20:54:02.401] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:54:02.403] [info] [player] [main.cpp:207] back from the background after 422572.0 s, 0 frames in it
[20:54:02.403] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:54:02.407] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:54:02.407] [debug] [renderer] [Renderer.cpp:522] mesh "CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:02.408] [debug] [renderer] [Renderer.cpp:577] texture "image 0": 128x128 ASTC6x6Srgb, 8 levels
[20:54:02.408] [debug] [renderer] [Renderer.cpp:577] texture "image 1": 128x128 ASTC4x4Unorm, 8 levels
[20:54:02.408] [debug] [renderer] [Renderer.cpp:522] mesh "BallMesh": 559 vertices, 960 triangles, 1 submeshes
[20:54:02.408] [debug] [renderer] [Renderer.cpp:522] mesh "LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:02.408] [debug] [renderer] [Renderer.cpp:522] mesh "HaloMesh": 323 vertices, 528 triangles, 1 submeshes
[20:54:02.408] [debug] [renderer] [Renderer.cpp:522] mesh "StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:54:02.408] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:54:02.408] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:54:02.408] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:54:02.414 sonnet_player[11816:2499125] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x10b274000>.
2026-09-28 20:54:02.414 sonnet_player[11816:2499125] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x10b274000>.
[20:54:02.598] [debug] [audio] [MiniaudioDevice.cpp:258] game.sbundle: 96000 frames, 1 channels at 48000 Hz
[20:54:05.808] [info] [runtime] [Screenshot.cpp:75] screenshot 1290x2796 written to /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/albedo.png
[20:54:05.808] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.51 ms a frame, 16.67 ms apart; GPU 11.917 ms: skinning 0.028 ms, cull 0.195 ms, shadow cascade 0 0.280 ms, shadow cascade 1 0.508 ms, shadow cascade 2 0.267 ms, shadow cascade 3 0.144 ms, depth 0.283 ms, light clustering 0.128 ms, forward 7.335 ms, bloom down 0 0.850 ms, bloom down 1 0.051 ms, bloom down 2 0.021 ms, bloom down 3 0.020 ms, bloom down 4 0.029 ms, bloom up 3 0.031 ms, bloom up 2 0.041 ms, bloom up 1 0.024 ms, bloom up 0 0.296 ms, tonemap 0.370 ms, fxaa 0.427 ms, present 0.589 ms
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 44 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[20:54:05.870] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`.

```sh
unset CC CXX
xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/albedo.png" --destination docs/agent-tasks/m10-device-checks/albedo.png
```

```text
20:54:47  Acquired tunnel connection to device.
20:54:47  Enabling developer disk image services.
20:54:47  Acquired usage assertion.
File received from Device
~/repositories/sonnet/docs/agent-tasks/m10-device-checks/albedo.png
```

Exit status: `0`.

### normal

[PNG](m10-device-checks/normal.png)

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --play 3 --shading-term normal --screenshot normal.png
```

```text
20:54:19  Acquired tunnel connection to device.
20:54:19  Enabling developer disk image services.
20:54:19  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:54:20.406] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:54:20.412 sonnet_player[11817:2499403] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:54:20.412] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:54:20.422] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilit[20:54:20.422] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
ies2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:54:20.427] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[20:54:20.430] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x109355440) on screen Main Screen.
[20:54:20.432] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:54:20.432] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:54:20.432] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:54:20.432] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:54:20.434] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:54:20.434] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:54:20.434] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:54:20.435] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:54:20.435] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:54:20.435] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:54:20.435] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:54:20.435] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:54:20.435] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:54:20.435] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:54:20.435] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:54:20.436] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:54:20.436] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:20.437] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:20.437] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:54:20.437] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:54:20.437] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:20.437] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:20.437] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:54:20.437] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:54:20.440] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:54:20.440] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:54:20.441] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:54:20.441] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:54:20.441] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:54:20.441] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:54:20.441] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:54:20.441] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:54:20.441] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:54:20.441] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:54:20.441] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:54:20.442] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:54:20.442] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:54:20.442] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:54:20.442] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:54:20.443] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:54:20.443] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:54:20.443] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.443] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:54:20.443] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[20:54:20.444] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.444] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:54:20.444] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.444] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.444] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:54:20.444] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:54:20.444] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.444] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:54:20.444] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:54:20.444] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.444] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:54:20.445] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:54:20.445] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:20.445] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:54:20.445] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:54:20.445] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:54:20.445] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:54:20.445] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:54:20.446] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:54:20.446] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:54:20.446] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:54:20.446] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:54:20.447] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:54:20.447] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:54:20.447] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:54:20.447] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:54:20.447] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:54:20.447] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:54:20.447] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:54:20.447] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:54:20.448] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:54:20.448] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:20.448] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:20.448] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:54:20.450] [debug] [world] [World.cpp:103] world ready with 13 components
[20:54:20.451] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:54:20.451] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:54:20.451] [debug] [world] [Animation.cpp:59] animation ready
[20:54:20.552] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:54:20.552] [info] [player] [main.cpp:94] capture run on "Apple A17 Pro GPU", Vulkan 1.4.357, writing /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/normal.png
[20:54:20.553] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:54:20.553] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:54:20.553] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:54:20.557] [info] [player] [main.cpp:207] back from the background after 422590.2 s, 0 frames in it
[20:54:20.557] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:54:20.559] [info] [player] [main.cpp:207] back from the background after 422590.2 s, 0 frames in it
[20:54:20.559] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:522] mesh "CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:20.562] [debug] [renderer] [Renderer.cpp:577] texture "image 0": 128x128 ASTC6x6Srgb, 8 levels
[20:54:20.563] [debug] [renderer] [Renderer.cpp:577] texture "image 1": 128x128 ASTC4x4Unorm, 8 levels
[20:54:20.563] [debug] [renderer] [Renderer.cpp:522] mesh "BallMesh": 559 vertices, 960 triangles, 1 submeshes
[20:54:20.563] [debug] [renderer] [Renderer.cpp:522] mesh "LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:20.563] [debug] [renderer] [Renderer.cpp:522] mesh "HaloMesh": 323 vertices, 528 triangles, 1 submeshes
[20:54:20.563] [debug] [renderer] [Renderer.cpp:522] mesh "StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:54:20.563] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:54:20.563] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:54:20.563] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:54:20.569 sonnet_player[11817:2499403] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109268000>.
2026-09-28 20:54:20.570 sonnet_player[11817:2499403] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109268000>.
[20:54:20.781] [debug] [audio] [MiniaudioDevice.cpp:258] game.sbundle: 96000 frames, 1 channels at 48000 Hz
[20:54:23.982] [info] [runtime] [Screenshot.cpp:75] screenshot 1290x2796 written to /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/normal.png
[20:54:23.982] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.49 ms a frame, 16.67 ms apart; GPU 10.526 ms: skinning 0.088 ms, cull 0.190 ms, shadow cascade 0 0.301 ms, shadow cascade 1 0.454 ms, shadow cascade 2 0.138 ms, shadow cascade 3 0.150 ms, depth 0.236 ms, light clustering 0.143 ms, forward 6.395 ms, bloom down 0 0.714 ms, bloom down 1 0.041 ms, bloom down 2 0.018 ms, bloom down 3 0.015 ms, bloom down 4 0.018 ms, bloom up 3 0.023 ms, bloom up 2 0.029 ms, bloom up 1 0.018 ms, bloom up 0 0.277 ms, tonemap 0.348 ms, fxaa 0.400 ms, present 0.530 ms
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 44 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[20:54:24.048] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`.

```sh
unset CC CXX
xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/normal.png" --destination docs/agent-tasks/m10-device-checks/normal.png
```

```text
20:54:48  Acquired tunnel connection to device.
20:54:48  Enabling developer disk image services.
20:54:48  Acquired usage assertion.
File received from Device
~/repositories/sonnet/docs/agent-tasks/m10-device-checks/normal.png
```

Exit status: `0`.

### playground

[PNG](m10-device-checks/playground.png)

```sh
unset CC CXX
xcrun devicectl device process launch --console --device <device> io.github.pacheco95.sonnet --play 3 --scene scenes/playground.scene.json --screenshot playground.png
```

```text
20:54:34  Acquired tunnel connection to device.
20:54:34  Enabling developer disk image services.
20:54:34  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:54:35.037] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:54:35.046 sonnet_player[11818:2499685] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:54:35.047] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:54:35.059] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilit[20:54:35.059] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
ies2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:54:35.064] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[20:54:35.067] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x15c7595c0) on screen Main Screen.
[20:54:35.069] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:54:35.070] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:54:35.070] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:54:35.070] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:54:35.072] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:54:35.072] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:54:35.072] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:54:35.073] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:54:35.073] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:54:35.073] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:54:35.073] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:54:35.073] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:54:35.073] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:54:35.073] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:54:35.073] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:54:35.074] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:54:35.074] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:35.074] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:35.074] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:54:35.075] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:54:35.075] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:54:35.075] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:54:35.075] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:54:35.075] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:54:35.078] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:54:35.078] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:54:35.078] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:54:35.078] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:54:35.078] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:54:35.078] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:54:35.079] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:54:35.079] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:54:35.079] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:54:35.079] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:54:35.079] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:54:35.079] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:54:35.079] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:54:35.080] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:54:35.080] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:54:35.080] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:54:35.080] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:54:35.081] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.081] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:54:35.081] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[20:54:35.081] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.082] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:54:35.082] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.082] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.082] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:54:35.082] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:54:35.082] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.082] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:54:35.082] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:54:35.082] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.082] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:54:35.082] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:54:35.082] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:54:35.083] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:54:35.083] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:54:35.083] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:54:35.084] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:54:35.084] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:54:35.084] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:54:35.084] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:54:35.084] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:54:35.084] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:54:35.085] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:54:35.085] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:54:35.085] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:54:35.085] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:54:35.085] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:54:35.085] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:54:35.085] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:54:35.085] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:54:35.086] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:54:35.086] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:35.086] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:54:35.086] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:54:35.088] [debug] [world] [World.cpp:103] world ready with 13 components
[20:54:35.089] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:54:35.089] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:54:35.089] [debug] [world] [Animation.cpp:59] animation ready
[20:54:35.195] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:54:35.196] [info] [player] [main.cpp:94] capture run on "Apple A17 Pro GPU", Vulkan 1.4.357, writing /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/playground.png
[20:54:35.196] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:54:35.196] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:54:35.198] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:54:35.205] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:54:35.205] [debug] [renderer] [Renderer.cpp:522] mesh "Cylinder": 134 vertices, 128 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "CrateMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:577] texture "image 0": 128x128 ASTC6x6Srgb, 8 levels
[20:54:35.206] [debug] [renderer] [Renderer.cpp:577] texture "image 1": 128x128 ASTC4x4Unorm, 8 levels
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "BallMesh": 559 vertices, 960 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "LampMesh": 24 vertices, 12 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "HaloMesh": 323 vertices, 528 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:522] mesh "StemMesh": 91 vertices, 144 triangles, 1 submeshes
[20:54:35.206] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:54:35.206] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:54:35.206] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:54:35.207] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:54:35.207] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:54:35.207] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:54:35.207] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
[20:54:35.212] [info] [runtime] [Game.cpp:217] playing "Basic": 21 entities
2026-09-28 20:54:35.215 sonnet_player[11818:2499685] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x15c668000>.
2026-09-28 20:54:35.215 sonnet_player[11818:2499685] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x15c668000>.
[20:54:35.394] [info] [scripting] [game.sbundle:14] W, A, S and D roll the ball, Space jumps, a held finger pulls it; click the viewport first
[20:54:36.880] [debug] [audio] [MiniaudioDevice.cpp:258] game.sbundle: 57600 frames, 1 channels at 48000 Hz
[20:54:38.593] [info] [runtime] [Screenshot.cpp:75] screenshot 1290x2796 written to /var/mobile/Containers/Data/Application/B07EB86D-9223-482E-AAB1-B761D0DAB244/Library/Application Support/sonnet/player/playground.png
[20:54:38.593] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.90 ms a frame, 16.67 ms apart; GPU 11.753 ms: cull 0.107 ms, shadow cascade 0 0.316 ms, shadow cascade 1 0.594 ms, shadow cascade 2 0.505 ms, shadow cascade 3 0.258 ms, depth 0.431 ms, light clustering 0.101 ms, forward 5.416 ms, bloom down 0 1.147 ms, bloom down 1 0.076 ms, bloom down 2 0.048 ms, bloom down 3 0.024 ms, bloom down 4 0.035 ms, bloom up 3 0.035 ms, bloom up 2 0.029 ms, bloom up 1 0.156 ms, bloom up 0 0.585 ms, tonemap 0.611 ms, fxaa 0.637 ms, present 0.644 ms
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 44 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[20:54:38.649] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`.

```sh
unset CC CXX
xcrun devicectl device copy from --device <device> --domain-type appDataContainer --domain-identifier io.github.pacheco95.sonnet --source "Library/Application Support/sonnet/player/playground.png" --destination docs/agent-tasks/m10-device-checks/playground.png
```

```text
20:54:48  Acquired tunnel connection to device.
20:54:48  Enabling developer disk image services.
20:54:48  Acquired usage assertion.
File received from Device
~/repositories/sonnet/docs/agent-tasks/m10-device-checks/playground.png
```

Exit status: `0`.

## Check 4: background and foreground

**PASS — performed by Michael Pacheco.** The user sent Sonnet to the Home Screen and reopened it. Verbatim observation:

```text
Yes, drawing and moving after returning
```

Console around the suspend and resume (including the duplicate lifecycle callbacks, 92 background frames, and UIKit diagnostic):

```text
[20:59:09.850] [info] [player] [main.cpp:194] entering the background
[20:59:09.867] [info] [rhi] [VulkanSwapchain.cpp:164] swapchain suspended
[20:59:09.875] [info] [audio] [MiniaudioDevice.cpp:132] audio paused
[20:59:09.876] [info] [player] [main.cpp:194] entering the background
[20:59:09.876] [debug] [rhi] [VulkanSwapchain.cpp:155] swapchain already suspended
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 0" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 1" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 2" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 3" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene hdr" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 0" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 1" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 2" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 3" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 4" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 3" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 2" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 1" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 0" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene ldr" released
[20:59:22.259] [info] [player] [main.cpp:207] back from the background after 12.4 s, 92 frames in it
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x11b6c7ec0) on screen Main Screen.
[20:59:22.264] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:59:22.264] [info] [rhi] [VulkanSwapchain.cpp:192] swapchain resumed at 1290x2796
[20:59:22.384] [info] [audio] [MiniaudioDevice.cpp:145] audio resumed
[20:59:22.389] [info] [player] [main.cpp:207] back from the background after 12.5 s, 92 frames in it
[20:59:22.389] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:59:22.396 sonnet_player[11823:2501899] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109278000>.
```


## Check 5: touch

**PASS — performed by Michael Pacheco.** A finger was held near the yellow ball in the playground. Verbatim user observation:

```text
Yes, it rolls toward my finger
```

This is Michael Pacheco’s observation during the persistent playground run.

## Check 6: frame times

**PASS — all four capture summaries recorded.** This report-only branch leaves roadmap integration for M10 closure; check 3 has not met its no-warning criterion.

final:

```text
[20:53:40.180] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.50 ms a frame, 16.67 ms apart; GPU 11.289 ms: skinning 0.045 ms, cull 0.175 ms, shadow cascade 0 0.318 ms, shadow cascade 1 0.437 ms, shadow cascade 2 0.218 ms, shadow cascade 3 0.147 ms, depth 0.250 ms, light clustering 0.145 ms, forward 6.952 ms, bloom down 0 0.765 ms, bloom down 1 0.047 ms, bloom down 2 0.023 ms, bloom down 3 0.021 ms, bloom down 4 0.029 ms, bloom up 3 0.037 ms, bloom up 2 0.034 ms, bloom up 1 0.030 ms, bloom up 0 0.258 ms, tonemap 0.381 ms, fxaa 0.423 ms, present 0.556 ms
```

albedo:

```text
[20:54:05.808] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.51 ms a frame, 16.67 ms apart; GPU 11.917 ms: skinning 0.028 ms, cull 0.195 ms, shadow cascade 0 0.280 ms, shadow cascade 1 0.508 ms, shadow cascade 2 0.267 ms, shadow cascade 3 0.144 ms, depth 0.283 ms, light clustering 0.128 ms, forward 7.335 ms, bloom down 0 0.850 ms, bloom down 1 0.051 ms, bloom down 2 0.021 ms, bloom down 3 0.020 ms, bloom down 4 0.029 ms, bloom up 3 0.031 ms, bloom up 2 0.041 ms, bloom up 1 0.024 ms, bloom up 0 0.296 ms, tonemap 0.370 ms, fxaa 0.427 ms, present 0.589 ms
```

normal:

```text
[20:54:23.982] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.49 ms a frame, 16.67 ms apart; GPU 10.526 ms: skinning 0.088 ms, cull 0.190 ms, shadow cascade 0 0.301 ms, shadow cascade 1 0.454 ms, shadow cascade 2 0.138 ms, shadow cascade 3 0.150 ms, depth 0.236 ms, light clustering 0.143 ms, forward 6.395 ms, bloom down 0 0.714 ms, bloom down 1 0.041 ms, bloom down 2 0.018 ms, bloom down 3 0.015 ms, bloom down 4 0.018 ms, bloom up 3 0.023 ms, bloom up 2 0.029 ms, bloom up 1 0.018 ms, bloom up 0 0.277 ms, tonemap 0.348 ms, fxaa 0.400 ms, present 0.530 ms
```

playground:

```text
[20:54:38.593] [info] [player] [main.cpp:152] capture frame times over the last 100 frames: CPU 0.90 ms a frame, 16.67 ms apart; GPU 11.753 ms: cull 0.107 ms, shadow cascade 0 0.316 ms, shadow cascade 1 0.594 ms, shadow cascade 2 0.505 ms, shadow cascade 3 0.258 ms, depth 0.431 ms, light clustering 0.101 ms, forward 5.416 ms, bloom down 0 1.147 ms, bloom down 1 0.076 ms, bloom down 2 0.048 ms, bloom down 3 0.024 ms, bloom down 4 0.035 ms, bloom up 3 0.035 ms, bloom up 2 0.029 ms, bloom up 1 0.156 ms, bloom up 0 0.585 ms, tonemap 0.611 ms, fxaa 0.637 ms, present 0.644 ms
```

## Additional observed output

The first trusted capture entered the background during startup, then resumed and finished. Its full log above records 405 frames while backgrounded. Each launch also logged a foreground event without a preceding suspend, with a large time-away value. These startup events are not counted as the requested hands-on lifecycle test.

User prerequisite reply:

```text
Profile trusted; phone unlocked
```

## Full hands-on console log

```sh
unset CC CXX
xcrun devicectl device process launch --console --terminate-existing --device <device> io.github.pacheco95.sonnet --scene scenes/playground.scene.json
```

```text
20:58:28  Acquired tunnel connection to device.
20:58:28  Enabling developer disk image services.
20:58:28  Acquired usage assertion.
Launched application with io.github.pacheco95.sonnet bundle identifier.
Waiting for the application to terminate…
[20:58:29.649] [info] [platform] [SdlEntryPoint.cpp:57] Sonnet 0.11.0
2026-09-28 20:58:29.657 sonnet_player[11823:2501899] You need UIApplicationSupportsIndirectInputEvents in your Info.plist for mouse support
[20:58:29.657] [info] [platform] [Platform.cpp:90] SDL 3.4.16 initialised, video driver "uikit"
[20:58:29.666] [debug] [platform] [SdlWindow.cpp:33] window "Sonnet" created: 1280x720 logical, 1290x2796 pixels
[mvk-info] MoltenVK version 1.4.2, supporting Vulkan version 1.4.357.
	The following 153 Vulkan extensions are supported:
	VK_KHR_16bit_storage v1
	VK_KHR_8bit_storage v1
	VK_KHR_bind_memory2 v1
	VK_KHR_buffer_device_address v1
	VK_KHR_calibrated_timestamps v1
	VK_KHR_copy_commands2 v1
	VK_KHR_create_renderpass2 v1
	VK_KHR_dedicated_allocation v3
	VK_KHR_deferred_host_operations v4
	VK_KHR_depth_stencil_resolve v1
	VK_KHR_descriptor_update_template v1
	VK_KHR_device_group v4
	VK_KHR_device_group_creation v1
	VK_KHR_driver_properties v1
	VK_KHR_dynamic_rendering v1
	VK_KHR_dynamic_rendering_local_read v1
	VK_KHR_external_fence v1
	VK_KHR_external_fence_capabilities v1
	VK_KHR_external_memory v1
	VK_KHR_external_memory_capabilities v1
	VK_KHR_external_semaphore v1
	VK_KHR_external_semaphore_capabilities v1
	VK_KHR_format_feature_flags2 v2
	VK_KHR_fragment_shader_barycentric v1
	VK_KHR_get_memory_requirements2 v1
	VK_KHR_get_physical_device_properties2 v2
	VK_KHR_get_surface_capabilit[20:58:29.666] [debug] [platform] [Platform.cpp:68] Vulkan loader "/private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/sonnet_player" kept mapped for the process
ies2 v1
	VK_KHR_global_priority v1
	VK_KHR_image_format_list v1
	VK_KHR_imageless_framebuffer v1
	VK_KHR_incremental_present v2
	VK_KHR_index_type_uint8 v1
	VK_KHR_line_rasterization v1
	VK_KHR_load_store_op_none v1
	VK_KHR_maintenance1 v2
	VK_KHR_maintenance2 v1
	VK_KHR_maintenance3 v1
	VK_KHR_maintenance4 v2
	VK_KHR_maintenance5 v1
	VK_KHR_maintenance6 v1
	VK_KHR_maintenance7 v1
	VK_KHR_maintenance8 v1
	VK_KHR_maintenance9 v1
	VK_KHR_map_memory2 v1
	VK_KHR_multiview v1
	VK_KHR_portability_subset v1
	VK_KHR_present_id v1
	VK_KHR_present_id2 v1
	VK_KHR_present_wait v1
	VK_KHR_present_wait2 v1
	VK_KHR_push_descriptor v2
	VK_KHR_relaxed_block_layout v1
	VK_KHR_robustness2 v1
	VK_KHR_sampler_mirror_clamp_to_edge v3
	VK_KHR_sampler_ycbcr_conversion v14
	VK_KHR_separate_depth_stencil_layouts v1
	VK_KHR_shader_draw_parameters v1
	VK_KHR_shader_expect_assume v1
	VK_KHR_shader_float_controls v4
	VK_KHR_shader_float_controls2 v1
	VK_KHR_shader_float16_int8 v1
	VK_KHR_shader_fma v1
	VK_KHR_shader_integer_dot_product v1
	VK_KHR_shader_maximal_reconvergence v1
	VK_KHR_shader_non_semantic_info v1
	VK_KHR_shader_quad_control v1
	VK_KHR_shader_relaxed_extended_instruction v1
	VK_KHR_shader_subgroup_extended_types v1
	VK_KHR_shader_subgroup_rotate v2
	VK_KHR_shader_subgroup_uniform_control_flow v1
	VK_KHR_shader_terminate_invocation v1
	VK_KHR_spirv_1_4 v1
	VK_KHR_storage_buffer_storage_class v1
	VK_KHR_surface v25
	VK_KHR_surface_maintenance1 v1
	VK_KHR_surface_protected_capabilities v1
	VK_KHR_swapchain v70
	VK_KHR_swapchain_maintenance1 v1
	VK_KHR_swapchain_mutable_format v1
	VK_KHR_synchronization2 v1
	VK_KHR_timeline_semaphore v2
	VK_KHR_uniform_buffer_standard_layout v1
	VK_KHR_variable_pointers v1
	VK_KHR_vertex_attribute_divisor v1
	VK_KHR_vulkan_memory_model v3
	VK_KHR_zero_initialize_workgroup_memory v1
	VK_EXT_4444_formats v1
	VK_EXT_buffer_device_address v2
	VK_EXT_calibrated_timestamps v2
	VK_EXT_debug_marker v4
	VK_EXT_debug_report v10
	VK_EXT_debug_utils v2
	VK_EXT_depth_clip_control v1
	VK_EXT_descriptor_indexing v2
	VK_EXT_extended_dynamic_state v1
	VK_EXT_extended_dynamic_state2 v1
	VK_EXT_extended_dynamic_state3 v2
	VK_EXT_external_memory_host v1
	VK_EXT_external_memory_metal v1
	VK_EXT_fragment_shader_interlock v1
	VK_EXT_global_priority v2
	VK_EXT_global_priority_query v1
	VK_EXT_headless_surface v1
	VK_EXT_host_image_copy v1
	VK_EXT_host_query_reset v1
	VK_EXT_image_2d_view_of_3d v1
	VK_EXT_image_robustness v1
	VK_EXT_index_type_uint8 v1
	VK_EXT_inline_uniform_block v1
	VK_EXT_layer_settings v2
	VK_EXT_legacy_dithering v2
	VK_EXT_line_rasterization v1
	VK_EXT_load_store_op_none v1
	VK_EXT_memory_budget v1
	VK_EXT_metal_objects v2
	VK_EXT_metal_surface v1
	VK_EXT_non_seamless_cube_map v1
	VK_EXT_pipeline_creation_cache_control v3
	VK_EXT_pipeline_creation_feedback v1
	VK_EXT_pipeline_robustness v1
	VK_EXT_post_depth_coverage v1
	VK_EXT_primitive_topology_list_restart v1
	VK_EXT_private_data v1
	VK_EXT_provoking_vertex v1
	VK_EXT_robustness2 v1
	VK_EXT_sample_locations v1
	VK_EXT_sampler_filter_minmax v2
	VK_EXT_scalar_block_layout v1
	VK_EXT_separate_stencil_usage v1
	VK_EXT_shader_atomic_float v1
	VK_EXT_shader_demote_to_helper_invocation v1
	VK_EXT_shader_stencil_export v1
	VK_EXT_shader_subgroup_ballot v1
	VK_EXT_shader_subgroup_vote v1
	VK_EXT_shader_viewport_index_layer v1
	VK_EXT_subgroup_size_control v2
	VK_EXT_surface_maintenance1 v1
	VK_EXT_swapchain_colorspace v5
	VK_EXT_swapchain_maintenance1 v1
	VK_EXT_texel_buffer_alignment v1
	VK_EXT_texture_compression_astc_hdr v1
	VK_EXT_tooling_info v1
	VK_EXT_vertex_attribute_divisor v3
	VK_AMD_gpu_shader_half_float v2
	VK_AMD_negative_viewport_height v1
	VK_AMD_shader_image_load_store_lod v1
	VK_AMD_shader_trinary_minmax v1
	VK_GOOGLE_display_timing v1
	VK_IMG_format_pvrtc v1
	VK_INTEL_shader_integer_functions2 v1
	VK_MVK_ios_surface v3
	VK_MVK_moltenvk v37
	VK_NV_fragment_shader_barycentric v1
[mvk-info] GPU device:
	model: Apple A17 Pro GPU
	type: Integrated
	vendorID: 0x106b
	deviceID: 0x1b000009
	pipelineCacheUUID: DB660224-1B00-0009-0000-000100000000
	GPU memory available: 5461 MB
	GPU memory used: 0 MB
	Metal Shading Language 4.0
	supports the following GPU Features:
		GPU Family Metal 4
		GPU Family Apple 9
		Read-Write Texture Tier 2
[mvk-info] Created VkInstance for Vulkan version 1.4.357, as requested by app, with the following 3 Vulkan extensions enabled:
	VK_KHR_surface v25
	VK_EXT_debug_utils v2
	VK_EXT_metal_surface v1
[20:58:29.672] [debug] [rhi] [VulkanDevice.cpp:247] Vulkan loader 1.4.357
[mvk-info] Vulkan semaphores using MTLEvent.
[mvk-info] Descriptor sets binding resources using Metal3 argument buffers.
[20:58:29.675] [info] [rhi] [VulkanDevice.cpp:165] Vulkan 1.4.357 device "Apple A17 Pro GPU", driver MoltenVK 1.4.2, loader 1.4.357, BC, ASTC
[mvk-info] Created VkDevice to run on GPU Apple A17 Pro GPU with the following 3 Vulkan extensions enabled:
	VK_KHR_portability_subset v1
	VK_KHR_swapchain v70
	VK_EXT_memory_budget v1
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x1093694c0) on screen Main Screen.
[20:58:29.677] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:58:29.677] [debug] [core] [JobSystem.cpp:78] job system started with 5 workers
[20:58:29.678] [debug] [renderer] [Renderer.cpp:417] shader module cluster: 8212 bytes
[20:58:29.678] [debug] [renderer] [Renderer.cpp:423] pipeline "light clustering" from cluster
[20:58:29.680] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "light clustering" from shader "cluster"
[20:58:29.680] [debug] [renderer] [Renderer.cpp:417] shader module cull: 5420 bytes
[20:58:29.680] [debug] [renderer] [Renderer.cpp:423] pipeline "cull" from cull
[20:58:29.681] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cull" from shader "cull"
[20:58:29.681] [debug] [renderer] [Renderer.cpp:423] pipeline "clear draw commands" from cull
[20:58:29.681] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "clear draw commands" from shader "cull"
[20:58:29.681] [debug] [renderer] [Renderer.cpp:417] shader module debug: 1892 bytes
[20:58:29.681] [debug] [renderer] [Renderer.cpp:423] pipeline "debug lines" from debug
[20:58:29.682] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "debug lines" from shader "debug"
[20:58:29.682] [debug] [renderer] [Renderer.cpp:417] shader module depth: 9740 bytes
[20:58:29.682] [debug] [renderer] [Renderer.cpp:423] pipeline "depth" from depth
[20:58:29.682] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth" from shader "depth"
[20:58:29.682] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:58:29.683] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:58:29.683] [debug] [renderer] [Renderer.cpp:423] pipeline "depth double sided" from depth
[20:58:29.683] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "depth double sided" from shader "depth"
[20:58:29.683] [debug] [renderer] [Renderer.cpp:423] pipeline "shadow" from depth
[20:58:29.683] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "shadow" from shader "depth"
[20:58:29.684] [debug] [renderer] [Renderer.cpp:417] shader module forward: 27936 bytes
[20:58:29.684] [debug] [renderer] [Renderer.cpp:423] pipeline "forward" from forward
[20:58:29.687] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward" from shader "forward"
[20:58:29.687] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend" from forward
[20:58:29.687] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend" from shader "forward"
[20:58:29.687] [debug] [renderer] [Renderer.cpp:423] pipeline "forward double sided" from forward
[20:58:29.687] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward double sided" from shader "forward"
[20:58:29.687] [debug] [renderer] [Renderer.cpp:423] pipeline "forward blend double sided" from forward
[20:58:29.687] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "forward blend double sided" from shader "forward"
[20:58:29.688] [debug] [renderer] [Renderer.cpp:417] shader module ibl: 17176 bytes
[20:58:29.688] [debug] [renderer] [Renderer.cpp:423] pipeline "equirect to cube" from ibl
[20:58:29.688] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "equirect to cube" from shader "ibl"
[20:58:29.688] [debug] [renderer] [Renderer.cpp:423] pipeline "cube mip" from ibl
[20:58:29.688] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "cube mip" from shader "ibl"
[20:58:29.688] [debug] [renderer] [Renderer.cpp:423] pipeline "irradiance" from ibl
[20:58:29.689] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "irradiance" from shader "ibl"
[20:58:29.689] [debug] [renderer] [Renderer.cpp:423] pipeline "prefilter" from ibl
[20:58:29.689] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "prefilter" from shader "ibl"
[20:58:29.689] [debug] [renderer] [Renderer.cpp:423] pipeline "brdf lut" from ibl
[20:58:29.689] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "brdf lut" from shader "ibl"
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.690] [debug] [renderer] [Renderer.cpp:417] shader module id: 6972 bytes
[20:58:29.690] [debug] [renderer] [Renderer.cpp:423] pipeline "id" from id
[20:58:29.690] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.690] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id" from shader "id"
[20:58:29.690] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask" from id
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[mvk-warn] VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.691] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.691] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask" from shader "id"
[20:58:29.691] [debug] [renderer] [Renderer.cpp:423] pipeline "id double sided" from id
[20:58:29.691] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.691] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "id double sided" from shader "id"
[20:58:29.691] [debug] [renderer] [Renderer.cpp:423] pipeline "selection mask double sided" from id
[20:58:29.691] [warn] [rhi] [VulkanDevice.cpp:1331] mvk-warn: VK_ERROR_FEATURE_NOT_PRESENT: Blending is enabled for attachment with format VK_FORMAT_R32_UINT, which does not support it.
[20:58:29.691] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "selection mask double sided" from shader "id"
[20:58:29.691] [debug] [renderer] [Renderer.cpp:417] shader module outline: 2952 bytes
[20:58:29.691] [debug] [renderer] [Renderer.cpp:423] pipeline "outline" from outline
[20:58:29.691] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "outline" from shader "outline"
[20:58:29.691] [debug] [renderer] [Renderer.cpp:417] shader module post: 13296 bytes
[20:58:29.691] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom downsample" from post
[20:58:29.692] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom downsample" from shader "post"
[20:58:29.692] [debug] [renderer] [Renderer.cpp:423] pipeline "bloom upsample" from post
[20:58:29.692] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "bloom upsample" from shader "post"
[20:58:29.692] [debug] [renderer] [Renderer.cpp:423] pipeline "tonemap" from post
[20:58:29.693] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "tonemap" from shader "post"
[20:58:29.693] [debug] [renderer] [Renderer.cpp:423] pipeline "fxaa" from post
[20:58:29.693] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "fxaa" from shader "post"
[20:58:29.693] [debug] [renderer] [Renderer.cpp:423] pipeline "present" from post
[20:58:29.693] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "present" from shader "post"
[20:58:29.693] [debug] [renderer] [Renderer.cpp:417] shader module skin: 5084 bytes
[20:58:29.693] [debug] [renderer] [Renderer.cpp:423] pipeline "skinning" from skin
[20:58:29.694] [debug] [rhi] [VulkanDevice.cpp:1029] compute pipeline "skinning" from shader "skin"
[20:58:29.694] [debug] [renderer] [Renderer.cpp:417] shader module skybox: 6360 bytes
[20:58:29.694] [debug] [renderer] [Renderer.cpp:423] pipeline "skybox" from skybox
[20:58:29.694] [debug] [rhi] [VulkanDevice.cpp:1013] pipeline "skybox" from shader "skybox"
[20:58:29.694] [debug] [renderer] [Renderer.cpp:577] texture "white": 1x1 R8G8B8A8Unorm, 1 levels
[20:58:29.695] [debug] [renderer] [Renderer.cpp:577] texture "flat normal": 1x1 R8G8B8A8Unorm, 1 levels
[20:58:29.695] [debug] [renderer] [Renderer.cpp:327] renderer ready, shaders from shaders
[20:58:29.697] [debug] [world] [World.cpp:103] world ready with 13 components
[20:58:29.697] [debug] [physics] [JoltPhysicsWorld.cpp:227] physics ready
[20:58:29.698] [debug] [scripting] [LuaScriptRuntime.cpp:219] scripting ready, Lua 5.5.1
[20:58:29.698] [debug] [world] [Animation.cpp:59] animation ready
[20:58:29.801] [debug] [audio] [MiniaudioDevice.cpp:71] audio ready: 48000 Hz, 2 channels, output device
[20:58:29.801] [info] [assets] [Bundle.cpp:178] opened bundle "Basic" at /private/var/containers/Bundle/Application/49D24EBC-AA93-4440-9FC3-E5C0370577DE/sonnet_player.app/game.sbundle (27 assets, 4 files, cooked for ios by 0.11.0)
[20:58:29.801] [info] [assets] [AssetDatabase.cpp:279] asset database: 27 cooked assets from game.sbundle
[20:58:29.802] [info] [runtime] [Game.cpp:217] playing "Basic": 15 entities
[20:58:29.803] [info] [runtime] [Game.cpp:217] playing "Basic": 21 entities
[20:58:29.813] [info] [scripting] [game.sbundle:14] W, A, S and D roll the ball, Space jumps, a held finger pulls it; click the viewport first
[20:58:29.813] [debug] [renderer] [Renderer.cpp:522] mesh "Plane": 4 vertices, 2 triangles, 1 submeshes
[20:58:29.813] [debug] [renderer] [Renderer.cpp:577] texture "checker": 64x64 ASTC6x6Srgb, 7 levels
[20:58:29.813] [debug] [renderer] [Renderer.cpp:522] mesh "Box": 24 vertices, 12 triangles, 1 submeshes
[20:58:29.814] [debug] [renderer] [Renderer.cpp:522] mesh "Sphere": 561 vertices, 960 triangles, 1 submeshes
[20:58:29.814] [debug] [renderer] [Renderer.cpp:522] mesh "Capsule": 594 vertices, 1024 triangles, 1 submeshes
[20:58:29.814] [debug] [renderer] [Renderer.cpp:686] environment "sky" from a 256x128 map
[20:58:29.814] [debug] [renderer] [RenderTarget.cpp:38] render target "game" 1290x2796
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:58:29.814] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:58:29.831 sonnet_player[11823:2501899] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109278000>.
2026-09-28 20:58:29.831 sonnet_player[11823:2501899] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109278000>.
[20:58:31.304] [debug] [audio] [MiniaudioDevice.cpp:258] game.sbundle: 57600 frames, 1 channels at 48000 Hz
[20:59:09.850] [info] [player] [main.cpp:194] entering the background
[20:59:09.867] [info] [rhi] [VulkanSwapchain.cpp:164] swapchain suspended
[20:59:09.875] [info] [audio] [MiniaudioDevice.cpp:132] audio paused
[20:59:09.876] [info] [player] [main.cpp:194] entering the background
[20:59:09.876] [debug] [rhi] [VulkanSwapchain.cpp:155] swapchain already suspended
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 0" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 1" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 2" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "shadow cascade 3" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene hdr" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 0" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 1" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 2" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 3" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom down 4" released
[20:59:09.909] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 3" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 2" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 1" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "bloom up 0" released
[20:59:09.910] [debug] [renderer] [RenderGraph.cpp:192] transient image "scene ldr" released
[20:59:22.259] [info] [player] [main.cpp:207] back from the background after 12.4 s, 92 frames in it
[mvk-info] Created 3 swapchain images with size (1290, 2796) and contents scale 3.0 in layer SDL_uikitmetalview (SDL_uikitviewcontroller) (0x11b6c7ec0) on screen Main Screen.
[20:59:22.264] [debug] [rhi] [VulkanSwapchain.cpp:127] swapchain 1290x2796, 3 images, B8G8R8A8Unorm, Fifo, surface transform Identity
[20:59:22.264] [info] [rhi] [VulkanSwapchain.cpp:192] swapchain resumed at 1290x2796
[20:59:22.384] [info] [audio] [MiniaudioDevice.cpp:145] audio resumed
[20:59:22.389] [info] [player] [main.cpp:207] back from the background after 12.5 s, 92 frames in it
[20:59:22.389] [debug] [rhi] [VulkanSwapchain.cpp:169] swapchain resumed without a suspend: nothing to do
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 0" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 1" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 2" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "shadow cascade 3" 2048x2048 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene hdr" 1290x2796 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 0" 645x1398 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 1" 322x699 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 2" 161x349 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 3" 80x174 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom down 4" 40x87 allocated
[20:59:22.392] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 3" 80x174 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 2" 161x349 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 1" 322x699 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "bloom up 0" 645x1398 allocated
[20:59:22.393] [debug] [renderer] [RenderGraph.cpp:176] transient image "scene ldr" 1290x2796 allocated
2026-09-28 20:59:22.396 sonnet_player[11823:2501899] Unbalanced calls to begin/end appearance transitions for <SDL_uikitviewcontroller: 0x109278000>.
[mvk-info] Destroyed VkDevice on GPU Apple A17 Pro GPU with 3 Vulkan extensions enabled.
[mvk-info] Destroyed VkPhysicalDevice for GPU Apple A17 Pro GPU with 87 MB of GPU memory still allocated.
[mvk-info] Destroying VkInstance for Vulkan version 1.4.357 with 3 Vulkan extensions enabled.
[21:01:33.798] [info] [platform] [SdlEntryPoint.cpp:96] exit ok
The app terminated with the exit code 0.
```

Exit status: `0`. The agent ended this persistent run with SIGTERM only after both hands-on observations had arrived.

```sh
unset CC CXX
xcrun devicectl device process signal --device <device> --pid 11823 --signal SIGTERM
```

```text
21:01:33  Enabling developer disk image services.
21:01:33  Acquired usage assertion.
Sent signal SIGTERM to pid 11823
```

Exit status: `0`.

## PNG integrity

The PNGs are the files copied from the app container, without editing. Their only chunk types are `IHDR`, `IDAT`, and `IEND`; no text or EXIF metadata is present.

```sh
unset CC CXX
shasum -a 256 docs/agent-tasks/m10-device-checks/check1.png docs/agent-tasks/m10-device-checks/final.png docs/agent-tasks/m10-device-checks/albedo.png docs/agent-tasks/m10-device-checks/normal.png docs/agent-tasks/m10-device-checks/playground.png
```

```text
1b297832ffff21bb6015a3f07b4f7b10c40cbc0a202bf02752db08cb6ccec830  docs/agent-tasks/m10-device-checks/check1.png
52e375eef7e864c20ae8e9a6a4244fc6f54383fa52b2601fadb1039c3b630215  docs/agent-tasks/m10-device-checks/final.png
4889dd32968dffb1f59e5071254551bfe77eda4771a4012233871b99d4b1bcf9  docs/agent-tasks/m10-device-checks/albedo.png
02434ac4bcc2b6d10f3be25c85000faaeca09785e6e8b848d525fe2c16355c7c  docs/agent-tasks/m10-device-checks/normal.png
61d5d1ffa4e8bb1f1420f08196f29a7554995394013a092e2688fef64f8fee56  docs/agent-tasks/m10-device-checks/playground.png
```

## Validation and privacy audit

`python3 tools/check_docs.py`:

```text
checked 42 markdown files, modules=['assets', 'audio', 'core', 'editor', 'physics', 'platform', 'renderer', 'rhi', 'runtime', 'scripting', 'ui', 'world'], ports=21, capture flags=7, player=5
OK: no broken links, anchors, or consistency gaps
```

Before committing, fixed-string `grep -nF` searches used the actual private team ID, CoreDevice identifier, UDID, compact UDID, serial, device name, hostnames, ECID, signing identity, and local home path as patterns. Every search returned 1 with no output. A separate `grep -nE` for user-home paths also returned 1 with no output. Private search arguments are intentionally not reproduced. The final Git identity was checked again and remained `Michael Pacheco <mdpgd95@gmail.com>`.

`git diff --cached --check` reports trailing whitespace inside verbatim command-output blocks. It is retained to preserve the requested output, rather than silently trimmed. Exit status: `2`.

```text
docs/agent-tasks/m10-device-checks-report.md:339: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:479: trailing whitespace.
+FAILED: [code=1] modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanSwapchain.cpp.o 
docs/agent-tasks/m10-device-checks-report.md:605: trailing whitespace.
+FAILED: [code=1] modules/rhi/CMakeFiles/sonnet_rhi.dir/src/VulkanDevice.cpp.o 
docs/agent-tasks/m10-device-checks-report.md:873: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1194: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1509: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1511: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1517: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1546: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1550: trailing whitespace.
+    
docs/agent-tasks/m10-device-checks-report.md:1593: trailing whitespace.
+• options: 
```
