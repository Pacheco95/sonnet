# sonnet_add_ios_bundle(<target> INFO_PLIST <file> LAUNCH_STORYBOARD <file> [BUNDLE <file.sbundle>])
#
# Turns the executable <target> into an iOS app bundle, signed by Xcode's own automatic signing
# (ADR-0018, "Packaging"): the Xcode generator both builds and installs the bundle, which is what
# lets `devicectl` and `xcodebuild -allowProvisioningUpdates` put it on a device.
#
# INFO_PLIST is configured with @ONLY: @PROJECT_VERSION@ and @SONNET_IOS_LAUNCH_STORYBOARD_NAME@
# (LAUNCH_STORYBOARD's own file name) are substituted by CMake, everything else the plist itself
# references as $(...) is an Xcode build setting substituted when Xcode compiles it, which is how
# $(PRODUCT_BUNDLE_IDENTIFIER) picks up XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER below. The
# result is set as the bundle's Info.plist. LAUNCH_STORYBOARD is added as a bundle resource with
# MACOSX_PACKAGE_LOCATION, which the Xcode generator recognises and compiles with `ibtool` on its
# own, the same as it would inside an Xcode project.
#
# A cooked .sbundle (BUNDLE, or the SONNET_IOS_BUNDLE cache variable) is copied into the bundle's
# root after the build: it is not known to exist at configure time, so it cannot be given to
# Xcode as an ordinary bundle resource the way LAUNCH_STORYBOARD is. The engine shaders need no
# such step: sonnet_add_engine_shaders already compiles them straight to $<TARGET_FILE_DIR:tgt>,
# which is the bundle root here. iOS bundles are flat (no Contents/Resources, unlike macOS), which
# is also where Platform::basePath() looks through SDL.
#
# Signing team: CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM (docs/build.md, "iOS"), set at configure
# time from the environment or a CMakeUserPresets.json, is a CMake global that every Xcode target
# in the project picks up on its own; nothing here needs to read it. Without one the bundle still
# configures and builds, but Xcode has no team to sign it for a device, which is what CI's
# CODE_SIGNING_ALLOWED=NO build is for instead of a device install.
set(SONNET_IOS_BUNDLE "" CACHE FILEPATH "Cooked .sbundle to ship in the iOS app bundle as game.sbundle (optional)")
set(SONNET_IOS_BUNDLE_IDENTIFIER "io.github.pacheco95.sonnet" CACHE STRING "iOS app bundle identifier")

function(sonnet_add_ios_bundle TARGET)
  cmake_parse_arguments(ARG "" "INFO_PLIST;LAUNCH_STORYBOARD;BUNDLE" "" ${ARGN})
  get_target_property(name ${TARGET} OUTPUT_NAME)

  get_filename_component(info_plist "${ARG_INFO_PLIST}" ABSOLUTE)
  get_filename_component(storyboard "${ARG_LAUNCH_STORYBOARD}" ABSOLUTE)
  # @SONNET_IOS_LAUNCH_STORYBOARD_NAME@ in INFO_PLIST becomes this, the file's own name, which is
  # baked in rather than read back from Xcode's $(...) substitution, since it does not change
  # between the app's Debug and Release configurations.
  get_filename_component(SONNET_IOS_LAUNCH_STORYBOARD_NAME "${storyboard}" NAME_WE)
  set(configured_plist "${CMAKE_CURRENT_BINARY_DIR}/${name}-Info.plist")
  configure_file("${info_plist}" "${configured_plist}" @ONLY)

  target_sources(${TARGET} PRIVATE "${storyboard}")
  set_source_files_properties("${storyboard}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

  set_target_properties(${TARGET} PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST "${configured_plist}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${SONNET_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "NO")

  set(bundle "${ARG_BUNDLE}")
  if(NOT bundle)
    set(bundle "${SONNET_IOS_BUNDLE}")
  endif()
  if(bundle)
    get_filename_component(bundle "${bundle}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
    # Not $<TARGET_BUNDLE_CONTENT_DIR:tgt> or $<TARGET_FILE_DIR:tgt>: under the Xcode generator, a
    # POST_BUILD custom command's genexp resolves to a path built from Xcode's own build settings
    # (${EFFECTIVE_PLATFORM_NAME} and friends), meant to be substituted by the shell running the
    # generated script phase, but it never was, and the bundle was copied next to a directory
    # literally named "Debug${EFFECTIVE_PLATFORM_NAME}" instead of "Debug-iphoneos" (found by a
    # Mac run of docs/agent-tasks/m10-mac-checks.md, section 2). This project never targets the
    # simulator (ports/moltenvk has no simulator slice), so the suffix is always "-iphoneos": built
    # from $<CONFIG>, which CMake itself resolves, and CMAKE_CURRENT_BINARY_DIR, both known without
    # Xcode's help.
    add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bundle}"
              "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>-iphoneos/${name}.app/game.sbundle"
      COMMENT "Copying ${bundle} into the app bundle")
  endif()
endfunction()
