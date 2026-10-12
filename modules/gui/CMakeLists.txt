find_package(Stb REQUIRED)

sonnet_add_module(gui
  SOURCES
    src/Atlas.cpp
    src/Components.cpp
    src/Draw.cpp
    src/Font.cpp
    src/FontCache.cpp
    src/GuiSystem.cpp
    src/Layout.cpp
    src/Text.cpp
  PUBLIC_DEPENDS
    sonnet::world
)
target_include_directories(sonnet_gui SYSTEM PRIVATE ${Stb_INCLUDE_DIR})

sonnet_add_module_test(gui
  SOURCES
    tests/GuiTests.cpp
    tests/TextTests.cpp
)
if(TARGET gui_tests)
  sonnet_add_engine_shaders(gui_tests)
endif()
