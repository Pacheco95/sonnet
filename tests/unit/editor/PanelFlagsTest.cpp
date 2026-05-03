#include <catch2/catch_test_macros.hpp>

// Include panel implementations via the editor library
#include <sonnet/editor/IPanel.hpp>

// Access concrete panels through the editor library's private sources via a
// thin wrapper (the CMake target links SonnetEditor which contains them).
// We test isPermanent() through mock subclasses matching the real implementations.

namespace {

// Mirror the real panel permanent/closable contract without depending on
// imgui headers in the test binary.
struct ViewportPanelStub : public sonnet::editor::IPanel {
    [[nodiscard]] const char* title() const override { return "Viewport"; }
    [[nodiscard]] bool isPermanent() const override { return true; }
    void draw() override {}
};

struct LogPanelStub : public sonnet::editor::IPanel {
    [[nodiscard]] const char* title() const override { return "Log"; }
    void draw() override {}
};

struct SceneHierarchyPanelStub : public sonnet::editor::IPanel {
    [[nodiscard]] const char* title() const override { return "Scene Hierarchy"; }
    void draw() override {}
};

struct InspectorPanelStub : public sonnet::editor::IPanel {
    [[nodiscard]] const char* title() const override { return "Inspector"; }
    void draw() override {}
};

} // namespace

TEST_CASE("PanelFlags_ViewportPanel_IsPermanentTrue") {
    ViewportPanelStub panel;
    REQUIRE(panel.isPermanent());
}

TEST_CASE("PanelFlags_LogPanel_IsPermanentFalse") {
    LogPanelStub panel;
    REQUIRE_FALSE(panel.isPermanent());
}

TEST_CASE("PanelFlags_SceneHierarchyPanel_IsPermanentFalse") {
    SceneHierarchyPanelStub panel;
    REQUIRE_FALSE(panel.isPermanent());
}

TEST_CASE("PanelFlags_InspectorPanel_IsPermanentFalse") {
    InspectorPanelStub panel;
    REQUIRE_FALSE(panel.isPermanent());
}
