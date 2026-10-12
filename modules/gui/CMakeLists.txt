sonnet_add_module(gui
  SOURCES
    src/Components.cpp
    src/Draw.cpp
    src/GuiSystem.cpp
    src/Layout.cpp
  PUBLIC_DEPENDS
    sonnet::world
)

sonnet_add_module_test(gui
  SOURCES
    tests/GuiTests.cpp
)
if(TARGET gui_tests)
  sonnet_add_engine_shaders(gui_tests)
endif()
