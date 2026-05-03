add_executable(SonnetEditorTests
    EditorInterfaceTest.cpp
    EditorLifecycleUnitTest.cpp
    PanelFlagsTest.cpp
    LayoutManagerTest.cpp
    LogPanelTest.cpp
)
target_link_libraries(SonnetEditorTests PRIVATE SonnetEditor Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetEditorTests)
catch_discover_tests(SonnetEditorTests)
