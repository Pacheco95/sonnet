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
# The engine shaders and a cooked .sbundle (BUNDLE, or the SONNET_IOS_BUNDLE cache variable) are
# copied into the bundle's Resources after the build, mirroring how sonnet_add_apk stages an
# APK's assets: neither is known to exist at configure time, so they cannot be given to Xcode as
# ordinary bundle resources. Platform::basePath() already looks in Resources/ on iOS.
#
# Signing team: CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM (docs/build.md, "iOS"), set at configure
# time from the environment or a CMakeUserPresets.json, is a CMake global that every Xcode target
# in the project picks up on its own; nothing here needs to read it. Without one the bundle still
# configures and builds, but Xcode has no team to sign it for a device, which is what CI's
# CODE_SIGNING_ALLOWED=NO build is for instead of a device install.
set(SONNET_IOS_BUNDLE "" CACHE FILEPATH "Cooked .sbundle to ship in the iOS app bundle's Resources/game.sbundle (optional)")
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
  # The shaders sonnet_add_engine_shaders compiles for the target (cmake/SonnetShaders.cmake).
  get_target_property(shaders ${TARGET}_shaders OUTPUTS)
  set(resources_dir "$<TARGET_BUNDLE_CONTENT_DIR:${TARGET}>/Resources")
  set(copy_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${resources_dir}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${shaders} "${resources_dir}/shaders/")
  if(bundle)
    get_filename_component(bundle "${bundle}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
    list(APPEND copy_commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bundle}" "${resources_dir}/game.sbundle")
  endif()
  # No VERBATIM: for the Xcode generator, TARGET_BUNDLE_CONTENT_DIR expands to a path built from
  # Xcode's own build settings (${EFFECTIVE_PLATFORM_NAME} and friends), meant to be substituted
  # by the shell when Xcode runs this as a script-phase shell script. VERBATIM escapes the '$' so
  # that never happens, and the resources are copied next to a directory literally named
  # "Debug${EFFECTIVE_PLATFORM_NAME}" instead of "Debug-iphoneos" (found by a Mac run of
  # docs/agent-tasks/m10-mac-checks.md, section 2).
  add_custom_command(TARGET ${TARGET} POST_BUILD
    ${copy_commands}
    COMMENT "Copying ${name}'s resources into the app bundle$<$<BOOL:${bundle}>: with ${bundle}>")
endfunction()
