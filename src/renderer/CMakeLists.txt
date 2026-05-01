add_library(SonnetRenderer STATIC
    src/Renderer.cpp
)

target_include_directories(SonnetRenderer PUBLIC include)

target_link_libraries(SonnetRenderer
    PUBLIC
        SonnetWindow
    PRIVATE
        SonnetLogging
)

sonnet_set_compile_options(SonnetRenderer)
