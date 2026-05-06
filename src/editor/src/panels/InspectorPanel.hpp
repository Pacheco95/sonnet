#pragma once

#include "../SceneContext.hpp"

#include <sonnet/editor/IPanel.hpp>

namespace sonnet::editor {

class InspectorPanel : public IPanel {
public:
    explicit InspectorPanel(SceneContext& sceneCtx);

    [[nodiscard]] const char* title() const override;
    void draw() override;

private:
    SceneContext& m_sceneCtx;
};

} // namespace sonnet::editor
