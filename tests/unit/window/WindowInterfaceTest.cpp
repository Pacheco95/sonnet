#include <catch2/catch_test_macros.hpp>

#include <sonnet/window/IWindow.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {

class MockWindow : public sonnet::window::IWindow {
public:
    bool init() override {
        m_initialized = true;
        m_shouldClose = false;
        return true;
    }

    void shutdown() override { m_initialized = false; }

    [[nodiscard]] bool shouldClose() const override { return m_shouldClose; }

    [[nodiscard]] std::pair<int, int> getSize() const override {
        return {800, 600}; // NOLINT(readability-magic-numbers)
    }

    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }

    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }

    void requestClose() { m_shouldClose = true; }
    [[nodiscard]] bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized{false};
    bool m_shouldClose{false};
};

} // namespace

TEST_CASE("IWindow_InitSucceeds_ReturnsTrue") {
    MockWindow win;
    REQUIRE(win.init() == true);
}

TEST_CASE("IWindow_ShouldClose_FalseAfterInit") {
    MockWindow win;
    win.init();
    REQUIRE(win.shouldClose() == false);
}

TEST_CASE("IWindow_ShouldClose_TrueAfterClose") {
    MockWindow win;
    win.init();
    win.requestClose();
    REQUIRE(win.shouldClose() == true);
}

TEST_CASE("IWindow_GetSize_Returns800x600") {
    MockWindow win;
    win.init();
    auto [width, height] = win.getSize();
    REQUIRE(width == 800);
    REQUIRE(height == 600);
}

TEST_CASE("IWindow_Shutdown_IsIdempotent") {
    MockWindow win;
    win.init();
    REQUIRE_NOTHROW(win.shutdown());
    REQUIRE_NOTHROW(win.shutdown());
}
