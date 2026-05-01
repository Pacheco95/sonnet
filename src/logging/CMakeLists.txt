add_library(SonnetLogging STATIC
    src/Logger.cpp
)

target_include_directories(SonnetLogging PUBLIC include)

string(TOUPPER "${SONNET_LOG_LEVEL}" _LOG_LEVEL_UPPER)
target_compile_definitions(SonnetLogging PUBLIC
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_${_LOG_LEVEL_UPPER}
)

target_link_libraries(SonnetLogging PUBLIC spdlog::spdlog)

sonnet_set_compile_options(SonnetLogging)
