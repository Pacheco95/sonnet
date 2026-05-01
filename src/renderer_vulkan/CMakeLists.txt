add_library(SonnetRendererVulkan STATIC
    src/VulkanBackend.cpp
)

target_include_directories(SonnetRendererVulkan PUBLIC include)

target_include_directories(SonnetRendererVulkan SYSTEM PRIVATE ${vulkanhpp_SOURCE_DIR})

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
