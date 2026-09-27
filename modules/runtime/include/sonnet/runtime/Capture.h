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

namespace sonnet::runtime {

class Game;

// The two applications that take the capture flags. They share the table and the parser; the
// editor takes every flag, the player those it has a use for (ADR-0018).
enum class CaptureApplication : std::uint8_t {
  Editor,
  Player,
};

// A scripted run that ends in screenshots, for agents and scripts that cannot capture the screen
// themselves (docs/editor.md, "Screenshots"; docs/player.md, "Capture runs").
struct CaptureOptions {
  std::filesystem::path scene; // relative to the project; empty keeps its start scene
  std::optional<float> playSeconds;
  std::string select; // an entity path of names, "Parent/Child"; the editor's only
  std::optional<renderer::DebugView> shadingTerm;
  std::filesystem::path viewport; // --screenshot: the scene as PNG, the editor's viewport or the player's window
  std::filesystem::path window;   // --screenshot-window: the whole editor window as PNG; the editor's only
  std::uint32_t settleFrames{10}; // frames drawn after the assets have loaded, before anything else
};

// An application's arguments: the content (the editor's project folder, the player's project
// folder or bundle), and the capture flags that make the run a capture.
struct CommandLine {
  std::filesystem::path content;
  std::optional<CaptureOptions> capture;
  // The player's --scene without a screenshot: a plain run of that scene rather than the start
  // scene. The editor has no use for it and refuses it, as it refuses every flag without one.
  std::filesystem::path scene;
  bool help{false}; // --help or -h: print commandLineUsage and quit
};

// One capture flag, as the parser accepts it and the usage text lists it.
struct CommandLineOption {
  std::string_view flag;       // "--play"
  std::string_view value;      // what it takes, "SECONDS"
  std::string_view help;       // one line
  std::string_view example;    // a value it accepts, for the tests
  bool player;                 // whether the player takes it too
  std::string_view playerHelp; // the player's line where it differs from the editor's; empty for the same
};

// Every capture flag, in the order the usage lists them. tools/check_docs.py holds docs/editor.md
// and docs/player.md to this list, so a flag cannot be added without being documented.
[[nodiscard]] std::span<const CommandLineOption> commandLineOptions() noexcept;
[[nodiscard]] bool takes(CaptureApplication application, const CommandLineOption &option) noexcept;

// `sonnet_editor [project] [capture flags]` or `sonnet_player [game] [capture flags]`, in any
// order. A capture needs a screenshot; the other capture flags need one to be any use, apart from
// the player's --scene, which alone picks the scene of a plain run. The editor's capture also
// needs a project, while the player's content defaults to game.sbundle. --help alone is help.
[[nodiscard]] core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args,
                                                         CaptureApplication application);

// --play's value: a non-negative decimal number of seconds, `3`, `2.5`, `.5` or `1e3`, parsed
// without floating-point std::from_chars, which alone would set the player's minimum iOS
// (ADR-0018, open question 3). It takes what from_chars took before it.
[[nodiscard]] std::optional<float> parseSeconds(std::string_view text);

// What --help prints: the application's flags, the shading terms, the exit codes and an example.
[[nodiscard]] std::string commandLineUsage(CaptureApplication application);

// The player writes a relative screenshot path under `directory`, Platform::prefPath, which is
// the one place a phone lets it write (docs/player.md, "Capture runs"). An absolute path stays.
void resolveOutputs(CaptureOptions &options, const std::filesystem::path &directory);

// What a capture run steps through: the editor, or the player's game.
class ICaptureTarget {
public:
  virtual ~ICaptureTarget() = default;
  // Opens --scene, when there is one, and sets --shading-term.
  [[nodiscard]] virtual core::Result<void> begin(const CaptureOptions &options) = 0;
  // Whether a frame has drawn into the image the screenshot copies, with nothing left loading.
  [[nodiscard]] virtual bool loaded() = 0;
  // Seeds the scripts' math.random and starts the simulation.
  virtual void play(std::uint64_t randomSeed) = 0;
  // --select: the entity at a path of names, outlined.
  [[nodiscard]] virtual core::Result<void> select(std::string_view path) = 0;
  // The next frame that can copies the scene into `viewport` and the window into `window`; an
  // empty path is not captured. The outcome is taken once, after the frame is written.
  virtual void requestScreenshots(const std::filesystem::path &viewport, const std::filesystem::path &window) = 0;
  [[nodiscard]] virtual std::optional<core::Result<void>> takeScreenshotResult() = 0;
};

// Steps a capture through the frames of an application that has opened its content: the scene
// and the shading term, then the frames it waits for the assets and the settling, the simulation
// for the seconds asked, the selection, and the screenshots. The application runs at FrameSeconds
// a frame meanwhile, so a capture plays the same simulation however fast the machine draws.
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
  // After each frame, once the frame's screenshots have been written.
  [[nodiscard]] Status step(ICaptureTarget &target);

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

// The player's side of a capture: its game, which has no selection and no panels.
class GameCaptureTarget final : public ICaptureTarget {
public:
  explicit GameCaptureTarget(Game &game) : m_game(game) {
  }
  [[nodiscard]] core::Result<void> begin(const CaptureOptions &options) override;
  [[nodiscard]] bool loaded() override;
  void play(std::uint64_t randomSeed) override;
  [[nodiscard]] core::Result<void> select(std::string_view path) override;
  void requestScreenshots(const std::filesystem::path &viewport, const std::filesystem::path &window) override;
  [[nodiscard]] std::optional<core::Result<void>> takeScreenshotResult() override;

private:
  Game &m_game;
};

} // namespace sonnet::runtime
