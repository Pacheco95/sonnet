find_package(VulkanHeaders CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

sonnet_add_module(rhi
  SOURCES
    src/Device.cpp
    src/NullDevice.cpp
    src/VmaImpl.cpp
    src/VulkanCommandList.cpp
    src/VulkanDevice.cpp
    src/VulkanSwapchain.cpp
    src/VulkanTypes.cpp
  PUBLIC_DEPENDS
    sonnet::platform
  DEPENDS
    Vulkan::Headers
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
)

# Vulkan is reached through the loader SDL3 loaded, never through static prototypes; Vulkan-HPP
# then dispatches through per-object dispatchers and no default dispatcher storage is needed.
# The definitions live on an interface target because the ui module, the one documented
# exception to the backend rule (docs/architecture.md), compiles the same implementation headers
# for Dear ImGui's Vulkan backend and must see identical Vulkan-HPP configuration.
add_library(sonnet_rhi_vulkan INTERFACE)
add_library(sonnet::rhi_vulkan ALIAS sonnet_rhi_vulkan)
target_include_directories(sonnet_rhi_vulkan INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(sonnet_rhi_vulkan INTERFACE
  VK_NO_PROTOTYPES
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0
)
target_link_libraries(sonnet_rhi_vulkan INTERFACE
  Vulkan::Headers
  GPUOpen::VulkanMemoryAllocator
  unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
)
target_link_libraries(sonnet_rhi PRIVATE sonnet::rhi_vulkan)
target_compile_definitions(sonnet_rhi PRIVATE SONNET_RHI_VULKAN=1)
target_compile_definitions(sonnet_rhi PUBLIC
  $<IF:$<AND:$<BOOL:${SONNET_ENABLE_VALIDATION}>,${SONNET_DEBUG_CONFIG}>,SONNET_ENABLE_VALIDATION=1,SONNET_ENABLE_VALIDATION=0>
)
# Vulkan-HPP is the bulk of every rhi translation unit's parse time.
target_precompile_headers(sonnet_rhi PRIVATE <vulkan/vulkan_raii.hpp>)

sonnet_add_module_test(rhi
  SOURCES
    tests/DeviceTests.cpp
    tests/IdImageTests.cpp
    tests/MeshTests.cpp
    tests/NullDeviceTests.cpp
    tests/ResourceTests.cpp
    tests/SwapchainTests.cpp
    tests/TriangleTests.cpp
)
if(TARGET rhi_tests)
  sonnet_add_shaders(rhi_tests SHADERS
    tests/shaders/triangle.slang tests/shaders/mesh.slang tests/shaders/id.slang tests/shaders/readid.slang)
endif()
