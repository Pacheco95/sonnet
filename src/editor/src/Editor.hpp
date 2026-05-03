#pragma once

#include <sonnet/editor/IEditor.hpp>
#include <sonnet/editor/ILayoutManager.hpp>
#include <sonnet/editor/IPanel.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace sonnet::editor {

class Editor : public IEditor {
public:
    explicit Editor(std::filesystem::path layoutsDir);
    ~Editor() override;

    bool init(sonnet::window::IWindow& window,
              sonnet::renderer::IRendererBackend& backend) override;
    void shutdown() override;
    bool processEvent(void* sdlEvent) override;
    void render() override;

private:
    void buildDefaultLayout();

    std::filesystem::path m_layoutsDir;
    sonnet::renderer::IRendererBackend* m_backend{nullptr};
    std::unique_ptr<ILayoutManager> m_layoutManager;
    std::vector<std::unique_ptr<IPanel>> m_panels;
    bool m_initialized{false};
    bool m_showSaveDialog{false};
    bool m_showLoadDialog{false};
    char m_saveNameBuf[256]{};
    int m_loadSelected{0};
};

} // namespace sonnet::editor
