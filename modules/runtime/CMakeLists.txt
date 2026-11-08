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

# The Linux ASan job took 220 to 292 s on this suite in CI on 2026-11-08 (runs 42524158664, 42523074718 and
# 42524485622), against 45 s locally, and passed the 300 s default once the character's fade and turn tests
# were both in. 600 s is twice that, so a failure still means a hang.
sonnet_add_module_test(runtime
  TIMEOUT 600
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
