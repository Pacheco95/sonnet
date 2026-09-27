find_package(Stb REQUIRED)

sonnet_add_module(runtime
  SOURCES
    src/Capture.cpp
    src/FrameTimes.cpp
    src/Game.cpp
    src/Screenshot.cpp
  PUBLIC_DEPENDS
    sonnet::audio
    sonnet::scripting
)
# stb_image_write for the screenshots (docs/player.md, "Capture runs").
target_include_directories(sonnet_runtime SYSTEM PRIVATE ${Stb_INCLUDE_DIR})

sonnet_add_module_test(runtime
  SOURCES
    tests/CaptureTests.cpp
    tests/GameTests.cpp
)
if(TARGET runtime_tests)
  sonnet_add_engine_shaders(runtime_tests)
  # stb_image, compiled into sonnet_assets, reads the screenshots back.
  target_include_directories(runtime_tests SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
endif()
