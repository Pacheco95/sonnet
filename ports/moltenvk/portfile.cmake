# Khronos publishes MoltenVK as prebuilt release archives, not a source tree with a CMake build
# (ADR-0018, "There is no moltenvk port"): the static library in each platform's xcframework slice
# is installed as-is. One release-only configuration, since Khronos ships no separate debug build.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

if(VCPKG_TARGET_IS_IOS)
  set(SONNET_MVK_SLICE "ios-arm64")
  vcpkg_download_distfile(SONNET_MVK_ARCHIVE
    URLS "https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-ios.tar"
    FILENAME "MoltenVK-ios-${VERSION}.tar"
    SHA512 eb98821ee08cf3e3429a93fb85f5aaeaa2de3ee2eb02a06fd1f8b09f9b31958f0d6b0d07162e1f44f19a2c846ba26bbbdb090fab6c97cc7ce7b7887e8c1d1086
  )
elseif(VCPKG_TARGET_IS_OSX)
  set(SONNET_MVK_SLICE "macos-arm64_x86_64")
  vcpkg_download_distfile(SONNET_MVK_ARCHIVE
    URLS "https://github.com/KhronosGroup/MoltenVK/releases/download/v${VERSION}/MoltenVK-macos.tar"
    FILENAME "MoltenVK-macos-${VERSION}.tar"
    SHA512 985f483e832ae3605b62b78798989f79c8833c13b568ff3615fb9c1f2780c3e9960cb13f79f2560b01563694249520d23d4f93d284580c13f6c842eff0c6b99d
  )
else()
  message(FATAL_ERROR "moltenvk only supports arm64-ios and the osx triplets (ADR-0018); got ${VCPKG_TARGET_TRIPLET}")
endif()

vcpkg_extract_source_archive(SONNET_MVK_SOURCE ARCHIVE "${SONNET_MVK_ARCHIVE}")

set(SONNET_MVK_LIB "${SONNET_MVK_SOURCE}/MoltenVK/static/MoltenVK.xcframework/${SONNET_MVK_SLICE}/libMoltenVK.a")
if(NOT EXISTS "${SONNET_MVK_LIB}")
  message(FATAL_ERROR
    "Expected ${SONNET_MVK_LIB} in the release archive; MoltenVK's xcframework layout changed "
    "since this port was written against v${VERSION}.")
endif()

file(INSTALL "${SONNET_MVK_LIB}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/unofficial-moltenvk-config.cmake.in"
  "${CURRENT_PACKAGES_DIR}/share/unofficial-moltenvk/unofficial-moltenvk-config.cmake"
  @ONLY
)

vcpkg_install_copyright(FILE_LIST "${SONNET_MVK_SOURCE}/MoltenVK/LICENSE")
