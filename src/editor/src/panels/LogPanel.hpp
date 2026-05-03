#pragma once

#include <sonnet/editor/IPanel.hpp>

#include "../log/EditorLogSink.hpp"

#include <memory>

namespace sonnet::editor {

struct LogFilter {
    bool showInfo{true};
    bool showWarnings{true};
    bool showErrors{true};
};

class LogPanel : public IPanel {
public:
    explicit LogPanel(std::shared_ptr<LogBuffer> buffer);

    [[nodiscard]] const char* title() const override;
    void draw() override;

private:
    std::shared_ptr<LogBuffer> m_buffer;
    LogFilter m_filter;
    bool m_autoScroll{true};
};

} // namespace sonnet::editor
