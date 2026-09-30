#pragma once

#include <sonnet/core/Error.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct SDL_Window;

namespace sonnet::editor {

enum class FileDialogMode : std::uint8_t {
  OpenFile,
  SaveFile,
  Folder,
};

struct FileDialogRequest {
  FileDialogMode mode{FileDialogMode::Folder};
  // Where the chooser starts: a file or folder, possibly one that does not exist yet.
  std::filesystem::path location{};
  // A filter for the file modes, `pattern` being SDL's semicolon-separated extensions ("scene.json").
  std::string filterName{};
  std::string filterPattern{};
};

struct FileDialogResult {
  enum class Kind : std::uint8_t {
    Picked,
    Cancelled,
    Failed
  };
  Kind kind{Kind::Cancelled};
  std::filesystem::path path{};
  std::string error{};
};

// What shows the operating system's chooser. `show` returns at once; `done` is called exactly
// once, from any thread, with the pick, a cancel or an error.
class IFileDialogBackend {
public:
  virtual ~IFileDialogBackend() = default;
  // False where no chooser can open (a headless run), so callers fall back to typing a path.
  [[nodiscard]] virtual bool available() const = 0;
  virtual void show(const FileDialogRequest &request, std::function<void(FileDialogResult)> done) = 0;
};

// SDL3's dialogs, modal to `window`. Unavailable on the offscreen and dummy video drivers.
[[nodiscard]] std::unique_ptr<IFileDialogBackend> makeSdlFileDialogBackend(SDL_Window *window);
// A backend that is never available.
[[nodiscard]] std::unique_ptr<IFileDialogBackend> makeNoFileDialogBackend();

// The editor's file chooser (docs/editor.md, "File dialogs"). The backend's callback may run on
// another thread, so the result waits in a mutex-guarded slot that the main thread takes from
// with `poll`; nothing here touches ImGui. One dialog is open at a time.
class FileDialog {
public:
  explicit FileDialog(std::unique_ptr<IFileDialogBackend> backend);

  [[nodiscard]] bool available() const;
  // True from `open` until the result has been taken.
  [[nodiscard]] bool busy() const;
  // Fails when a dialog is already open or the backend is unavailable, leaving nothing pending.
  [[nodiscard]] core::Result<void> open(const FileDialogRequest &request);
  // The finished dialog's result, once.
  [[nodiscard]] std::optional<FileDialogResult> poll();

private:
  struct Slot {
    std::mutex mutex;
    bool busy{false};
    std::optional<FileDialogResult> result;
  };

  std::unique_ptr<IFileDialogBackend> m_backend;
  // Shared with the callback, which can outlive the dialog object.
  std::shared_ptr<Slot> m_slot{std::make_shared<Slot>()};
};

} // namespace sonnet::editor
