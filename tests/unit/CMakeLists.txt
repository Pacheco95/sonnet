add_subdirectory(editor)

add_executable(SonnetSceneTests
    scene/TransformTests.cpp
    scene/SceneTests.cpp
)
target_link_libraries(SonnetSceneTests PRIVATE SonnetScene Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetSceneTests)
catch_discover_tests(SonnetSceneTests)

add_executable(SonnetPrimitivesTests
    primitives/MeshPrimitivesTests.cpp
)
target_link_libraries(SonnetPrimitivesTests PRIVATE SonnetPrimitives Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetPrimitivesTests)
catch_discover_tests(SonnetPrimitivesTests)

add_executable(SonnetWindowTests
    window/WindowInterfaceTest.cpp
)
target_link_libraries(SonnetWindowTests PRIVATE SonnetWindow Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetWindowTests)
catch_discover_tests(SonnetWindowTests)

add_executable(SonnetRendererTests
    renderer/RendererInterfaceTest.cpp
)
target_link_libraries(SonnetRendererTests PRIVATE SonnetRenderer Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetRendererTests)
catch_discover_tests(SonnetRendererTests)

add_executable(SonnetLoggingTests
    logging/LoggerTest.cpp
)
target_link_libraries(SonnetLoggingTests PRIVATE SonnetLogging Catch2::Catch2WithMain)
sonnet_set_compile_options(SonnetLoggingTests)
catch_discover_tests(SonnetLoggingTests)
