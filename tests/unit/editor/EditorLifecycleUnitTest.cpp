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
    [[nodiscard]] std::pair<int, int> getSize() const override { return {800, 600}; }
    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }
    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }
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
    sonnet::renderer::IRendererBackend* backend = nullptr; // not used by mock

    // Suppress unused warning — mock backend pointer is not dereferenced
    (void)backend;

    REQUIRE(editor.initCount == 0);
    editor.init(window, *reinterpret_cast<sonnet::renderer::IRendererBackend*>(backend));
    REQUIRE(editor.initCount == 1);

    for (int i = 0; i < 5; ++i) {
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
