find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

sonnet_add_module(platform
  SOURCES
    src/Platform.cpp
    src/SdlEntryPoint.cpp
    src/SdlEvents.cpp
    src/SdlWindow.cpp
  PUBLIC_DEPENDS
    sonnet::core
    # SDL3 is public only because EntryPoint.h has to include SDL_main.h in the executable's
    # translation unit; no other public header includes SDL. Vulkan::Headers provides the
    # surface handle types in IWindow.
    SDL3::SDL3
    Vulkan::Headers
)

sonnet_add_module_test(platform
  SOURCES
    tests/EventTests.cpp
    tests/PlatformTests.cpp
    tests/WindowTests.cpp
  DEPENDS
    SDL3::SDL3
)
