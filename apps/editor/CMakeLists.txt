sonnet_add_executable(editor
  SOURCES
    main.cpp
  DEPENDS
    sonnet::rhi
)
sonnet_add_shaders(sonnet_editor SHADERS shaders/triangle.slang)
