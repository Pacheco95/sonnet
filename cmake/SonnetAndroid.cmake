# sonnet_add_apk(<target> MANIFEST <file> RESOURCES <dir> JAVA_SOURCES <file> ... [BUNDLE <file.sbundle>])
#
# Packages the shared library <target> into a debug-signed APK, <output name>.apk beside it, built
# by the target <output name>_apk (ADR-0018, "Packaging"). No Gradle: the SDK's build tools do each
# step, and the ADR's order is kept:
#
#   1. aapt2 compiles the resources and links them with the manifest against android.jar.
#   2. javac compiles SDL's Java glue (installed by the sdl3 overlay port) and JAVA_SOURCES.
#   3. d8 dexes the classes.
#   4. zip adds classes.dex, then stores lib/arm64-v8a/<library>, assets/shaders/ and, when a
#      bundle is given, assets/game.sbundle uncompressed, so they are read in place. The library is
#      stripped on the way in, as Gradle does; the unstripped one stays in the build directory
#      for symbolising.
#   5. zipalign aligns the entries to 4 bytes and the library to 16 KB pages (-P 16).
#   6. apksigner signs with a debug keystore that keytool generates once under the build directory.
#
# BUNDLE, or the SONNET_ANDROID_BUNDLE cache variable, names a cooked bundle to ship as
# assets/game.sbundle. Without one the APK carries no game.
#
# The tools come from ANDROID_HOME (build-tools/${SONNET_ANDROID_BUILD_TOOLS_VERSION} and
# platforms/android-<ANDROID_PLATFORM_LEVEL>) and from a JDK found through JAVA_HOME or PATH.
# Configuring fails with a message naming whichever is missing.
#
# Adapted from SDL's cmake/android/SdlAndroidFunctions.cmake (SDL 3.4.16), whose licence follows.
# Altered: one function builds the whole APK instead of one per step, no Gradle project is involved,
# the Java sources are compiled here rather than taken from SDL3.jar, entries are stored rather than
# deflated, and zipalign page-aligns the native library.
#
#   Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>
#
#   This software is provided 'as-is', without any express or implied
#   warranty.  In no event will the authors be held liable for any damages
#   arising from the use of this software.
#
#   Permission is granted to anyone to use this software for any purpose,
#   including commercial applications, and to alter it and redistribute it
#   freely, subject to the following restrictions:
#
#   1. The origin of this software must not be misrepresented; you must not
#      claim that you wrote the original software. If you use this software
#      in a product, an acknowledgment in the product documentation would be
#      appreciated but is not required.
#   2. Altered source versions must be plainly marked as such, and must not be
#      misrepresented as being the original software.
#   3. This notice may not be removed or altered from any source distribution.

set(SONNET_ANDROID_BUILD_TOOLS_VERSION "36.1.0" CACHE STRING "Android SDK build-tools version that packages the APK")
set(SONNET_ANDROID_BUNDLE "" CACHE FILEPATH "Cooked .sbundle to ship in the APK as assets/game.sbundle (optional)")

# The SDK. A cache variable wins over the environment, so an IDE can pass it, and the one found in
# the environment is cached, so a regeneration during the build finds it again.
if(NOT ANDROID_HOME)
  if(DEFINED ENV{ANDROID_HOME})
    set(ANDROID_HOME "$ENV{ANDROID_HOME}" CACHE PATH "Android SDK that packages the APK")
  elseif(DEFINED ENV{ANDROID_SDK_ROOT})
    set(ANDROID_HOME "$ENV{ANDROID_SDK_ROOT}" CACHE PATH "Android SDK that packages the APK")
  endif()
endif()
if(NOT ANDROID_HOME OR NOT IS_DIRECTORY "${ANDROID_HOME}")
  message(FATAL_ERROR
    "The APK needs the Android SDK: set ANDROID_HOME to it (docs/build.md, \"Android\"). "
    "ANDROID_HOME is '${ANDROID_HOME}'.")
endif()

set(_sonnet_build_tools "${ANDROID_HOME}/build-tools/${SONNET_ANDROID_BUILD_TOOLS_VERSION}")
# Not find_program: its cached result would outlive a change of SONNET_ANDROID_BUILD_TOOLS_VERSION.
foreach(tool IN ITEMS aapt2 d8 zipalign apksigner)
  string(TOUPPER "${tool}" upper)
  set(SONNET_ANDROID_${upper} "${_sonnet_build_tools}/${tool}")
  if(NOT EXISTS "${SONNET_ANDROID_${upper}}")
    message(FATAL_ERROR
      "The APK needs ${tool} from build-tools ${SONNET_ANDROID_BUILD_TOOLS_VERSION}, which is not in "
      "${_sonnet_build_tools}. Install it with `sdkmanager \"build-tools;${SONNET_ANDROID_BUILD_TOOLS_VERSION}\"` "
      "or set SONNET_ANDROID_BUILD_TOOLS_VERSION to an installed version.")
  endif()
