sonnet_add_module(renderer
  SOURCES
    src/Camera.cpp
    src/Primitives.cpp
    src/RenderGraph.cpp
    src/Renderer.cpp
    src/RenderTarget.cpp
  PUBLIC_DEPENDS
    sonnet::rhi
)

# Engine shaders. sonnet.slang is a module the entry-point files import; only entry-point files
# are compiled, and sonnet_add_engine_shaders(<target>) places the result next to a binary.
set_property(GLOBAL PROPERTY SONNET_ENGINE_SHADERS
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/forward.slang"
)

sonnet_add_module_test(renderer
  SOURCES
    tests/CameraTests.cpp
    tests/PrimitivesTests.cpp
    tests/RenderGraphTests.cpp
    tests/RendererTests.cpp
)
if(TARGET renderer_tests)
  sonnet_add_engine_shaders(renderer_tests)
endif()
