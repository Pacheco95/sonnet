cmake_minimum_required(VERSION 3.18)

add_library(SonnetPrimitives STATIC
    src/MeshPrimitives.cpp
)

target_include_directories(SonnetPrimitives PUBLIC include)

target_link_libraries(SonnetPrimitives
    PUBLIC
        SonnetRenderer
)

sonnet_set_compile_options(SonnetPrimitives)
