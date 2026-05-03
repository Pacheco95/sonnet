#pragma once

#include <sonnet/editor/IPanel.hpp>

#include <cstdint>

namespace sonnet::editor {

class ViewportPanel : public IPanel {
public:
    explicit ViewportPanel(uint64_t viewportTextureId);

    [[nodiscard]] const char* title() const override;
    [[nodiscard]] bool isPermanent() const override { return true; }
    void draw() override;

private:
    uint64_t m_viewportTextureId;
};

} // namespace sonnet::editor
