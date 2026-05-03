#include <sonnet/editor/EditorFactory.hpp>
#include <sonnet/editor/IEditor.hpp>

#include "Editor.hpp"

namespace sonnet::editor {

std::unique_ptr<IEditor> createEditor(const std::filesystem::path& layoutsDir) {
    return std::make_unique<Editor>(layoutsDir);
}

} // namespace sonnet::editor
