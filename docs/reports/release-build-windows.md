# Windows Release build check (issue #36)

CI only builds Debug-type presets, so Release and RelWithDebInfo had never been checked on Windows before this. Checked at commit `40ca7a6` on `main`, on a Windows 11 (build 10.0.26200) machine with Visual Studio 18.5.11709 (Community, C++ workload) and clang-cl 23.1.1 from the same LLVM 20 install line. `VCPKG_ROOT` pointed at a local vcpkg checkout at the manifest's baseline.

Four configure+build passes, each in its own fresh build directory from the `windows-release` preset, building with `-- -k 0` so every failure is collected:

| Build | Configure | `CMAKE_CXX_COMPILER` | Compiler version | Build type |
|---|---|---|---|---|
| MSVC RelWithDebInfo | `cmake --preset windows-release -B build/rel-check-msvc-relwithdebinfo --fresh` | `.../VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64/cl.exe` | 19.50.35729.0 | RelWithDebInfo |
| MSVC Release | same, `-DCMAKE_BUILD_TYPE=Release` | same cl.exe | 19.50.35729.0 | Release |
| clang-cl RelWithDebInfo | same, `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` | `C:/Program Files/LLVM/bin/clang-cl.exe` | 23.1.1 | RelWithDebInfo |
| clang-cl Release | same, plus `-DCMAKE_BUILD_TYPE=Release` | same clang-cl.exe | 23.1.1 | Release |

Configure succeeded (exit 0) for all four; no configure-time errors to report. clang-cl was already installed, so no vcpkg-port rebuild was needed to reach it (it resolved through `PATH`, the same LLVM install `check_setup.py` reports).

## MSVC RelWithDebInfo — succeeded

Build exit code 0, no `FAILED:` lines, no diagnostics of any kind.

`ctest --test-dir build/rel-check-msvc-relwithdebinfo --output-on-failure`: 100% of 12 suites passed (31.16 s total).

| # | Suite | Result |
|---|---|---|
| 1 | `core_tests` | Passed |
| 2 | `platform_tests` | Passed |
| 3 | `rhi_tests` | Passed |
| 4 | `renderer_tests` | Passed |
| 5 | `assets_tests` | Passed |
| 6 | `world_tests` | Passed |
| 7 | `physics_tests` | Passed |
| 8 | `scripting_tests` | Passed |
| 9 | `audio_tests` | Passed |
| 10 | `runtime_tests` | Passed |
| 11 | `ui_tests` | Passed |
| 12 | `editor_tests` | Passed |

## MSVC Release — failed, 2 objects

Build exit code 2. `SONNET_ASSERTS_ENABLED` is off in plain Release, so two assertion-only values become unused there and nowhere else. Both diagnostics are `/W4`-family MSVC warnings turned into hard errors by `COMPILE_WARNING_AS_ERROR`.

| File | Line | Warning | Source line | Occurrences |
|---|---|---|---|---|
| `modules/rhi/src/OwnerThread.h` | 16 | C4100 (unreferenced parameter `owner`) | `inline void assertOwnerThread(std::thread::id owner, std::string_view what) {` | 2 (reached from `modules/rhi/src/NullDevice.cpp:504` and `modules/rhi/src/VulkanDevice.cpp:976`) |
| `modules/rhi/src/OwnerThread.h` | 16 | C4100 (unreferenced parameter `what`) | `inline void assertOwnerThread(std::thread::id owner, std::string_view what) {` | 2 (same two call sites) |
| `modules/rhi/src/VulkanDevice.cpp` | 795 | C4189 (unreferenced local variable `expected`) | `const std::uint64_t expected = levelByteSize(desc.format, size);` | 1 |

Failed objects:

| Target | Source |
|---|---|
| `sonnet_rhi` | `modules/rhi/src/NullDevice.cpp` |
| `sonnet_rhi` | `modules/rhi/src/VulkanDevice.cpp` |

No other diagnostics appeared anywhere else in the log; ninja stopped scheduling further work once its two `sonnet_rhi` object files failed and modules depending on `sonnet_rhi` had nothing left to build, so `-k 0` never gets far enough to try Release-only issues in modules that link after `rhi`.

## clang-cl RelWithDebInfo — failed, 137 objects, single root cause

Build exit code 1. Every one of the 137 C++ translation units in the project (engine sources, tests, and the three app `main.cpp` files) fails the same way, on the very first diagnostic clang-cl produces, before any of the project's own source code is read:

