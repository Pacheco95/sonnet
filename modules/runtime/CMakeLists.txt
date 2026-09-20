sonnet_add_module(runtime
  SOURCES
    src/Game.cpp
  PUBLIC_DEPENDS
    sonnet::audio
    sonnet::scripting
)

sonnet_add_module_test(runtime
  SOURCES
    tests/GameTests.cpp
)
if(TARGET runtime_tests)
  sonnet_add_engine_shaders(runtime_tests)
endif()
