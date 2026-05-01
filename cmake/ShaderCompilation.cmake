function(compile_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SPV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
        OUTPUT  ${SPV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc ${SHADER_SOURCE} -o ${SPV_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_NAME} -> SPIR-V"
        VERBATIM
    )

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${SPV_OUTPUT}
            "$<TARGET_FILE_DIR:Sonnet>/shaders/${SHADER_NAME}.spv"
        COMMENT "Copying ${SHADER_NAME}.spv to output directory"
        VERBATIM
    )

    target_sources(${TARGET} PRIVATE ${SPV_OUTPUT})
endfunction()
