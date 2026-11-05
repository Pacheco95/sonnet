find_package(Stb REQUIRED)
find_package(slang CONFIG REQUIRED GLOBAL)

sonnet_add_module(editor
  SOURCES
    src/AssetBrowserPanel.cpp
    src/AssetCommands.cpp
    src/CameraLines.cpp
    src/Capture.cpp
    src/CommandStack.cpp
    src/Editor.cpp
    src/EntityCommands.cpp
    src/Export.cpp
    src/FileDialog.cpp
    src/FileManager.cpp
    src/FlyCamera.cpp
    src/Fonts.cpp
    src/GamePanel.cpp
    src/Gizmo.cpp
    src/HierarchyPanel.cpp
    src/Icons.cpp
    src/InspectorPanel.cpp
    src/InterFontData.cpp
    src/JetBrainsMonoFontData.cpp
    src/Layout.cpp
    src/LightLines.cpp
    src/LogPanel.cpp
    src/LucideFontData.cpp
    src/ThumbnailRenderer.cpp
    src/Preferences.cpp
    src/Project.cpp
    src/Recovery.cpp
    src/ScriptSlots.cpp
    src/Selection.cpp
    src/ShaderCompiler.cpp
    src/StatisticsPanel.cpp
    src/ThumbnailCache.cpp
    src/TypeFilter.cpp
    src/ViewportDrop.cpp
    src/ViewportPanel.cpp
    src/WindowState.cpp
  PUBLIC_DEPENDS
    sonnet::audio
    sonnet::runtime
    sonnet::scripting
    sonnet::ui
  DEPENDS
    slang::slang
)
if(WIN32)
  # Show in file manager selects the file with SHOpenFolderAndSelectItems, which needs COM.
  target_link_libraries(sonnet_editor PRIVATE shell32 ole32)
endif()

# One binary, two CTest tests (issue #469): the suite is over half of the Linux jobs' test time, so on its own it
# was the run's floor under `ctest --parallel 2`. Registered by sonnet_add_catch2_test below.
sonnet_add_module_test(editor
  SHARDED
  SOURCES
    tests/AssetMenuTests.cpp
    tests/CameraLinesTests.cpp
    tests/CaptureTests.cpp
    tests/CommandTests.cpp
    tests/EditorTests.cpp
    tests/EditorPlayTests.cpp
    tests/FileDialogTests.cpp
    tests/FlyCameraTests.cpp
    tests/GameViewTests.cpp
    tests/GizmoTests.cpp
    tests/HierarchyPanelTests.cpp
    tests/InspectorTests.cpp
    tests/LayoutTests.cpp
    tests/LightLinesTests.cpp
    tests/LogPanelTests.cpp
    tests/ProjectTests.cpp
    tests/RecoveryTests.cpp
    tests/ScriptSlotsTests.cpp
    tests/ShaderTests.cpp
    tests/TypeFilterTests.cpp
    tests/ViewportDropTests.cpp
    tests/EditorDropTests.cpp
    tests/WindowOriginsTests.cpp
    tests/WindowStateTests.cpp
)
if(TARGET editor_tests)
  # The shards split the cases by source file, with Catch2's file-name tags (-#): editor_tests_1 runs the files
  # named here and editor_tests_2 every other file, so both specs come from this one list, a case is in exactly
  # one shard, and a new file joins editor_tests_2. editor_tests_1 also leaves out the hidden cases ([.]), which
  # a tag named in a spec would otherwise select; a spec of exclusions alone keeps them out already.
  # The files are the window cases and the two other files of whole-editor frames: in CI run 41930016334
  # they took 205 s of the suite's 345 s on Linux Clang and 229 s of 344 s under ASan. editor_tests_2 is
  # then about as long as runtime_tests, and the two end together on the second lane. Catch2's
  # --shard-count is not used: it slices the run order, which is random with a new seed in every
  # process, so two shards would overlap.
  set(shard_1_files EditorTests GameViewTests CaptureTests)
  set(shard_1_spec "")
  set(shard_2_spec "")
  foreach(file IN LISTS shard_1_files)
    if(shard_1_spec)
      string(APPEND shard_1_spec ",")
    endif()
    string(APPEND shard_1_spec "[#${file}]~[.]")
    string(APPEND shard_2_spec "~[#${file}]")
  endforeach()
  sonnet_add_catch2_test(editor_tests_1 editor_tests --filenames-as-tags "${shard_1_spec}")
  sonnet_add_catch2_test(editor_tests_2 editor_tests --filenames-as-tags "${shard_2_spec}")
  # TIMEOUT: about twice each shard's time in the Linux ASan job, the slowest (229 s and 115 s in that run), so a
  # failure still means a hang. COST: the shards start first, ahead of runtime_tests (its COST), and the other
  # suites follow in registration order (docs/build.md, "Continuous integration").
  set_tests_properties(editor_tests_1 PROPERTIES LABELS editor TIMEOUT 480 COST 200)
  set_tests_properties(editor_tests_2 PROPERTIES LABELS editor TIMEOUT 300 COST 140)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(editor_tests PRIVATE tests/VulkanLoaderTests.cpp)
    target_link_libraries(editor_tests PRIVATE ${CMAKE_DL_LIBS})
    # The SDK's loader search path otherwise hides a vcpkg RUNPATH regression.
    set_tests_properties(editor_tests_1 editor_tests_2 PROPERTIES
      ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=unset:;SDL_VULKAN_LIBRARY=unset:")
  endif()
  # Lavapipe, the CPU Vulkan driver CI renders with, starts a raster thread per CPU in each process, so the two
  # shards side by side oversubscribe a 4-CPU runner. Two threads each fills it without the contention.
  set_property(TEST editor_tests_1 editor_tests_2 APPEND PROPERTY ENVIRONMENT_MODIFICATION "LP_NUM_THREADS=set:2")
  sonnet_add_engine_shaders(editor_tests)
  sonnet_copy_slang_runtime(editor_tests)
  target_compile_definitions(editor_tests PRIVATE SONNET_ENGINE_SHADER_DIR="${SONNET_ENGINE_SHADER_DIR}")
  # stb_image, compiled into sonnet_assets, reads the screenshots back.
  target_include_directories(editor_tests SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
endif()
