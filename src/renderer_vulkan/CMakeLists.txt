add_library(SonnetRendererVulkan STATIC
    src/VulkanBackend.cpp
)

target_include_directories(SonnetRendererVulkan PUBLIC include)

# Include the bundled Vulkan-Headers BEFORE the system paths so that the
# vulkan.h version matches what vulkan.hpp was built against (VK_HEADER_VERSION).
# Without this, environments where VULKAN_SDK is not set (e.g. CLion) pick up
# the distro vulkan.h (which may be an older version) and get an assertion failure.
target_include_directories(SonnetRendererVulkan SYSTEM PRIVATE
    ${vulkanhpp_SOURCE_DIR}/Vulkan-Headers/include
    ${vulkanhpp_SOURCE_DIR}
)

target_link_libraries(SonnetRendererVulkan
    PUBLIC
        SonnetRenderer
    PRIVATE
        Vulkan::Vulkan
        SonnetLogging
)

if(SONNET_ENABLE_VALIDATION_LAYERS)
    target_compile_definitions(SonnetRendererVulkan PRIVATE SONNET_VALIDATION_LAYERS_ENABLED)
endif()

sonnet_set_compile_options(SonnetRendererVulkan)

compile_shader(SonnetRendererVulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/triangle.vert)
compile_shader(SonnetRendererVulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/triangle.frag)
