#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/renderer/Renderer.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sonnet::editor {

class Editor;

// A scripted run of the editor that ends in screenshots, for agents and scripts that cannot
// capture the screen themselves (docs/editor.md, "Screenshots").
struct CaptureOptions {
  std::filesystem::path scene; // relative to the project; empty keeps its start scene
  std::optional<float> playSeconds;
  std::string select; // an entity path of names, "Parent/Child"
  std::optional<renderer::DebugView> shadingTerm;
  std::filesystem::path viewport; // the viewport's scene as PNG; empty for none
  std::filesystem::path window;   // the whole window as PNG; empty for none
  std::uint32_t settleFrames{10}; // frames drawn after the assets have loaded, before anything else
};

// The editor's arguments: a project folder, and the capture flags that make the run a capture.
struct CommandLine {
  std::filesystem::path project;
  std::optional<CaptureOptions> capture;
  bool help{false}; // --help or -h: print commandLineUsage and quit
};

// One capture flag, as the parser accepts it and the usage text lists it.
struct CommandLineOption {
  std::string_view flag;    // "--play"
  std::string_view value;   // what it takes, "SECONDS"
  std::string_view help;    // one line
  std::string_view example; // a value it accepts, for the tests
};

// Every capture flag, in the order the usage lists them. tools/check_docs.py holds docs/editor.md
// to this list, so a flag cannot be added without being documented.
[[nodiscard]] std::span<const CommandLineOption> commandLineOptions() noexcept;

// `sonnet_editor [project] [capture flags]`. A capture needs a project and at least one
// screenshot; the other capture flags need a screenshot to be any use. --help alone is help.
[[nodiscard]] core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args);

// What --help prints: the flags from commandLineOptions, the shading terms, the exit codes and
// an example.
[[nodiscard]] std::string commandLineUsage();

// Steps a capture through the frames of an editor that has opened the project: the scene and the
// shading term, then the frames it waits for the assets and the settling, play mode for the
// seconds asked, the selection, and the screenshots. The application runs at FrameSeconds a
// frame meanwhile, so a capture plays the same simulation however fast the machine draws.
class CaptureRun {
public:
  static constexpr float FrameSeconds = 1.0f / 60.0f;
  // How long the assets may take to load before the run gives up.
  static constexpr std::chrono::seconds LoadTimeout{120};
  // Frames drawn between the selection and the capture, so the panels have laid out what it
  // changed: an ImGui table sizes its columns from the frames before (docs/editor.md,
  // "Screenshots").
  static constexpr std::uint32_t SelectionFrames = 3;
  // What math.random is seeded with before playing: Lua seeds it differently in every process,
  // and a scene whose scripts draw random numbers would otherwise play differently every run.
  static constexpr std::uint64_t RandomSeed = 1;

  enum class Status : std::uint8_t {
    Running,
    Done,
    Failed, // logged
  };

  explicit CaptureRun(CaptureOptions options);
  // After each frame, once the editor's afterPresent has run.
  [[nodiscard]] Status step(Editor &editor);

private:
  enum class Stage : std::uint8_t {
    Start,
    Settle,
    Play,
    Select,
    Capture,
    Write,
  };

  [[nodiscard]] Status fail(const core::Error &error);

  CaptureOptions m_options;
  Stage m_stage{Stage::Start};
  std::uint32_t m_frames{0};
  std::chrono::steady_clock::time_point m_loadStart;
};

} // namespace sonnet::editor
