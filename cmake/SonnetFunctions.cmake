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
  set_target_properties(${target} PROPERTIES FOLDER "Modules" COMPILE_WARNING_AS_ERROR ON)
  sonnet_apply_coverage(${target})
endfunction()

# sonnet_copy_tracy_client(<target>)
#
# vcpkg's tracy port installs the Debug TracyClient.dll under debug/bin/Debug instead of
# debug/bin, which VCPKG_APPLOCAL_DEPS's copy step does not look in; a target that actually
# references Tracy symbols then fails to start with STATUS_DLL_NOT_FOUND. Tracy::TracyClient's
# IMPORTED_LOCATION is correct even though the convention-based copy step gets confused by it, so
# copying from the target's own TARGET_FILE side-steps the wrong assumption instead of hardcoding
# vcpkg's layout.
function(sonnet_copy_tracy_client TARGET)
  if(NOT WIN32)
    return()
  endif()
  set(tracy_enabled "$<AND:$<BOOL:${SONNET_ENABLE_TRACY}>,${SONNET_DEBUGGABLE_CONFIG}>")
  add_custom_command(TARGET ${TARGET} POST_BUILD
    COMMAND "$<${tracy_enabled}:${CMAKE_COMMAND};-E;copy_if_different;$<TARGET_FILE:Tracy::TracyClient>;$<TARGET_FILE_DIR:${TARGET}>>"
    COMMAND_EXPAND_LISTS VERBATIM)
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
  # TestSupport.cpp is compiled in rather than linked from a library so its static initialiser
  # cannot be dropped by the linker.
  add_executable(${target} ${ARG_SOURCES} "${CMAKE_SOURCE_DIR}/tests/support/TestSupport.cpp")
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(${target} PRIVATE SONNET_MODULE="${target}")
  target_link_libraries(${target} PRIVATE sonnet::${NAME} sonnet::warnings Catch2::Catch2WithMain ${ARG_DEPENDS})
  set_target_properties(${target} PROPERTIES FOLDER "Tests" COMPILE_WARNING_AS_ERROR ON)
  sonnet_copy_tracy_client(${target})

  # A binary whose every case skipped (rhi_tests without a Vulkan device) is a pass, not Catch2's exit code 4.
  add_test(NAME ${target} COMMAND ${target} --allow-running-no-tests)
  # A suite takes seconds; a hang (a blocked dialog on Windows, a driver wait) must fail with output.
  set_tests_properties(${target} PROPERTIES LABELS "${NAME}" TIMEOUT 300)
endfunction()

# sonnet_add_executable(<name> SOURCES ... DEPENDS ...)
#
# Creates the target sonnet_<name>_app, whose binary is named sonnet_<name>, with the shared
# warning flags and per-configuration definitions of an engine target. The target carries the
# suffix because an app usually shares its name with the module it fronts (`editor`), and the
# module owns the plain sonnet_<name> target. SONNET_MODULE is <name>, so an app logs under its
# own name.
function(sonnet_add_executable NAME)
  cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
  set(target sonnet_${NAME}_app)

  add_executable(${target} ${ARG_SOURCES})
  target_compile_definitions(${target} PRIVATE SONNET_MODULE="${NAME}")
  target_link_libraries(${target} PRIVATE sonnet::warnings ${ARG_DEPENDS})
  set_target_properties(${target} PROPERTIES FOLDER "Apps" COMPILE_WARNING_AS_ERROR ON OUTPUT_NAME sonnet_${NAME})
  sonnet_copy_tracy_client(${target})
endfunction()
