#pragma once

#include "../SceneContext.hpp"

#include <sonnet/editor/IPanel.hpp>

namespace sonnet::editor {

class SceneHierarchyPanel : public IPanel {
public:
    explicit SceneHierarchyPanel(SceneContext& sceneCtx);

    [[nodiscard]] const char* title() const override;
    void draw() override;

private:
    SceneContext& m_sceneCtx;
};

} // namespace sonnet::editor
