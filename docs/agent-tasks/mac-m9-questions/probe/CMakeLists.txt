cmake_minimum_required(VERSION 3.28)
project(vkprobe C CXX)

# SDL3 and the Vulkan headers come from vcpkg (this directory's manifest). On Apple platforms
# MoltenVK is the static library from Khronos's release archive, linked into the executable the
# way ADR-0018 proposes for the player; elsewhere the system loader is linked, which also puts
# vkGetInstanceProcAddr in the process.
find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)

add_executable(vkprobe vkprobe.c)
set_target_properties(vkprobe PROPERTIES C_STANDARD 11 LINKER_LANGUAGE CXX)
target_link_libraries(vkprobe PRIVATE SDL3::SDL3 Vulkan::Headers)

if(APPLE)
  set(MOLTENVK_DIR "" CACHE PATH "The MoltenVK/ directory extracted from a Khronos MoltenVK release archive")
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(slice ios-arm64)
  else()
    set(slice macos-arm64_x86_64)
  endif()
  set(moltenvk "${MOLTENVK_DIR}/MoltenVK/static/MoltenVK.xcframework/${slice}/libMoltenVK.a")
  if(NOT EXISTS "${moltenvk}")
    message(FATAL_ERROR "no static MoltenVK at ${moltenvk}; set MOLTENVK_DIR")
  endif()
  # Nothing references vkGetInstanceProcAddr at link time; without -u the linker drops MoltenVK.
  target_link_libraries(vkprobe PRIVATE "${moltenvk}" "-Wl,-u,_vkGetInstanceProcAddr"
    "-framework Metal" "-framework Foundation" "-framework QuartzCore" "-framework IOSurface"
    "-framework CoreGraphics")
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_link_libraries(vkprobe PRIVATE "-framework UIKit")
    set(VKPROBE_BUNDLE_ID "com.example.sonnet.vkprobe" CACHE STRING "Bundle identifier; must be unique to the signing team")
    set_target_properties(vkprobe PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_GUI_IDENTIFIER "${VKPROBE_BUNDLE_ID}"
      MACOSX_BUNDLE_BUNDLE_NAME vkprobe
      MACOSX_BUNDLE_SHORT_VERSION_STRING 1.0
      MACOSX_BUNDLE_BUNDLE_VERSION 1
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VKPROBE_BUNDLE_ID}"
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
      XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "$ENV{SONNET_DEVELOPMENT_TEAM}"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
  else()
    target_link_libraries(vkprobe PRIVATE "-framework IOKit" "-framework AppKit")
  endif()
else()
  find_package(Vulkan REQUIRED)
  target_link_libraries(vkprobe PRIVATE Vulkan::Vulkan)
endif()
