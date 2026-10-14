#include <sonnet/editor/FileManager.h>

#include <sonnet/core/Log.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>

#if defined(_WIN32)
#include <windows.h>
// shlobj.h needs windows.h first.
#include <shlobj.h>
#else
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#endif

#include <cctype>
#include <format>
#include <string_view>
#include <thread>
#include <utility>

namespace sonnet::editor {

namespace {

#if defined(_WIN32)

bool showSelected(const std::filesystem::path &file) {
  // The shell's folder views are COM objects; this thread is the caller's own, so it initialises COM for itself.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  bool shown = false;
  if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(file.wstring().c_str()); item != nullptr) {
    shown = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    ILFree(item);
  }
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
  return shown;
}

#else

// Runs `args` without a shell, its output discarded, and waits for it; true when it exits with 0.
bool run(const char *const *args) {
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void *>(const_cast<char **>(args)));
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_Process *process = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (process == nullptr) {
    return false;
  }
  int exitCode = -1;
  SDL_WaitProcess(process, true, &exitCode);
  SDL_DestroyProcess(process);
  return exitCode == 0;
}

bool showSelected(const std::filesystem::path &file) {
#if defined(__APPLE__)
  const std::string path = file.string();
  const char *const args[] = {"open", "-R", path.c_str(), nullptr};
#else
  // --print-reply makes dbus-send wait for the answer, so a missing FileManager1 service is a non-zero exit.
  const std::string items = "array:string:" + fileUrl(file);
  const char *const args[] = {"dbus-send",
                              "--session",
                              "--print-reply",
                              "--reply-timeout=10000",
                              "--dest=org.freedesktop.FileManager1",
                              "/org/freedesktop/FileManager1",
                              "org.freedesktop.FileManager1.ShowItems",
                              items.c_str(),
                              "string:",
                              nullptr};
#endif
  return run(args);
}

#endif

} // namespace

std::string fileUrl(const std::filesystem::path &path) {
  const std::string generic = path.generic_string();
  std::string url = "file://";
  if (generic.empty() || generic.front() != '/') {
    url += '/'; // a drive letter: file:///C:/...
  }
  for (const char c : generic) {
    const auto byte = static_cast<unsigned char>(c);
    if (std::isalnum(byte) != 0 || std::string_view{"/:-._~"}.contains(c)) {
      url += c;
    } else {
      url += std::format("%{:02X}", byte);
    }
  }
  return url;
}

void revealInFileManager(const std::filesystem::path &file) {
  std::filesystem::path absolute = std::filesystem::absolute(file).lexically_normal();
  absolute.make_preferred();
  std::thread{[file = std::move(absolute)] {
    if (showSelected(file)) {
      return;
    }
    SONNET_LOG_WARN("cannot select {} in the file manager; opening its folder", file.string());
    if (!SDL_OpenURL(fileUrl(file.parent_path()).c_str())) {
      SONNET_LOG_ERROR("cannot open {}: {}", file.parent_path().string(), SDL_GetError());
    }
  }}.detach();
}

} // namespace sonnet::editor
