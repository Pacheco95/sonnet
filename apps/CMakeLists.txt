# Executables. Sample projects under samples/ are data, not targets; they are copied next to the
# binaries when SONNET_BUILD_SAMPLES is on.
if(SONNET_BUILD_EDITOR)
  add_subdirectory(editor)
  # Desktop tooling: cooking a project is what an export does, and mobile builds only run one.
  add_subdirectory(cook)
endif()
if(SONNET_BUILD_PLAYER)
  add_subdirectory(player)
endif()
add_subdirectory(samples)