| Diagnostic | Occurrences |
|---|---|
| `clang-cl: error: argument unused during compilation: '/Zc:preprocessor' [-Werror,-Wunused-command-line-argument]` | 137 |

This is not tied to a repository line: it is the compiler driver rejecting one of its own command-line arguments (`/Zc:preprocessor`, added by `cmake/SonnetWarnings.cmake` for MSVC-family compilers) before invoking the front end, on every single compile job the same way — including the one compiling `modules/rhi`'s precompiled header (`cmake_pch.cxx`, a CMake-generated file, not a repository source). No project source is ever reached, so nothing else clang-cl might have reported (the NDEBUG-only unused-parameter/variable cases MSVC hits in Release, for instance) is visible yet.

Failed objects (target, source file), 137 total:

<details>
<summary>Full list</summary>

| Target | Source |
|---|---|
| `assets_tests` | `tests/support/TestSupport.cpp` |
| `assets_tests` | `modules/assets/tests/AnimationTests.cpp` |
| `assets_tests` | `modules/assets/tests/BundleTests.cpp` |
| `assets_tests` | `modules/assets/tests/CookTests.cpp` |
| `assets_tests` | `modules/assets/tests/DatabaseTests.cpp` |
| `assets_tests` | `modules/assets/tests/GltfTests.cpp` |
| `assets_tests` | `modules/assets/tests/JsonTests.cpp` |
| `assets_tests` | `modules/assets/tests/ProjectTests.cpp` |
| `assets_tests` | `modules/assets/tests/TextureTests.cpp` |
| `audio_tests` | `tests/support/TestSupport.cpp` |
| `audio_tests` | `modules/audio/tests/AudioTests.cpp` |
| `core_tests` | `tests/support/TestSupport.cpp` |
| `core_tests` | `modules/core/tests/ErrorTests.cpp` |
| `core_tests` | `modules/core/tests/FileTests.cpp` |
| `core_tests` | `modules/core/tests/HandlePoolTests.cpp` |
| `core_tests` | `modules/core/tests/HandleTests.cpp` |
| `core_tests` | `modules/core/tests/JobSystemTests.cpp` |
| `core_tests` | `modules/core/tests/LogTests.cpp` |
| `core_tests` | `modules/core/tests/UuidTests.cpp` |
| `core_tests` | `modules/core/tests/VersionTests.cpp` |
| `editor_tests` | `tests/support/TestSupport.cpp` |
| `editor_tests` | `modules/editor/tests/CaptureTests.cpp` |
| `editor_tests` | `modules/editor/tests/CommandTests.cpp` |
| `editor_tests` | `modules/editor/tests/EditorPlayTests.cpp` |
| `editor_tests` | `modules/editor/tests/EditorTests.cpp` |
| `editor_tests` | `modules/editor/tests/FlyCameraTests.cpp` |
| `editor_tests` | `modules/editor/tests/GizmoTests.cpp` |
| `editor_tests` | `modules/editor/tests/InspectorTests.cpp` |
| `editor_tests` | `modules/editor/tests/LogPanelTests.cpp` |
| `editor_tests` | `modules/editor/tests/ProjectTests.cpp` |
| `editor_tests` | `modules/editor/tests/ShaderTests.cpp` |
| `physics_tests` | `tests/support/TestSupport.cpp` |
| `physics_tests` | `modules/physics/tests/PhysicsTests.cpp` |
| `platform_tests` | `tests/support/TestSupport.cpp` |
| `platform_tests` | `modules/platform/tests/EventTests.cpp` |
| `platform_tests` | `modules/platform/tests/InputStateTests.cpp` |
| `platform_tests` | `modules/platform/tests/PlatformTests.cpp` |
| `platform_tests` | `modules/platform/tests/WindowTests.cpp` |
| `renderer_tests` | `tests/support/TestSupport.cpp` |
| `renderer_tests` | `modules/renderer/tests/CameraTests.cpp` |
| `renderer_tests` | `modules/renderer/tests/PickingTests.cpp` |
| `renderer_tests` | `modules/renderer/tests/PrimitivesTests.cpp` |
| `renderer_tests` | `modules/renderer/tests/RenderGraphTests.cpp` |
| `renderer_tests` | `modules/renderer/tests/RendererTests.cpp` |
| `rhi_tests` | `tests/support/TestSupport.cpp` |
| `rhi_tests` | `modules/rhi/tests/BindlessTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/DeviceTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/IdImageTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/IndirectTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/MeshTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/NullDeviceTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/ResourceTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/SwapchainTests.cpp` |
| `rhi_tests` | `modules/rhi/tests/TriangleTests.cpp` |
| `runtime_tests` | `tests/support/TestSupport.cpp` |
| `runtime_tests` | `modules/runtime/tests/GameTests.cpp` |
| `scripting_tests` | `tests/support/TestSupport.cpp` |
| `scripting_tests` | `modules/scripting/tests/ScriptingTests.cpp` |
| `sonnet_assets` | `modules/assets/src/Animation.cpp` |
| `sonnet_assets` | `modules/assets/src/Asset.cpp` |
| `sonnet_assets` | `modules/assets/src/AssetDatabase.cpp` |
| `sonnet_assets` | `modules/assets/src/Bundle.cpp` |
| `sonnet_assets` | `modules/assets/src/Cook.cpp` |
| `sonnet_assets` | `modules/assets/src/GltfImporter.cpp` |
| `sonnet_assets` | `modules/assets/src/ImageImporter.cpp` |
| `sonnet_assets` | `modules/assets/src/Json.cpp` |
| `sonnet_assets` | `modules/assets/src/Ktx2.cpp` |
| `sonnet_assets` | `modules/assets/src/MeshCook.cpp` |
| `sonnet_assets` | `modules/assets/src/Project.cpp` |
| `sonnet_audio` | `modules/audio/src/Components.cpp` |
| `sonnet_audio` | `modules/audio/src/MiniaudioDevice.cpp` |
| `sonnet_audio` | `modules/audio/src/MiniaudioImplementation.cpp` |
| `sonnet_cook_app` | `apps/cook/main.cpp` |
| `sonnet_core` | `modules/core/src/Assert.cpp` |
| `sonnet_core` | `modules/core/src/Error.cpp` |
| `sonnet_core` | `modules/core/src/File.cpp` |
| `sonnet_core` | `modules/core/src/JobSystem.cpp` |
| `sonnet_core` | `modules/core/src/Log.cpp` |
| `sonnet_core` | `modules/core/src/Uuid.cpp` |
| `sonnet_core` | `modules/core/src/Version.cpp` |
| `sonnet_editor` | `modules/editor/src/AssetBrowserPanel.cpp` |
| `sonnet_editor` | `modules/editor/src/AssetCommands.cpp` |
| `sonnet_editor` | `modules/editor/src/Capture.cpp` |
| `sonnet_editor` | `modules/editor/src/CommandStack.cpp` |
| `sonnet_editor` | `modules/editor/src/Editor.cpp` |
| `sonnet_editor` | `modules/editor/src/EntityCommands.cpp` |
| `sonnet_editor` | `modules/editor/src/Export.cpp` |
| `sonnet_editor` | `modules/editor/src/FlyCamera.cpp` |
| `sonnet_editor` | `modules/editor/src/Gizmo.cpp` |
| `sonnet_editor` | `modules/editor/src/HierarchyPanel.cpp` |
| `sonnet_editor` | `modules/editor/src/InspectorPanel.cpp` |
| `sonnet_editor` | `modules/editor/src/LogPanel.cpp` |
| `sonnet_editor` | `modules/editor/src/Preferences.cpp` |
| `sonnet_editor` | `modules/editor/src/Project.cpp` |
| `sonnet_editor` | `modules/editor/src/Selection.cpp` |
| `sonnet_editor` | `modules/editor/src/ShaderCompiler.cpp` |
| `sonnet_editor` | `modules/editor/src/StatisticsPanel.cpp` |
| `sonnet_editor` | `modules/editor/src/ViewportPanel.cpp` |
| `sonnet_editor_app` | `apps/editor/main.cpp` |
| `sonnet_physics` | `modules/physics/src/ColliderLines.cpp` |
| `sonnet_physics` | `modules/physics/src/Components.cpp` |
| `sonnet_physics` | `modules/physics/src/JoltJobSystem.cpp` |
| `sonnet_physics` | `modules/physics/src/JoltPhysicsWorld.cpp` |
| `sonnet_platform` | `modules/platform/src/Input.cpp` |
| `sonnet_platform` | `modules/platform/src/InputState.cpp` |
| `sonnet_platform` | `modules/platform/src/Platform.cpp` |
| `sonnet_platform` | `modules/platform/src/SdlEntryPoint.cpp` |
| `sonnet_platform` | `modules/platform/src/SdlEvents.cpp` |
| `sonnet_platform` | `modules/platform/src/SdlWindow.cpp` |
| `sonnet_player_app` | `apps/player/main.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Camera.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Mesh.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Picker.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Primitives.cpp` |
| `sonnet_renderer` | `modules/renderer/src/RenderGraph.cpp` |
| `sonnet_renderer` | `modules/renderer/src/RenderTarget.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Renderer.cpp` |
| `sonnet_renderer` | `modules/renderer/src/Texture.cpp` |
| `sonnet_rhi` | `cmake_pch.cxx` (generated, not a repository file) |
| `sonnet_runtime` | `modules/runtime/src/Game.cpp` |
| `sonnet_scripting` | `modules/scripting/src/Components.cpp` |
| `sonnet_scripting` | `modules/scripting/src/LuaReflection.cpp` |
| `sonnet_scripting` | `modules/scripting/src/LuaScriptRuntime.cpp` |
| `sonnet_ui` | `modules/ui/src/ImGuiLayer.cpp` |
| `sonnet_world` | `modules/world/src/Animation.cpp` |
| `sonnet_world` | `modules/world/src/Components.cpp` |
| `sonnet_world` | `modules/world/src/DrawList.cpp` |
| `sonnet_world` | `modules/world/src/Scene.cpp` |
| `sonnet_world` | `modules/world/src/World.cpp` |
| `ui_tests` | `tests/support/TestSupport.cpp` |
| `ui_tests` | `modules/ui/tests/ImGuiLayerTests.cpp` |
| `world_tests` | `tests/support/TestSupport.cpp` |
| `world_tests` | `modules/world/tests/AnimationTests.cpp` |
| `world_tests` | `modules/world/tests/ComponentsTests.cpp` |
| `world_tests` | `modules/world/tests/DrawListTests.cpp` |
| `world_tests` | `modules/world/tests/SceneTests.cpp` |
| `world_tests` | `modules/world/tests/WorldTests.cpp` |

