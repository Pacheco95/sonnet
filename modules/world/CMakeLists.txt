find_package(flecs CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# world sits above renderer until the assets module arrives in M3 and takes that place
# (docs/architecture.md, "Module map").
sonnet_add_module(world
  SOURCES
    src/Components.cpp
    src/DrawList.cpp
    src/Scene.cpp
    src/World.cpp
  PUBLIC_DEPENDS
    sonnet::renderer
    $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
    nlohmann_json::nlohmann_json
)

sonnet_add_module_test(world
  SOURCES
    tests/ComponentsTests.cpp
    tests/DrawListTests.cpp
    tests/SceneTests.cpp
    tests/WorldTests.cpp
)
if(TARGET world_tests)
  sonnet_add_engine_shaders(world_tests)
endif()
