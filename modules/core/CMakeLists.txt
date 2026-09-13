find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
# GLOBAL: sonnet_copy_tracy_client (cmake/SonnetFunctions.cmake) references Tracy::TracyClient
# from every module and app directory, not just core's.
find_package(Tracy CONFIG REQUIRED GLOBAL)

sonnet_add_module(core
  SOURCES
    src/Assert.cpp
    src/Error.cpp
    src/File.cpp
    src/Log.cpp
    src/Uuid.cpp
    src/Version.cpp
  PUBLIC_DEPENDS
    glm::glm
    spdlog::spdlog
    $<$<AND:$<BOOL:${SONNET_ENABLE_TRACY}>,${SONNET_DEBUGGABLE_CONFIG}>:Tracy::TracyClient>
)

# GLM is configured here and nowhere else (docs/conventions.md, "Math conventions").
target_compile_definitions(sonnet_core PUBLIC
  GLM_FORCE_RADIANS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_FORCE_EXPLICIT_CTOR
  GLM_ENABLE_EXPERIMENTAL
  $<$<AND:$<BOOL:${SONNET_ENABLE_TRACY}>,${SONNET_DEBUGGABLE_CONFIG}>:SONNET_ENABLE_TRACY=1>
)
target_compile_definitions(sonnet_core PRIVATE
  SONNET_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  SONNET_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  SONNET_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

sonnet_add_module_test(core
  SOURCES
    tests/ErrorTests.cpp
    tests/FileTests.cpp
    tests/HandlePoolTests.cpp
    tests/HandleTests.cpp
    tests/LogTests.cpp
    tests/UuidTests.cpp
    tests/VersionTests.cpp
)
