find_package(Stb REQUIRED)

sonnet_add_module(runtime
  SOURCES
    src/Capture.cpp
    src/FrameTimes.cpp
    src/Game.cpp
    src/GameModule.cpp
    src/Screenshot.cpp
    src/TaaCheck.cpp
  PUBLIC_DEPENDS
    sonnet::audio
    sonnet::scripting
)
# stb_image_write for the screenshots (docs/player.md, "Capture runs").
target_include_directories(sonnet_runtime SYSTEM PRIVATE ${Stb_INCLUDE_DIR})

# COST: the second-longest suite, 160 s of the Linux Clang job's suites and 130 s under ASan in CI run 41930016334,
# so `ctest --parallel` starts it right after the editor suite rather than after the ten suites registered
# before it (issue #469).
sonnet_add_module_test(runtime
  COST 130
  SOURCES
    tests/CaptureTests.cpp
    tests/GameTests.cpp
    tests/GameModuleTests.cpp
    tests/TaaCheckTests.cpp
)
if(TARGET runtime_tests)
  sonnet_add_engine_shaders(runtime_tests)
  # stb_image, compiled into sonnet_assets, reads the screenshots back.
  target_include_directories(runtime_tests SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
endif()
