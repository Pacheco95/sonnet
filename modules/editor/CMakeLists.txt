find_package(Stb REQUIRED)
find_package(slang CONFIG REQUIRED GLOBAL)

sonnet_add_module(editor
  SOURCES
    src/AssetBrowserPanel.cpp
    src/AssetCommands.cpp
    src/Capture.cpp
    src/CommandStack.cpp
    src/Editor.cpp
    src/EntityCommands.cpp
    src/Export.cpp
    src/FlyCamera.cpp
    src/Gizmo.cpp
    src/HierarchyPanel.cpp
    src/InspectorPanel.cpp
    src/LogPanel.cpp
    src/Preferences.cpp
    src/Project.cpp
    src/Selection.cpp
    src/ShaderCompiler.cpp
    src/StatisticsPanel.cpp
    src/ViewportPanel.cpp
  PUBLIC_DEPENDS
    sonnet::audio
    sonnet::scripting
    sonnet::ui
  DEPENDS
    slang::slang
)
# stb_image_write for the screenshots (docs/editor.md, "Screenshots").
target_include_directories(sonnet_editor SYSTEM PRIVATE ${Stb_INCLUDE_DIR})

sonnet_add_module_test(editor
  SOURCES
    tests/CaptureTests.cpp
    tests/CommandTests.cpp
    tests/EditorTests.cpp
    tests/EditorPlayTests.cpp
    tests/FlyCameraTests.cpp
    tests/GizmoTests.cpp
    tests/InspectorTests.cpp
    tests/LogPanelTests.cpp
    tests/ProjectTests.cpp
    tests/ShaderTests.cpp
)
if(TARGET editor_tests)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(editor_tests PRIVATE tests/VulkanLoaderTests.cpp)
    target_link_libraries(editor_tests PRIVATE ${CMAKE_DL_LIBS})
    # The SDK's loader search path otherwise hides a vcpkg RUNPATH regression.
    set_tests_properties(editor_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=unset:;SDL_VULKAN_LIBRARY=unset:")
  endif()
  sonnet_add_engine_shaders(editor_tests)
  sonnet_copy_slang_runtime(editor_tests)
  target_compile_definitions(editor_tests PRIVATE SONNET_ENGINE_SHADER_DIR="${SONNET_ENGINE_SHADER_DIR}")
  # stb_image, compiled into sonnet_assets, reads the screenshots back.
  target_include_directories(editor_tests SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
endif()
