cmake_minimum_required(VERSION 3.18)

add_library(SonnetScene STATIC
    src/Transform.cpp
    src/Scene.cpp
)

target_include_directories(SonnetScene PUBLIC include)

sonnet_set_compile_options(SonnetScene)
