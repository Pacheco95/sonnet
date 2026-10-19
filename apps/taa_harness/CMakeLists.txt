# The TAA checks of ADR-0033 (docs/rendering.md, "TAA checks"): renders a project's cases under TAA
# and as 16-sample references on a real GPU and judges the difference. Needs the engine shaders
# beside it and a Vulkan device, so it is a tool to run on the machine that measures, not a test.
sonnet_add_executable(taa_harness
  SOURCES
    main.cpp
  DEPENDS
    sonnet::runtime
)
sonnet_add_engine_shaders(sonnet_taa_harness_app)
