#include <catch2/catch_test_macros.hpp>

#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/IRenderer.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/RendererFactory.hpp>
#include <sonnet/window/IWindow.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class MockWindow : public sonnet::window::IWindow {
public:
    bool init() override {
        m_initialized = true;
        return true;
    }
    void shutdown() override { m_initialized = false; }
    [[nodiscard]] bool shouldClose() const override { return m_closeRequested; }
    [[nodiscard]] std::pair<int, int> getSize() const override {
        return {800, 600}; // NOLINT(readability-magic-numbers)
    }
    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }
    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }

    void requestClose() { m_closeRequested = true; }
    [[nodiscard]] bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized{false};
    bool m_closeRequested{false};
};

class MockBackend : public sonnet::renderer::IRendererBackend {
public:
    bool init(sonnet::window::IWindow& window) override {
        m_window = &window;
        m_initCalled = true;
        return true;
    }
    void shutdown() override { m_shutdownCalled = true; }
    void beginFrame() override {}
    void drawPrimitive() override {}
    void endFrame() override { m_frameCount++; }

    sonnet::window::IWindow* m_window{nullptr};
    bool m_initCalled{false};
    bool m_shutdownCalled{false};
    uint64_t m_frameCount{0};
};

} // namespace

TEST_CASE("AppLifecycle_InitShutdown_NoLeaks") {
    sonnet::logging::init();
    MockWindow window;
    REQUIRE(window.init());

    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));

    REQUIRE(renderer->init(window));
    renderer->shutdown();
    window.shutdown();
    REQUIRE_FALSE(window.isInitialized());
}

TEST_CASE("AppLifecycle_RendererInit_InjectsWindow") {
    sonnet::logging::init();
    MockWindow window;
    window.init();

    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));

    renderer->init(window);
    REQUIRE(rawBackend->m_window == &window);
    renderer->shutdown();
}

TEST_CASE("AppLifecycle_RenderLoop_FrameCountIncrements") {
    sonnet::logging::init();
    MockWindow window;
    window.init();

    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));

    renderer->init(window);
    renderer->renderFrame();
    renderer->renderFrame();
    renderer->renderFrame();

    REQUIRE(rawBackend->m_frameCount == 3);
    renderer->shutdown();
}

TEST_CASE("AppLifecycle_WindowCloseRequest_PropagatesCorrectly") {
    MockWindow window;
    window.init();
    REQUIRE_FALSE(window.shouldClose());

    window.requestClose();
    REQUIRE(window.shouldClose());
}
