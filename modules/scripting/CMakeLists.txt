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
# sol2 calls every bound function through a pointer it takes from Lua userdata without a null check
# (function_detail::call), so GCC 14's -Wnull-dereference flags whichever bindings its inlining
# reaches, differently at -O2 and -O3. Nothing in this file can show GCC the pointer is set (#36).
set_source_files_properties(src/LuaScriptRuntime.cpp PROPERTIES
  COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-null-dereference>")

sonnet_add_module_test(scripting
  SOURCES
    tests/ScriptingTests.cpp
)
if(TARGET scripting_tests)
  sonnet_add_engine_shaders(scripting_tests)
endif()
