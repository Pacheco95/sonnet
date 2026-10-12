# The game module's own test (ADR-0027), built with SONNET_GAME: the start scene played in
# runtime::Game and in the editor, both linked with the module, must move the same way and show
# the same text, since what plays in the editor is what ships.
find_package(Catch2 CONFIG REQUIRED)
add_executable(game_module_tests GameModuleTests.cpp "${CMAKE_SOURCE_DIR}/tests/support/TestSupport.cpp")
target_compile_definitions(game_module_tests PRIVATE SONNET_MODULE="game_module_tests"
                                                     SONNET_GAME_PROJECT="${SONNET_GAME_DIR}")
target_link_libraries(game_module_tests PRIVATE sonnet::editor sonnet::warnings Catch2::Catch2WithMain)
sonnet_link_game(game_module_tests)
set_target_properties(game_module_tests PROPERTIES FOLDER "Tests" COMPILE_WARNING_AS_ERROR ON)
sonnet_copy_tracy_client(game_module_tests)
sonnet_add_engine_shaders(game_module_tests)
sonnet_copy_slang_runtime(game_module_tests)
add_test(NAME game_module_tests COMMAND game_module_tests --allow-running-no-tests)
set_tests_properties(game_module_tests PROPERTIES LABELS "game" TIMEOUT 300)