</details>

## clang-cl Release — failed, same 137 objects

Build exit code 1. The set of 137 failed objects is exactly the set from clang-cl RelWithDebInfo above (verified by diffing the two `FAILED:` lists after sorting; the only difference between the two logs is ninja's parallel scheduling order). The diagnostic is the same single `-Wunused-command-line-argument` error, 137 times, for the same reason: it happens before the front end runs, so the build type has nothing to do with it.

## Differences

- **RelWithDebInfo vs. Release**: differ for MSVC (RelWithDebInfo succeeds; Release fails on 2 objects, both `SONNET_ASSERT`-only parameters/locals that only go unused once asserts compile out). Do not differ for clang-cl (both fail identically on all 137 objects, for a reason unrelated to `NDEBUG` or asserts).
- **MSVC vs. clang-cl**: very different. MSVC gets all the way through RelWithDebInfo clean and only trips on the two documented assertion-only values in Release. clang-cl never reaches a single line of project source in either configuration — it rejects its own `/Zc:preprocessor` command-line argument first, on every translation unit, so none of the NDEBUG-only issues MSVC hits (or anything else) can be observed with clang-cl until that is addressed.

## Anything else that failed

Nothing else. All four configures exited 0 with no errors. No link errors occurred: MSVC RelWithDebInfo reached and passed linking (and all 12 test suites); MSVC Release, clang-cl RelWithDebInfo and clang-cl Release all failed during compilation and never reached a link step.

## Observations

MSVC Release's failure is exactly the two diagnostics `docs/reports` context predicted (`OwnerThread.h:16`, `VulkanDevice.cpp:795`) and nothing more — Windows has fewer Release-only surprises than Linux's GCC 14 or macOS's Apple Clang reportedly do. clang-cl's failure is categorically different and, on this evidence, unrelated to warnings-as-errors on Release-only unused values at all: `/Zc:preprocessor` is passed unconditionally by `cmake/SonnetWarnings.cmake` for MSVC-family compilers, and clang-cl's driver considers it unused and — because `-WX` maps to `-Werror` for clang-cl — turns that into a hard stop before compiling anything, in both build types. Until that single argument is resolved, clang-cl cannot be used to see whatever Release-only diagnostics might otherwise appear on this toolchain.
