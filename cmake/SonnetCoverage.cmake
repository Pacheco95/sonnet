# gcov instrumentation for engine modules and a `coverage` target that runs the tests and gcovr.
# The compile flag is applied per module by sonnet_apply_coverage so third-party code is not
# instrumented; the link flag has to be global so every executable that links engine objects
# gets the gcov runtime.

function(sonnet_apply_coverage TARGET)
  if(SONNET_COVERAGE)
    target_compile_options(${TARGET} PRIVATE --coverage -O0 -g)
  endif()
endfunction()

if(NOT SONNET_COVERAGE)
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "SONNET_COVERAGE requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
endif()
add_link_options(--coverage)

find_program(GCOVR_EXECUTABLE gcovr)
if(NOT GCOVR_EXECUTABLE)
  message(FATAL_ERROR "SONNET_COVERAGE requires gcovr on PATH (pip install gcovr)")
endif()

# Clang and GCC emit incompatible .gcno formats, and each GCC major version its own; gcovr has to
# drive the reader that matches the compiler: llvm-cov for Clang, gcov-<major> for GCC. A plain
# `gcov` is the system GCC's, which on Ubuntu 24.04 is 13 and rejects GCC 14's data.
string(REGEX MATCH "^[0-9]+" _compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  find_program(LLVM_COV_EXECUTABLE NAMES "llvm-cov-${_compiler_major}" llvm-cov REQUIRED)
  set(_gcov_args --gcov-executable "${LLVM_COV_EXECUTABLE} gcov")
else()
  find_program(GCOV_EXECUTABLE NAMES "gcov-${_compiler_major}" gcov REQUIRED)
  set(_gcov_args --gcov-executable "${GCOV_EXECUTABLE}")
endif()

add_custom_target(coverage
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/coverage"
  COMMAND ${GCOVR_EXECUTABLE}
          --root "${CMAKE_SOURCE_DIR}"
          --filter "${CMAKE_SOURCE_DIR}/modules/"
          --exclude ".*/tests/.*"
          --exclude-throw-branches
          --print-summary
          --html-details "${CMAKE_BINARY_DIR}/coverage/index.html"
          --xml "${CMAKE_BINARY_DIR}/coverage/coverage.xml"
          ${_gcov_args}
          "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Running tests and writing ${CMAKE_BINARY_DIR}/coverage/index.html"
  VERBATIM USES_TERMINAL)
