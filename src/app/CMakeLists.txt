add_executable(Sonnet main.cpp)

target_link_libraries(Sonnet
    PRIVATE
        SonnetRenderer
        SonnetRendererVulkan
        SonnetWindow
        SonnetLogging
        SDL3::SDL3
)

sonnet_set_compile_options(Sonnet)
