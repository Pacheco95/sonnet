# The cook tool needs the engine shaders beside it: it builds a Renderer on a null device so the
# importers have somewhere to upload to (docs/assets.md, "Cooking and export").
sonnet_add_executable(cook
  SOURCES
    main.cpp
  DEPENDS
    sonnet::assets
)
sonnet_add_engine_shaders(sonnet_cook_app)
