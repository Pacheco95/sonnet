sonnet_add_module(gui
  SOURCES
    src/Components.cpp
    src/GuiSystem.cpp
    src/Layout.cpp
  PUBLIC_DEPENDS
    sonnet::world
)

sonnet_add_module_test(gui
  SOURCES
    tests/GuiTests.cpp
)
