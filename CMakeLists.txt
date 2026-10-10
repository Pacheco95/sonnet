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

# On macOS, `/usr/bin/clang++` is a shim that adds `-I/usr/local/include` to the compile, and a
# plain -I is searched before the vcpkg install root's -isystem. Where the Vulkan SDK has put its
# headers there, they shadow vcpkg's `vulkan-headers` and the build fails in vk-bootstrap's types
# (docs/reports/macos-vulkan-header-search.md). The toolchain's own compiler does not do it, so
# unless the caller chose one (CMAKE_CXX_COMPILER or CXX), configure with that.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND VCPKG_TARGET_TRIPLET MATCHES "-osx$"
   AND NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX}
   AND NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
  execute_process(COMMAND xcrun --find clang++ OUTPUT_VARIABLE sonnet_xcode_cxx
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE sonnet_xcrun_cxx)
  execute_process(COMMAND xcrun --find clang OUTPUT_VARIABLE sonnet_xcode_cc
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE sonnet_xcrun_cc)
  if(sonnet_xcrun_cxx EQUAL 0 AND sonnet_xcrun_cc EQUAL 0 AND EXISTS "${sonnet_xcode_cxx}" AND EXISTS "${sonnet_xcode_cc}")
    set(CMAKE_CXX_COMPILER "${sonnet_xcode_cxx}" CACHE FILEPATH "C++ compiler (the toolchain's, not the /usr/bin shim)")
    set(CMAKE_C_COMPILER "${sonnet_xcode_cc}" CACHE FILEPATH "C compiler (the toolchain's, not the /usr/bin shim)")
    message(STATUS "Using the toolchain's compiler ${sonnet_xcode_cxx}, not the /usr/bin shim")
  endif()
endif()

project(sonnet VERSION 0.16.0 LANGUAGES CXX C)

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
