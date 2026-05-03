#include <catch2/catch_test_macros.hpp>

#include <sonnet/editor/EditorFactory.hpp>
#include <sonnet/editor/IEditor.hpp>
#include <sonnet/editor/ILayoutManager.hpp>
#include <sonnet/editor/IPanel.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

class MockEditor : public sonnet::editor::IEditor {
public:
    bool init(sonnet::window::IWindow& /*window*/,
              sonnet::renderer::IRendererBackend& /*backend*/) override {
        ++m_initCount;
        return true;
    }
    void shutdown() override { ++m_shutdownCount; }
    bool processEvent(void* /*sdlEvent*/) override { return false; }
    void render() override { ++m_renderCount; }

    int m_initCount{0};
    int m_shutdownCount{0};
    int m_renderCount{0};
};

class MockPanel : public sonnet::editor::IPanel {
public:
    [[nodiscard]] const char* title() const override { return "Mock"; }
    void draw() override {}
};

class MockLayoutManager : public sonnet::editor::ILayoutManager {
public:
    [[nodiscard]] std::vector<std::string> listLayouts() const override { return {}; }
    bool saveLayout(std::string_view /*name*/) override { return true; }
    bool loadLayout(std::string_view /*name*/) override { return true; }
    [[nodiscard]] std::string activeLayoutName() const override { return {}; }
    void restoreLastLayout() override {}
    void resetToDefault() override {}
};

} // namespace

TEST_CASE("EditorInterface_MockEditor_LifecycleCallsCounted") {
    MockEditor editor;
    REQUIRE(editor.m_initCount == 0);
    REQUIRE(editor.m_shutdownCount == 0);
    REQUIRE(editor.m_renderCount == 0);

    editor.render();
    editor.render();
    REQUIRE(editor.m_renderCount == 2);
}

TEST_CASE("EditorInterface_MockPanel_TitleReturnsString") {
    MockPanel panel;
    REQUIRE(std::string(panel.title()) == "Mock");
    REQUIRE_FALSE(panel.isPermanent());
}

TEST_CASE("EditorInterface_MockLayoutManager_ListLayoutsEmpty") {
    MockLayoutManager mgr;
    REQUIRE(mgr.listLayouts().empty());
    REQUIRE(mgr.activeLayoutName().empty());
    REQUIRE(mgr.saveLayout("test"));
    REQUIRE(mgr.loadLayout("test"));
}

TEST_CASE("EditorInterface_IPanel_IsPermanentDefaultFalse") {
    MockPanel panel;
    REQUIRE_FALSE(panel.isPermanent());
}