endforeach()
unset(_sonnet_build_tools)

set(SONNET_ANDROID_JAR "${ANDROID_HOME}/platforms/android-${ANDROID_PLATFORM_LEVEL}/android.jar")
if(NOT EXISTS "${SONNET_ANDROID_JAR}")
  message(FATAL_ERROR
    "The APK needs platforms/android-${ANDROID_PLATFORM_LEVEL}, which is not in ${ANDROID_HOME}. "
    "Install it with `sdkmanager \"platforms;android-${ANDROID_PLATFORM_LEVEL}\"`.")
endif()

# The JDK: JAVA_HOME first, then PATH. d8 and apksigner are scripts that run `java` from PATH, so
# the build puts this JDK's bin first for them.
foreach(tool IN ITEMS javac java keytool)
  string(TOUPPER "${tool}" upper)
  find_program(SONNET_${upper} ${tool} HINTS "$ENV{JAVA_HOME}/bin" NO_CMAKE_FIND_ROOT_PATH)
  if(NOT SONNET_${upper})
    message(FATAL_ERROR
      "The APK needs ${tool} from a JDK (17 or later): set JAVA_HOME or put the JDK's bin on PATH "
      "(docs/build.md, \"Android\").")
  endif()
endforeach()
get_filename_component(SONNET_JDK_BIN "${SONNET_JAVAC}" DIRECTORY)

find_program(SONNET_ZIP zip NO_CMAKE_FIND_ROOT_PATH)
if(NOT SONNET_ZIP)
  message(FATAL_ERROR "The APK needs zip, which is not on PATH (on Debian and Ubuntu, `apt install zip`).")
endif()

# SDL's Java glue, from the same sdl3 port as the native library (ports/README.md).
set(SONNET_SDL_JAVA_DIR "${SDL3_DIR}/android-java")
file(GLOB SONNET_SDL_JAVA_SOURCES "${SONNET_SDL_JAVA_DIR}/*.java")
if(NOT SONNET_SDL_JAVA_SOURCES)
  message(FATAL_ERROR
    "SDL's Java sources are not in ${SONNET_SDL_JAVA_DIR}. The sdl3 overlay port installs them for "
    "Android triplets; delete the build directory's vcpkg_installed and configure again.")
endif()

