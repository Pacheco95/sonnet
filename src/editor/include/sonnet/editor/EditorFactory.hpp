#pragma once

#include <filesystem>
#include <memory>

namespace sonnet::editor {

class IEditor;

// Returns the default ImGui-based editor implementation.
std::unique_ptr<IEditor> createEditor(const std::filesystem::path& layoutsDir);

} // namespace sonnet::editor
