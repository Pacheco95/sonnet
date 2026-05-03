#include <catch2/catch_test_macros.hpp>

#include <sonnet/editor/EditorFactory.hpp>
#include <sonnet/editor/IEditor.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/window/IWindow.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

class HeadlessMockWindow : public sonnet::window::IWindow {
public:
    bool init() override { return true; }
    void shutdown() override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] std::pair<int, int> getSize() const override { return {800, 600}; }
    [[nodiscard]] std::vector<std::string> getRequiredInstanceExtensions() const override {
        return {};
    }
    [[nodiscard]] uint64_t createSurface(uint64_t /*instanceHandle*/) const override { return 0; }
    [[nodiscard]] void* getWindowHandle() const override { return nullptr; }
};

class ZeroHandlesBackend : public sonnet::renderer::IRendererBackend {
public:
    bool init(sonnet::window::IWindow& /*window*/) override { return true; }
    void shutdown() override {}
    void beginFrame() override {}
    void drawPrimitive() override {}
    void endFrame() override {}
    // getNativeHandles() returns zero struct via default implementation
};

} // namespace

TEST_CASE("EditorLifecycle_ZeroHandles_InitFailsGracefully") {
    // When the renderer native handles are all zero, the Vulkan ImGui backend
    // cannot initialise. Editor::init() must return false without crashing.
    HeadlessMockWindow window;
    ZeroHandlesBackend backend;

    auto editor = sonnet::editor::createEditor(std::filesystem::temp_directory_path() / "sonnet_test_layouts");
    bool result = editor->init(window, backend);
    REQUIRE_FALSE(result);

    // shutdown() must be safe to call after a failed init
    editor->shutdown();
    REQUIRE(true); // reached here without crash
}
