add_executable(SonnetEditorTests
    EditorInterfaceTest.cpp
    EditorLifecycleUnitTest.cpp
    PanelFlagsTest.cpp
    LayoutManagerTest.cpp
    LogPanelTest.cpp
    SceneHierarchyPanelTests.cpp
    InspectorPanelTests.cpp
)
target_link_libraries(SonnetEditorTests PRIVATE SonnetEditor Catch2::Catch2WithMain)
# Expose the editor's private src directory so tests can include SceneContext.hpp etc.
target_include_directories(SonnetEditorTests PRIVATE ${CMAKE_SOURCE_DIR}/src/editor/src)
sonnet_set_compile_options(SonnetEditorTests)
catch_discover_tests(SonnetEditorTests)
