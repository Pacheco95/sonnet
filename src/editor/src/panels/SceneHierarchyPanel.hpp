#pragma once

#include <sonnet/editor/IPanel.hpp>

namespace sonnet::editor {

class SceneHierarchyPanel : public IPanel {
public:
    [[nodiscard]] const char* title() const override;
    void draw() override;
};

} // namespace sonnet::editor
