#include <catch2/catch_test_macros.hpp>

#include <sonnet/logging/Logger.hpp>

TEST_CASE("Logger_Init_DoesNotThrow") {
    REQUIRE_NOTHROW(sonnet::logging::init());
}

TEST_CASE("Logger_DoubleInit_IsIdempotent") {
    REQUIRE_NOTHROW(sonnet::logging::init());
    REQUIRE_NOTHROW(sonnet::logging::init());
}

TEST_CASE("Logger_MacroInfo_DoesNotThrow") {
    sonnet::logging::init();
    REQUIRE_NOTHROW(SONNET_LOG_INFO("test info message"));
}

TEST_CASE("Logger_MacroError_DoesNotThrow") {
    sonnet::logging::init();
    REQUIRE_NOTHROW(SONNET_LOG_ERROR("test error message"));
}
