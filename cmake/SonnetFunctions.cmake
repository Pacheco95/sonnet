# sonnet_add_module(<name> SOURCES ... DEPENDS ... PUBLIC_DEPENDS ...)
#
# Creates the static library sonnet_<name> with alias sonnet::<name>, public headers under
# modules/<name>/include, the shared warning flags, and the declared dependencies. Linking is
# the only way a module reaches another, which is what enforces the one-way dependency rule.
function(sonnet_add_module NAME)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;PUBLIC_DEPENDS" ${ARGN})
  set(target sonnet_${NAME})

  add_library(${target} STATIC ${ARG_SOURCES})
  add_library(sonnet::${NAME} ALIAS ${target})

  target_include_directories(${target}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(${target}
    PUBLIC ${ARG_PUBLIC_DEPENDS}
    PRIVATE sonnet::warnings ${ARG_DEPENDS})
  # SONNET_MODULE names the logger the SONNET_LOG_* macros write to.
  target_compile_definitions(${target} PRIVATE SONNET_MODULE="${NAME}")
  target_compile_definitions(${target} PUBLIC
    $<${SONNET_DEBUGGABLE_CONFIG}:SONNET_ASSERTS_ENABLED=1>
    $<$<CONFIG:Release,MinSizeRel>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    $<$<NOT:$<CONFIG:Release,MinSizeRel>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>)
  set_target_properties(${target} PROPERTIES FOLDER "Modules")
  sonnet_apply_coverage(${target})
endfunction()

# sonnet_add_module_test(<name> SOURCES ... DEPENDS ...)
#
# Creates <name>_tests linked against sonnet::<name> and Catch2, registers it with CTest under the
# label <name>, and allows including the module's src/ for white-box tests.
function(sonnet_add_module_test NAME)
  if(NOT SONNET_BUILD_TESTS)
    return()
  endif()
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
  set(target ${NAME}_tests)

  find_package(Catch2 CONFIG REQUIRED)
  add_executable(${target} ${ARG_SOURCES})
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(${target} PRIVATE SONNET_MODULE="${target}")
  target_link_libraries(${target} PRIVATE sonnet::${NAME} sonnet::warnings Catch2::Catch2WithMain ${ARG_DEPENDS})
  set_target_properties(${target} PROPERTIES FOLDER "Tests")

  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES LABELS "${NAME}")
endfunction()
