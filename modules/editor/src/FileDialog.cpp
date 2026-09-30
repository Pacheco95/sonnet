#include <sonnet/editor/FileDialog.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <cstring>
#include <string_view>
#include <utility>

namespace sonnet::editor {

namespace {

struct SdlCall {
  std::function<void(FileDialogResult)> done;
};

void SDLCALL onSdlResult(void *userdata, const char *const *files, int) {
  const std::unique_ptr<SdlCall> call{static_cast<SdlCall *>(userdata)};
  FileDialogResult result;
  if (files == nullptr) {
    result.kind = FileDialogResult::Kind::Failed;
    result.error = SDL_GetError();
    if (result.error.empty()) {
      result.error = "the file chooser failed";
    }
  } else if (files[0] == nullptr) {
    result.kind = FileDialogResult::Kind::Cancelled;
  } else {
    result.kind = FileDialogResult::Kind::Picked;
    result.path = std::filesystem::path{std::u8string_view{reinterpret_cast<const char8_t *>(files[0])}};
  }
  call->done(std::move(result));
}

class SdlBackend final : public IFileDialogBackend {
public:
  explicit SdlBackend(SDL_Window *window) : m_window(window) {
  }

  [[nodiscard]] bool available() const override {
    const char *driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr && std::strcmp(driver, "offscreen") != 0 && std::strcmp(driver, "dummy") != 0;
  }

  void show(const FileDialogRequest &request, std::function<void(FileDialogResult)> done) override {
    const auto type = request.mode == FileDialogMode::OpenFile   ? SDL_FILEDIALOG_OPENFILE
                      : request.mode == FileDialogMode::SaveFile ? SDL_FILEDIALOG_SAVEFILE
                                                                 : SDL_FILEDIALOG_OPENFOLDER;
    // The strings live until SDL has copied them, which it does before this returns.
    const std::u8string utf8 = request.location.u8string();
    const std::string location{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
    const SDL_DialogFileFilter filter{request.filterName.c_str(), request.filterPattern.c_str()};
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, m_window);
    if (!location.empty()) {
      SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    }
    if (request.mode != FileDialogMode::Folder && !request.filterPattern.empty()) {
      SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter *>(&filter));
      SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    }
    // The callback deletes the call; SDL calls it exactly once, even on an error.
    SDL_ShowFileDialogWithProperties(type, onSdlResult, new SdlCall{std::move(done)}, props);
    SDL_DestroyProperties(props);
  }

private:
  SDL_Window *m_window;
};

class NoBackend final : public IFileDialogBackend {
public:
  [[nodiscard]] bool available() const override {
    return false;
  }
  void show(const FileDialogRequest &, std::function<void(FileDialogResult)> done) override {
    done({.kind = FileDialogResult::Kind::Failed, .error = "no file chooser is available"});
  }
};

} // namespace

std::unique_ptr<IFileDialogBackend> makeSdlFileDialogBackend(SDL_Window *window) {
  return std::make_unique<SdlBackend>(window);
}

std::unique_ptr<IFileDialogBackend> makeNoFileDialogBackend() {
  return std::make_unique<NoBackend>();
}

FileDialog::FileDialog(std::unique_ptr<IFileDialogBackend> backend) : m_backend(std::move(backend)) {
}

bool FileDialog::available() const {
  return m_backend->available();
}

bool FileDialog::busy() const {
  const std::scoped_lock lock{m_slot->mutex};
  return m_slot->busy;
}

core::Result<void> FileDialog::open(const FileDialogRequest &request) {
  if (!m_backend->available()) {
    return std::unexpected(core::Error{"no file chooser is available here: type the path", core::ErrorCategory::Io});
  }
  {
    const std::scoped_lock lock{m_slot->mutex};
    if (m_slot->busy) {
      return std::unexpected(core::Error{"a file chooser is already open", core::ErrorCategory::Io});
    }
    m_slot->busy = true;
    m_slot->result.reset();
  }
  m_backend->show(request, [slot = m_slot](FileDialogResult result) {
    const std::scoped_lock lock{slot->mutex};
    slot->result = std::move(result);
  });
  return {};
}

std::optional<FileDialogResult> FileDialog::poll() {
  const std::scoped_lock lock{m_slot->mutex};
  if (!m_slot->result) {
    return std::nullopt;
  }
  std::optional<FileDialogResult> result = std::move(m_slot->result);
  m_slot->result.reset();
  m_slot->busy = false;
  return result;
}

} // namespace sonnet::editor
