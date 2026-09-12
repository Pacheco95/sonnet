cmake_minimum_required(VERSION 3.28)

# The presets pass the vcpkg toolchain file. An IDE profile that does not use a preset gets it
# from VCPKG_ROOT, or from the conventional ~/vcpkg checkout, so a plain configure still works.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
  elseif(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
    message(STATUS "VCPKG_ROOT is not set; using $ENV{HOME}/vcpkg")
  else()
    message(FATAL_ERROR
      "Dependencies come from vcpkg (docs/build.md). Configure with a preset, e.g. "
      "`cmake --preset linux-debug`, with VCPKG_ROOT pointing at a vcpkg checkout, or pass "
      "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake.")
  endif()
endif()

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
include(SonnetShaders)

enable_testing()

add_subdirectory(modules)
add_subdirectory(apps)
