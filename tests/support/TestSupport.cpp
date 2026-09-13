// Compiled into every test executable. On Windows a crash, an abort or a debug-CRT assertion
// opens a dialog by default, which blocks forever on a CI runner; route all of them to stderr
// and a non-zero exit instead. Other platforms need nothing.
#if defined(_WIN32)
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

namespace {

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

} // namespace
#endif