function(sonnet_add_apk TARGET)
  cmake_parse_arguments(ARG "" "MANIFEST;RESOURCES;BUNDLE" "JAVA_SOURCES" ${ARGN})
  get_target_property(name ${TARGET} OUTPUT_NAME)
  set(apk_target ${name}_apk)
  set(work "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${apk_target}.dir")
  set(apk "${CMAKE_CURRENT_BINARY_DIR}/${name}.apk")
  get_filename_component(manifest "${ARG_MANIFEST}" ABSOLUTE)
  get_filename_component(resources "${ARG_RESOURCES}" ABSOLUTE)
  set(java_sources)
  foreach(source IN LISTS ARG_JAVA_SOURCES)
    get_filename_component(source "${source}" ABSOLUTE)
    list(APPEND java_sources "${source}")
  endforeach()
  set(bundle "${ARG_BUNDLE}")
  if(NOT bundle)
    set(bundle "${SONNET_ANDROID_BUNDLE}")
  endif()
  if(bundle)
    get_filename_component(bundle "${bundle}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
  endif()
  # The shaders sonnet_add_engine_shaders compiles for the target (cmake/SonnetShaders.cmake).
  get_target_property(shaders ${TARGET}_shaders OUTPUTS)
  set(run_with_jdk ${CMAKE_COMMAND} -E env --modify "PATH=path_list_prepend:${SONNET_JDK_BIN}" --)

  # Generated once and kept, so a reinstall over an earlier build keeps its signature.
  set(keystore "${CMAKE_BINARY_DIR}/android/debug.keystore")
  add_custom_command(
    OUTPUT "${keystore}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/android"
    COMMAND "${SONNET_KEYTOOL}" -genkeypair -keystore "${keystore}" -storepass android -alias androiddebugkey
            -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
            -noprompt
    COMMENT "keytool: generating the Android debug keystore"
    VERBATIM)

  # 1. Resources and manifest. --debug-mode marks the APK debuggable in Debug and RelWithDebInfo,
  # which `adb shell run-as` needs.
  file(GLOB_RECURSE resource_files CONFIGURE_DEPENDS "${resources}/*")
  add_custom_command(
    OUTPUT "${work}/resources.zip"
    COMMAND "${SONNET_ANDROID_AAPT2}" compile --dir "${resources}" -o "${work}/resources.zip"
    DEPENDS ${resource_files}
    COMMENT "aapt2 compile ${name}"
    VERBATIM)
  add_custom_command(
    OUTPUT "${work}/linked.apk"
    COMMAND "${SONNET_ANDROID_AAPT2}" link -I "${SONNET_ANDROID_JAR}" --manifest "${manifest}"
            "$<${SONNET_DEBUGGABLE_CONFIG}:--debug-mode>" -o "${work}/linked.apk" "${work}/resources.zip"
    DEPENDS "${work}/resources.zip" "${manifest}"
    COMMENT "aapt2 link ${name}"
    COMMAND_EXPAND_LISTS
    VERBATIM)

  # 2 and 3. Java 17 bytecode whichever JDK compiles it, the level d8 and the CI runner's JDK share.
  add_custom_command(
    OUTPUT "${work}/classes.dex"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${work}/classes" "${work}/classes.zip"
    COMMAND "${SONNET_JAVAC}" --release 17 -nowarn -encoding UTF-8 -classpath "${SONNET_ANDROID_JAR}"
            -d "${work}/classes" ${SONNET_SDL_JAVA_SOURCES} ${java_sources}
    COMMAND ${CMAKE_COMMAND} -E chdir "${work}/classes" ${CMAKE_COMMAND} -E tar cf "${work}/classes.zip" --format=zip .
    COMMAND ${run_with_jdk} "${SONNET_ANDROID_D8}" "$<IF:$<CONFIG:Debug>,--debug,--release>"
            --min-api ${ANDROID_PLATFORM_LEVEL} --lib "${SONNET_ANDROID_JAR}" --output "${work}" "${work}/classes.zip"
    DEPENDS ${SONNET_SDL_JAVA_SOURCES} ${java_sources}
    COMMENT "javac and d8 ${name}"
    VERBATIM)

  # 4. The staging directory mirrors the APK's layout; it is rebuilt each time so nothing stale,
  # such as a bundle no longer given, is zipped in.
  set(stage "${work}/stage")
  set(libdir "lib/${ANDROID_ABI}")
  set(stage_commands
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${stage}" "${work}/unaligned.apk"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${stage}/${libdir}" "${stage}/assets/shaders"
    COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${TARGET}>" -o "${stage}/${libdir}/$<TARGET_FILE_NAME:${TARGET}>"
    COMMAND ${CMAKE_COMMAND} -E copy ${shaders} "${stage}/assets/shaders/")
  set(bundle_depends)
  if(bundle)
    list(APPEND stage_commands COMMAND ${CMAKE_COMMAND} -E copy "${bundle}" "${stage}/assets/game.sbundle")
    set(bundle_depends "${bundle}")
  endif()
  add_custom_command(
    OUTPUT "${work}/unaligned.apk"
    ${stage_commands}
    COMMAND ${CMAKE_COMMAND} -E copy "${work}/linked.apk" "${work}/unaligned.apk"
    COMMAND "${SONNET_ZIP}" -q -j "${work}/unaligned.apk" "${work}/classes.dex"
    COMMAND ${CMAKE_COMMAND} -E chdir "${stage}" "${SONNET_ZIP}" -q -0 -r "${work}/unaligned.apk" lib assets
    DEPENDS "${work}/linked.apk" "${work}/classes.dex" ${TARGET} ${TARGET}_shaders ${shaders} ${bundle_depends}
    COMMENT "Packaging ${name}.apk$<$<BOOL:${bundle}>: with ${bundle}>"
    VERBATIM)

  # 5 and 6.
  add_custom_command(
    OUTPUT "${apk}"
    COMMAND "${SONNET_ANDROID_ZIPALIGN}" -f -P 16 4 "${work}/unaligned.apk" "${work}/aligned.apk"
    COMMAND ${run_with_jdk} "${SONNET_ANDROID_APKSIGNER}" sign --ks "${keystore}" --ks-pass pass:android
            --key-pass pass:android --out "${apk}" "${work}/aligned.apk"
    DEPENDS "${work}/unaligned.apk" "${keystore}"
    BYPRODUCTS "${apk}.idsig"
    COMMENT "zipalign and apksigner ${name}.apk"
    VERBATIM)

  add_custom_target(${apk_target} ALL DEPENDS "${apk}")
  set_target_properties(${apk_target} PROPERTIES FOLDER "Apps" OUTPUT "${apk}")
endfunction()
