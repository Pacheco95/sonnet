cmake_minimum_required(VERSION 4.2.1)
project(Sonnet CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
set(SONNET_LOG_LEVEL "info" CACHE STRING "Compile-time log level (trace/debug/info/warn/error/critical/off)")
set_property(CACHE SONNET_LOG_LEVEL PROPERTY STRINGS trace debug info warn error critical off)

option(SONNET_ENABLE_VALIDATION_LAYERS "Enable Vulkan VK_LAYER_KHRONOS_validation" $<CONFIG:Debug>)
option(SONNET_BUILD_TESTS "Build and register Catch2 tests" ON)

find_package(Vulkan REQUIRED)

include(cmake/FetchDependencies.cmake)
include(cmake/CompilerOptions.cmake)
include(cmake/ShaderCompilation.cmake)

add_subdirectory(src/logging)
add_subdirectory(src/window)
add_subdirectory(src/renderer)
add_subdirectory(src/renderer_vulkan)
add_subdirectory(src/app)

if(SONNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
