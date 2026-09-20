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

# vcpkg builds Jolt without RTTI, and its target does not say so. A class deriving from a Jolt
# base whose virtuals live in the library (JobSystemWithBarrier) then fails to link, because the
# base's typeinfo was never emitted. This module derives from several and uses RTTI nowhere, so it
# is compiled the way Jolt was. Only this module: it is the only one that links Jolt.
target_compile_options(sonnet_physics PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/GR->
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>)

# UBSan's vptr check needs the typeinfo that is missing for the same reason; off here, and only here.
if(SONNET_SANITIZERS)
  target_compile_options(sonnet_physics PRIVATE -fno-sanitize=vptr)
endif()
