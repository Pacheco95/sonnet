#include <catch2/catch_test_macros.hpp>

#include <sonnet/renderer/IRenderer.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/RendererFactory.hpp>
#include <sonnet/window/IWindow.hpp>

#include <string>
#include <utility>
#include <vector>

class MockWindow : public sonnet::window::IWindow {
public:
    bool init() override { return true; }
    void shutdown() override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] std::pair<int, int> getSize() const override {
        return {800, 600};
    } // NOLINT(readability-magic-numbers)
    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }
    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }
};

class MockBackend : public sonnet::renderer::IRendererBackend {
public:
    bool init(sonnet::window::IWindow& /*window*/) override {
        initCalled = true;
        return true;
    }
    void shutdown() override { shutdownCalled = true; }
    void beginFrame() override { lastCall = "begin"; }
    void drawPrimitive() override { lastCall = "draw"; }
    void endFrame() override { lastCall = "end"; }

    bool initCalled{false};
    bool shutdownCalled{false};
    std::string lastCall;
};

TEST_CASE("IRenderer_InitWithMockBackend_ReturnsTrue") {
    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));
    MockWindow window;
    REQUIRE(renderer->init(window) == true);
    REQUIRE(rawBackend->initCalled == true);
    renderer->shutdown();
}

TEST_CASE("IRenderer_RenderFrame_CallsBackendSequence") {
    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));
    MockWindow window;
    renderer->init(window);

    // After renderFrame, endFrame should be the last call
    renderer->renderFrame();
    REQUIRE(rawBackend->lastCall == "end");
    renderer->shutdown();
}

TEST_CASE("IRenderer_Shutdown_DelegatesToBackend") {
    auto* rawBackend = new MockBackend();
    auto renderer = sonnet::renderer::createRenderer(
        std::unique_ptr<sonnet::renderer::IRendererBackend>(rawBackend));
    MockWindow window;
    renderer->init(window);
    renderer->shutdown();
    REQUIRE(rawBackend->shutdownCalled == true);
}
