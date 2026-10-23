// Compiled into every test executable.
//
// Scratch directory: tests build fixed names under std::filesystem::temp_directory_path()
// ("sonnet_editor_tests", "sonnet_world_morph", ...), so two runs at once, from two checkouts or
// two worktrees, deleted each other's files (issue #398). Each process points the temporary
// directory at one of its own, named after the suite and the process id, and removes it on a
// normal exit; a crash or a ctest timeout leaves it behind in the system temporary directory.
//
// Windows: a crash, an abort or a debug-CRT assertion opens a dialog by default, which blocks
// forever on a CI runner; route all of them to stderr and a non-zero exit instead.
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
struct DisableDialogs {
  DisableDialogs() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    // The report functions are no-op macros in the release CRT.
    constexpr int reportTypes[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (const int type : reportTypes) {
      _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
      _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
  }
};

const DisableDialogs g_disableDialogs;
#endif

unsigned long processId() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return static_cast<unsigned long>(getpid());
#endif
}

class ProcessTempDirectory {
public:
  ProcessTempDirectory() {
    std::error_code error;
    const std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error) {
      return;
    }
    // A directory left by a crashed run whose process id came round again must not leak into this one.
    const std::filesystem::path path = base / ("sonnet-" SONNET_MODULE "-" + std::to_string(processId()));
    std::filesystem::remove_all(path, error);
    if (!std::filesystem::create_directories(path, error) || error) {
      return;
    }
    // temp_directory_path reads TMPDIR on POSIX and TMP on Windows, first of all its candidates.
#if defined(_WIN32)
    const bool redirected = _putenv_s("TMP", path.string().c_str()) == 0;
#else
    const bool redirected = setenv("TMPDIR", path.c_str(), 1) == 0;
#endif
    if (redirected) {
      m_path = path;
    } else {
      std::filesystem::remove(path, error);
    }
  }

  ~ProcessTempDirectory() {
    if (!m_path.empty()) {
      std::error_code error;
      std::filesystem::remove_all(m_path, error);
    }
  }

  ProcessTempDirectory(const ProcessTempDirectory &) = delete;
  ProcessTempDirectory &operator=(const ProcessTempDirectory &) = delete;

private:
  std::filesystem::path m_path;
};

const ProcessTempDirectory g_processTempDirectory;

} // namespace
