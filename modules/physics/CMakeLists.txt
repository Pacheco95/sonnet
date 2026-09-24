find_package(Jolt CONFIG REQUIRED)

sonnet_add_module(physics
  SOURCES
    src/ColliderLines.cpp
    src/Components.cpp
    src/JoltJobSystem.cpp
    src/JoltPhysicsWorld.cpp
  PUBLIC_DEPENDS
    sonnet::world
  DEPENDS
    # Private: Jolt's headers never appear in the module's public ones, and its instruction-set
    # flags (AVX2 on x64) stay on the module's own sources.
    Jolt::Jolt
)

sonnet_add_module_test(physics
  SOURCES
    tests/PhysicsTests.cpp
)
if(TARGET physics_tests)
  sonnet_add_engine_shaders(physics_tests)
endif()
