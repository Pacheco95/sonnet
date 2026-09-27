#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/runtime/Capture.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sonnet::editor {

class Editor;

// The capture flags, their table and their parser are runtime's, shared with the player
// (ADR-0018); the editor takes all of them (docs/editor.md, "Screenshots").
using runtime::CaptureOptions;
using runtime::CommandLineOption;

// The editor's arguments: a project folder, and the capture flags that make the run a capture.
struct CommandLine {
  std::filesystem::path project;
  std::optional<CaptureOptions> capture;
  bool help{false}; // --help or -h: print commandLineUsage and quit
};

// Every capture flag, in the order the usage lists them, the player's and the editor's own.
[[nodiscard]] std::span<const CommandLineOption> commandLineOptions() noexcept;

// `sonnet_editor [project] [capture flags]`. A capture needs a project and at least one
// screenshot; the other capture flags need a screenshot to be any use. --help alone is help.
[[nodiscard]] core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args);

// What --help prints: the flags from commandLineOptions, the shading terms, the exit codes and
// an example.
[[nodiscard]] std::string commandLineUsage();

// Steps runtime's capture run through the frames of an editor that has opened the project: its
// viewport is the scene's screenshot, and it has the selection and the window with its panels.
class CaptureRun {
public:
  using Status = runtime::CaptureRun::Status;
  static constexpr float FrameSeconds = runtime::CaptureRun::FrameSeconds;

  explicit CaptureRun(CaptureOptions options);
  // After each frame, once the editor's afterPresent has run.
  [[nodiscard]] Status step(Editor &editor);

private:
  runtime::CaptureRun m_run;
};

} // namespace sonnet::editor
