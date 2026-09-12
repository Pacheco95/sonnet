find_package(VulkanHeaders CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

sonnet_add_module(rhi
  SOURCES
    src/Device.cpp
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
target_compile_definitions(sonnet_rhi PRIVATE
  VK_NO_PROTOTYPES
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
  VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0
  SONNET_RHI_VULKAN=1
)
target_compile_definitions(sonnet_rhi PUBLIC
  $<IF:$<AND:$<BOOL:${SONNET_ENABLE_VALIDATION}>,${SONNET_DEBUG_CONFIG}>,SONNET_ENABLE_VALIDATION=1,SONNET_ENABLE_VALIDATION=0>
)
# Vulkan-HPP is the bulk of every rhi translation unit's parse time.
target_precompile_headers(sonnet_rhi PRIVATE <vulkan/vulkan_raii.hpp>)

sonnet_add_module_test(rhi
  SOURCES
    tests/DeviceTests.cpp
    tests/ResourceTests.cpp
    tests/SwapchainTests.cpp
)
