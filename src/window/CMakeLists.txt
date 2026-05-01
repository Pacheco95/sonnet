add_library(SonnetWindow STATIC
    src/SDLWindow.cpp
    src/WindowFactory.cpp
)

target_include_directories(SonnetWindow PUBLIC include)

target_link_libraries(SonnetWindow
    PRIVATE
        SDL3::SDL3
        SonnetLogging
)

sonnet_set_compile_options(SonnetWindow)
