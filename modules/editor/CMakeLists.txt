sonnet_add_module(editor
  SOURCES
    src/Editor.cpp
    src/FlyCamera.cpp
    src/LogPanel.cpp
    src/PrimitiveScene.cpp
    src/StatisticsPanel.cpp
    src/ViewportPanel.cpp
  PUBLIC_DEPENDS
    sonnet::renderer
    sonnet::ui
)

sonnet_add_module_test(editor
  SOURCES
    tests/EditorTests.cpp
    tests/FlyCameraTests.cpp
    tests/LogPanelTests.cpp
    tests/PrimitiveSceneTests.cpp
)
if(TARGET editor_tests)
  sonnet_add_engine_shaders(editor_tests)
endif()
