# The x64-linux triplet with Jolt built under the thread sanitizer, for the linux-tsan preset
# (docs/build.md, "Presets"). Jolt orders its temp allocator and its jobs inside the library, so a
# prebuilt, uninstrumented libJolt.a hides that ordering from the sanitizer and every report through
# it had to be suppressed wholesale. Every other port is built exactly as x64-linux builds it.
#
# The flags only instrument; the compiler is whatever the configure environment's CC and CXX name,
# which vcpkg passes through on Linux. It has to be the preset's own (Clang 20 on the development
# machine), or the library's instrumentation and the test binary's runtime disagree.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "joltphysics")
  set(VCPKG_C_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
  set(VCPKG_CXX_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
  set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
endif()
