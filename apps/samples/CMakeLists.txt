# Sample projects are data (docs/architecture.md): copied next to the editor binary so
# `sonnet_editor samples/basic` works from the build directory.
if(SONNET_BUILD_SAMPLES AND TARGET sonnet_editor_app)
  add_custom_target(sonnet_samples ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}" "$<TARGET_FILE_DIR:sonnet_editor_app>/samples"
    COMMENT "Copying sample projects next to the editor"
    VERBATIM)
endif()
