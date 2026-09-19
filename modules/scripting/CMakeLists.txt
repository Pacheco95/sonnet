find_package(unofficial-lua CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)

sonnet_add_module(scripting
  SOURCES
    src/Components.cpp
    src/LuaReflection.cpp
    src/LuaScriptRuntime.cpp
  PUBLIC_DEPENDS
    sonnet::physics
  DEPENDS
    # Lua compiled as C++, so its errors unwind C++ frames (ADR-0009); sol2 has to know.
    unofficial::lua::lua-cpp
    sol2::sol2
)
target_compile_definitions(sonnet_scripting PRIVATE SOL_USING_CXX_LUA=1 SOL_ALL_SAFETIES_ON=1)

sonnet_add_module_test(scripting
  SOURCES
    tests/ScriptingTests.cpp
)
if(TARGET scripting_tests)
  sonnet_add_engine_shaders(scripting_tests)
endif()
