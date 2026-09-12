cmake_minimum_required(VERSION 3.28)

project(sonnet VERSION 0.1.0 LANGUAGES CXX C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Configure with a preset: cmake --preset linux-debug")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(SonnetOptions)
include(SonnetWarnings)
include(SonnetFunctions)
include(SonnetCoverage)

enable_testing()

add_subdirectory(modules)
add_subdirectory(apps)
