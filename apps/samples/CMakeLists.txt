# Sample projects are data (docs/architecture.md): copied next to the binaries that open them so
# `sonnet_editor samples/basic` and `sonnet_player samples/basic` work from the build directory.
if(SONNET_BUILD_SAMPLES)
  set(_sonnet_sample_targets)
  foreach(_app sonnet_editor_app sonnet_player_app)
    if(TARGET ${_app})
      list(APPEND _sonnet_sample_targets COMMAND ${CMAKE_COMMAND} -E copy_directory
           "${CMAKE_CURRENT_SOURCE_DIR}" "$<TARGET_FILE_DIR:${_app}>/samples")
    endif()
  endforeach()
  if(_sonnet_sample_targets)
    add_custom_target(sonnet_samples ALL
      ${_sonnet_sample_targets}
      COMMENT "Copying sample projects next to the binaries"
      VERBATIM)
  endif()
endif()
