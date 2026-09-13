find_package(imgui CONFIG REQUIRED)

sonnet_add_module(ui
  SOURCES
    src/ImGuiLayer.cpp
  PUBLIC_DEPENDS
    sonnet::rhi
    sonnet::platform
    imgui::imgui
  DEPENDS
    # The documented exception (docs/architecture.md): Dear ImGui's Vulkan backend needs the
    # implementation headers of rhi.
    sonnet::rhi_vulkan
)

sonnet_add_module_test(ui
  SOURCES
    tests/ImGuiLayerTests.cpp
)
