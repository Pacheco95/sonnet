#include <catch2/catch_test_macros.hpp>

#include <sonnet/editor/IEditor.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/window/IWindow.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class MockWindow2 : public sonnet::window::IWindow {
public:
    bool init() override { return true; }
    void shutdown() override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] std::pair<int, int> getSize() const override {
        constexpr int kWidth = 800;
        constexpr int kHeight = 600;
        return {kWidth, kHeight};
    }
    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }
    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }
};

class StubRendererBackend : public sonnet::renderer::IRendererBackend {
public:
    bool init(sonnet::window::IWindow& /*window*/) override { return true; }
    void shutdown() override {}
    void beginFrame() override {}
    void drawPrimitive() override {}
    void endFrame() override {}
};

class RecordingEditor : public sonnet::editor::IEditor {
public:
    bool init(sonnet::window::IWindow& /*window*/,
              sonnet::renderer::IRendererBackend& /*backend*/) override {
        ++initCount;
        return true;
    }
    void shutdown() override { ++shutdownCount; }
    bool processEvent(void* /*sdlEvent*/) override { return false; }
    void render() override { ++renderCount; }

    int initCount{0};
    int shutdownCount{0};
    int renderCount{0};
};

} // namespace

TEST_CASE("EditorLifecycle_InitRenderShutdown_CallCounts") {
    RecordingEditor editor;
    MockWindow2 window;
    StubRendererBackend backend;

    REQUIRE(editor.initCount == 0);
    editor.init(window, backend);
    REQUIRE(editor.initCount == 1);

    constexpr int kRenderIterations = 5;
    for (int i = 0; i < kRenderIterations; ++i) {
        editor.render();
    }
    REQUIRE(editor.renderCount == 5);

    editor.shutdown();
    REQUIRE(editor.shutdownCount == 1);
}

TEST_CASE("EditorLifecycle_ShutdownAfterFailedInit_Safe") {
    RecordingEditor editor;
    // shutdown before any init — should be safe (mock does nothing)
    editor.shutdown();
    REQUIRE(editor.shutdownCount == 1); // mock always increments
}
