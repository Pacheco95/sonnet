cmake_minimum_required(VERSION 3.18)

# --- Dear ImGui static library (PRIVATE to SonnetEditor) ---
FetchContent_GetProperties(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${vulkanhpp_SOURCE_DIR}/Vulkan-Headers/include
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

# --- SonnetEditor static library ---
add_library(SonnetEditor STATIC
    src/Editor.cpp
    src/EditorFactory.cpp
    src/LayoutManager.cpp
    src/log/EditorLogSink.cpp
    src/panels/ViewportPanel.cpp
    src/panels/SceneHierarchyPanel.cpp
    src/panels/InspectorPanel.cpp
    src/panels/LogPanel.cpp
)

target_include_directories(SonnetEditor PUBLIC include)

target_link_libraries(SonnetEditor
    PUBLIC
        SonnetRenderer
        SonnetWindow
        SonnetLogging
    PRIVATE
        imgui
        SDL3::SDL3
)

target_include_directories(SonnetEditor SYSTEM PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${vulkanhpp_SOURCE_DIR}/Vulkan-Headers/include
    ${vulkanhpp_SOURCE_DIR}
)

sonnet_set_compile_options(SonnetEditor)
