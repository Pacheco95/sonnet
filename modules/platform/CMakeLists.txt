find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

# The thermal state is one Foundation call on Apple platforms (docs/platform.md, "Thermal state"),
# so those take the Objective-C++ file and everyone else the portable one.
if(APPLE)
  enable_language(OBJCXX)
  set(SONNET_THERMAL_SOURCE src/ThermalApple.mm)
else()
  set(SONNET_THERMAL_SOURCE src/Thermal.cpp)
endif()

sonnet_add_module(platform
  SOURCES
    src/Content.cpp
    src/Input.cpp
    src/InputState.cpp
    src/Platform.cpp
    src/SdlDisplay.cpp
    src/SdlEntryPoint.cpp
    src/SdlEvents.cpp
    src/SdlWindow.cpp
    ${SONNET_THERMAL_SOURCE}
  PUBLIC_DEPENDS
    sonnet::core
    # SDL3 is public only because EntryPoint.h has to include SDL_main.h in the executable's
    # translation unit; no other public header includes SDL. Vulkan::Headers provides the
    # surface handle types in IWindow.
    SDL3::SDL3
    Vulkan::Headers
)
if(ANDROID)
  # spdlog's logcat sink calls __android_log_write (docs/platform.md, "Logging"), and the window
  # asks for its frame rate through libnativewindow (docs/platform.md, "Window"); the thermal
  # status is libandroid's.
  target_link_libraries(sonnet_platform PRIVATE log nativewindow android)
endif()
if(APPLE)
  target_link_libraries(sonnet_platform PRIVATE "-framework Foundation")
endif()

sonnet_add_module_test(platform
  SOURCES
    tests/ContentTests.cpp
    tests/EventTests.cpp
    tests/InputStateTests.cpp
    tests/PlatformTests.cpp
    tests/ThermalTests.cpp
    tests/WindowTests.cpp
  DEPENDS
    SDL3::SDL3
)
