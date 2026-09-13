sonnet_add_executable(editor
  SOURCES
    main.cpp
  DEPENDS
    sonnet::editor
)
sonnet_add_engine_shaders(sonnet_editor_app)
