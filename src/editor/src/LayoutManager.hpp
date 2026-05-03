#pragma once

#include <sonnet/editor/ILayoutManager.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace sonnet::editor {

class LayoutManager : public ILayoutManager {
public:
    explicit LayoutManager(std::filesystem::path layoutsDir);

    [[nodiscard]] std::vector<std::string> listLayouts() const override;
    bool saveLayout(std::string_view name) override;
    bool loadLayout(std::string_view name) override;
    [[nodiscard]] std::string activeLayoutName() const override;
    void restoreLastLayout() override;
    void resetToDefault() override;

private:
    [[nodiscard]] bool isValidName(std::string_view name) const;

    std::filesystem::path m_layoutsDir;
};

} // namespace sonnet::editor
