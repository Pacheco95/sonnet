# Executables. Sample projects under samples/ are data, not targets; they are copied next to the
# binaries when SONNET_BUILD_SAMPLES is on.
if(SONNET_BUILD_EDITOR)
  add_subdirectory(editor)
endif()
add_subdirectory(samples)
