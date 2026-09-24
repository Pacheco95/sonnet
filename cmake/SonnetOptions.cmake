# Build options. Defaults follow docs/build.md; a change here is a change there.

set(SONNET_RHI "Vulkan" CACHE STRING "Graphics implementation")
set_property(CACHE SONNET_RHI PROPERTY STRINGS Vulkan)
if(NOT SONNET_RHI STREQUAL "Vulkan")
  message(FATAL_ERROR "SONNET_RHI=${SONNET_RHI}: only Vulkan exists")
endif()

if(ANDROID OR IOS)
  set(_sonnet_desktop OFF)
else()
  set(_sonnet_desktop ON)
endif()

include(CMakeDependentOption)
cmake_dependent_option(SONNET_BUILD_EDITOR "Build the editor" ON "_sonnet_desktop" OFF)
option(SONNET_BUILD_PLAYER "Build the generic player" ON)
option(SONNET_BUILD_TESTS "Build Catch2 tests and register them with CTest" ON)
option(SONNET_BUILD_SAMPLES "Copy the sample projects next to the binaries" ON)
option(SONNET_SANITIZERS "Address and undefined-behaviour sanitizers" OFF)
option(SONNET_THREAD_SANITIZER "Thread sanitizer, for the job system and what runs on it" OFF)
option(SONNET_COVERAGE "gcov instrumentation for engine modules" OFF)

# Single-config generators decide at configure time; multi-config ones through generator expressions.
# Both are handled by the two generator-expression conditions below, which the module helper applies.
set(SONNET_DEBUGGABLE_CONFIG "$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>")
set(SONNET_DEBUG_CONFIG "$<CONFIG:Debug>")

option(SONNET_ENABLE_TRACY "Compile Tracy zones in (Debug and RelWithDebInfo only unless forced)" ON)
option(SONNET_ENABLE_VALIDATION "Request Vulkan validation layers at instance creation (Debug only unless forced)" ON)

if(SONNET_SANITIZERS AND SONNET_THREAD_SANITIZER)
  # They instrument the same accesses in incompatible ways and no toolchain links both.
  message(FATAL_ERROR "SONNET_SANITIZERS and SONNET_THREAD_SANITIZER are mutually exclusive")
endif()

if(SONNET_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "SONNET_SANITIZERS requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
  endif()
  # A finding aborts rather than prints, so CI cannot pass with undefined behaviour in its log. The
  # flag is compiled in, so it holds however the binary is run, not only under the test preset.
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(SONNET_THREAD_SANITIZER)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "SONNET_THREAD_SANITIZER requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

message(STATUS "Sonnet ${PROJECT_VERSION}: rhi=${SONNET_RHI} editor=${SONNET_BUILD_EDITOR} "
               "player=${SONNET_BUILD_PLAYER} tests=${SONNET_BUILD_TESTS} samples=${SONNET_BUILD_SAMPLES} "
               "tracy=${SONNET_ENABLE_TRACY} validation=${SONNET_ENABLE_VALIDATION} "
               "sanitizers=${SONNET_SANITIZERS} tsan=${SONNET_THREAD_SANITIZER} coverage=${SONNET_COVERAGE}")
