# The generic runtime an exported game ships as (docs/player.md). It links no editor code and,
# in release, no Dear ImGui; the engine shaders go next to it and export copies them along.
sonnet_add_executable(player
  SOURCES
    main.cpp
  DEPENDS
    sonnet::runtime
)
sonnet_add_engine_shaders(sonnet_player_app)

if(ANDROID)
  # build/android-*/apps/player/sonnet_player.apk, built by sonnet_player_apk (docs/player.md).
  include(SonnetAndroid)
  sonnet_add_apk(sonnet_player_app
    MANIFEST android/AndroidManifest.xml
    RESOURCES android/res
    JAVA_SOURCES android/SonnetActivity.java
  )
endif()

if(APPLE)
  # Only the player links the static MoltenVK: the editor and every test binary keep loading the
  # Vulkan SDK's loader through SDL, so they keep validation (ADR-0018, "There is no moltenvk
  # port"). This is also what closes "the macOS export needs the Vulkan SDK"
  # (docs/reports/roadmap-closed-work.md#the-macos-export-needs-the-vulkan-sdk): SDL finds vkGetInstanceProcAddr in
  # the process before it searches the machine for a loader.
  find_package(unofficial-moltenvk CONFIG REQUIRED)
  target_link_libraries(sonnet_player_app PRIVATE unofficial::moltenvk::moltenvk)
  # Nothing references vkGetInstanceProcAddr at link time, so the linker would otherwise drop
  # MoltenVK's objects from the static archive before SDL's dlsym(RTLD_DEFAULT) can find it.
  target_link_options(sonnet_player_app PRIVATE "-Wl,-u,_vkGetInstanceProcAddr")
endif()

if(IOS)
  # build/ios-*/apps/player/sonnet_player.app, an Xcode-signed bundle (docs/player.md).
  include(SonnetIOS)
  sonnet_add_ios_bundle(sonnet_player_app
    INFO_PLIST ios/Info.plist.in
    LAUNCH_STORYBOARD ios/LaunchScreen.storyboard
  )
endif()

if(SONNET_BUILD_EDITOR)
  # The export dialog looks for the player binary in the editor's directory (docs/editor.md#export).
  add_custom_command(TARGET sonnet_player_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:sonnet_player_app>
      $<TARGET_FILE_DIR:sonnet_editor_app>/$<TARGET_FILE_NAME:sonnet_player_app>
    COMMENT "Copying player binary next to editor for export"
  )
endif()
