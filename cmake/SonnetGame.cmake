# The game module (ADR-0027, docs/player.md#game-modules). With SONNET_GAME set to a project
# folder, the build reads the module's name from its project.json, adds its native/ folder, which
# calls sonnet_add_game_module, and writes sonnet/game/LinkedGame.h for the player and the editor
# to call the module's registerGame. Without it the header names no module and both binaries stay
# generic. One build directory builds one game.
set(SONNET_GAME "" CACHE PATH "Project folder whose game module the player and the editor link, relative to the source tree or absolute")

set(SONNET_GAME_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/game")
set(SONNET_GAME_MODULE "")
if(SONNET_GAME)
  cmake_path(ABSOLUTE_PATH SONNET_GAME BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE SONNET_GAME_DIR)
  set(_sonnet_game_project "${SONNET_GAME_DIR}/project.json")
  if(NOT EXISTS "${_sonnet_game_project}")
    message(FATAL_ERROR "SONNET_GAME=${SONNET_GAME}: ${_sonnet_game_project} does not exist")
  endif()
  file(READ "${_sonnet_game_project}" _sonnet_game_json)
  string(JSON SONNET_GAME_MODULE ERROR_VARIABLE _sonnet_game_error GET "${_sonnet_game_json}" module)
  if(_sonnet_game_error OR NOT SONNET_GAME_MODULE MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    message(FATAL_ERROR "SONNET_GAME=${SONNET_GAME}: project.json needs a \"module\", a C identifier "
                        "(${_sonnet_game_error}${SONNET_GAME_MODULE})")
  endif()
  if(NOT EXISTS "${SONNET_GAME_DIR}/native/CMakeLists.txt")
    message(FATAL_ERROR "SONNET_GAME=${SONNET_GAME}: the module's native/CMakeLists.txt does not exist")
  endif()
  # A reconfigure picks up a changed project.json.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sonnet_game_project}")
  add_subdirectory("${SONNET_GAME_DIR}/native" "${CMAKE_BINARY_DIR}/game")
  get_property(_sonnet_game_target GLOBAL PROPERTY SONNET_GAME_TARGET)
  if(NOT _sonnet_game_target)
    message(FATAL_ERROR "SONNET_GAME=${SONNET_GAME}: native/CMakeLists.txt did not call sonnet_add_game_module")
  endif()
  set(SONNET_GAME_DECLARATION "void registerGame(sonnet::runtime::GameRegistry &registry);\n")
  set(SONNET_GAME_BODY
      "  static constexpr runtime::GameModule module{.name = \"${SONNET_GAME_MODULE}\", .registerGame = &::registerGame};\n  return &module;")
  message(STATUS "Sonnet game module \"${SONNET_GAME_MODULE}\" from ${SONNET_GAME_DIR}")
else()
  set(SONNET_GAME_DECLARATION "")
  set(SONNET_GAME_BODY "  return nullptr;")
endif()
configure_file("${CMAKE_SOURCE_DIR}/cmake/LinkedGame.h.in" "${SONNET_GAME_INCLUDE_DIR}/sonnet/game/LinkedGame.h" @ONLY)

if(SONNET_GAME AND SONNET_BUILD_TESTS AND SONNET_BUILD_EDITOR)
  add_subdirectory("${CMAKE_SOURCE_DIR}/tests/game" "${CMAKE_BINARY_DIR}/tests/game")
endif()
