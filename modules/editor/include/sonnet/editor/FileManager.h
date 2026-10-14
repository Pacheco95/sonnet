#pragma once

#include <filesystem>
#include <string>

namespace sonnet::editor {

// A file: URL for a file or folder, percent-encoded so a space or a non-ASCII byte survives the trip to the
// platform's opener and to the file manager's D-Bus interface.
[[nodiscard]] std::string fileUrl(const std::filesystem::path &path);

// Opens the platform's file manager on the folder holding `file` with `file` selected: the freedesktop
// FileManager1.ShowItems D-Bus call on Linux, `open -R` on macOS, SHOpenFolderAndSelectItems on Windows. Where that
// fails (no file manager owns FileManager1, for one) it opens the folder with SDL_OpenURL instead. The work runs on a
// detached thread, since a file manager that has to start first can take seconds to answer.
void revealInFileManager(const std::filesystem::path &file);

} // namespace sonnet::editor
