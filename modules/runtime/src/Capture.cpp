#include <sonnet/runtime/Capture.h>

#include <sonnet/core/Log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <source_location>
#include <string>

namespace sonnet::runtime {

namespace {

// At the caller's location, as an Error created there would be.
core::Error captureError(std::string message, std::source_location location = std::source_location::current()) {
  return core::Error{std::move(message), core::ErrorCategory::Unknown, location};
}

// "Sun direct" is "sun-direct" on the command line.
std::string flagName(std::string_view name) {
  std::string flag;
  for (const char c : name) {
    flag.push_back(c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return flag;
}

std::optional<renderer::DebugView> shadingTerm(std::string_view text) {
  for (std::uint32_t i = 0; i < renderer::DebugViewCount; ++i) {
    const auto view = static_cast<renderer::DebugView>(i);
    if (flagName(renderer::debugViewName(view)) == flagName(text)) {
      return view;
    }
  }
  return std::nullopt;
}

std::string shadingTerms() {
  std::string terms;
  for (std::uint32_t t = 0; t < renderer::DebugViewCount; ++t) {
    terms += (t == 0 ? "" : ", ") + flagName(renderer::debugViewName(static_cast<renderer::DebugView>(t)));
  }
  return terms;
}

constexpr std::array<CommandLineOption, 7> Options{{
    {"--screenshot", "FILE", "the viewport's scene, at the viewport's size, as a PNG", "shots/view.png", true,
     "the scene, at the window's size, as a PNG"},
    {"--screenshot-window", "FILE", "the whole editor window, with its panels, as a PNG", "shots/window.png", false,
     ""},
    {"--scene", "FILE", "open this scene, relative to the project, instead of its start scene",
     "scenes/playground.scene.json", true, ""},
    {"--play", "SECONDS", "play for this many seconds first (60 fixed steps a second) and capture while playing", "2.5",
     true, ""},
    {"--select", "PATH", "select the entity at this path of names, Parent/Child, so it is outlined", "Ball", false, ""},
    {"--shading-term", "TERM", "show one term of the forward shading instead of the final image", "sun-direct", true,
     ""},
    {"--settle-frames", "N", "frames drawn after the assets have loaded, before anything else (10)", "4", true, ""},
}};

} // namespace

std::span<const CommandLineOption> commandLineOptions() noexcept {
  return Options;
}

bool takes(CaptureApplication application, const CommandLineOption &option) noexcept {
  return application == CaptureApplication::Editor || option.player;
}

std::optional<float> parseSeconds(std::string_view text) {
  // What floating-point from_chars took in its general format, and --play then kept: an optional
  // minus, digits with at most one point and at least one digit, and an exponent, all of it, in
  // float's range, finite and not negative. So "2.5", ".5", "5.", "1e3" and "-0", but not "+1",
  // " 1", "1e", "0x1", "inf" or "-0.5".
  std::size_t i = 0;
  const bool negative = i < text.size() && text[i] == '-';
  if (negative) {
    ++i;
  }
  // Up to 19 significant digits in the integer, which a uint64 holds; any more only move the
  // point, and a value that long is not a number of seconds anyone types.
  std::uint64_t mantissa = 0;
  int significant = 0;
  long exponent = 0;
  bool digits = false;
  bool point = false;
  for (; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '.' && !point) {
      point = true;
      continue;
    }
    if (c < '0' || c > '9') {
      break;
    }
    digits = true;
    if (mantissa == 0 && c == '0') {
      exponent -= point ? 1 : 0; // a leading zero is no significant digit
    } else if (significant < 19) {
      mantissa = mantissa * 10 + static_cast<std::uint64_t>(c - '0');
      ++significant;
      exponent -= point ? 1 : 0;
    } else {
      exponent += point ? 0 : 1;
    }
  }
  if (!digits) {
    return std::nullopt;
  }
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    const bool negativeExponent = i < text.size() && text[i] == '-';
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
      ++i;
    }
    if (i == text.size()) {
      return std::nullopt;
    }
    long written = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
      written = std::min(written * 10 + (text[i] - '0'), 100000L); // far past float's range either way
    }
    if (i < text.size()) {
      return std::nullopt;
    }
    exponent += negativeExponent ? -written : written;
  }
  if (i != text.size()) {
    return std::nullopt;
  }
  // Up to 10^22 a power of ten is exact in a double, so every value with a short fraction is the
  // correctly rounded one from_chars gave.
  double value = static_cast<double>(mantissa);
  if (mantissa != 0) {
    const long clamped = std::clamp(exponent, -400L, 400L);
    value = clamped < 0 ? value / std::pow(10.0, static_cast<double>(-clamped))
                        : value * std::pow(10.0, static_cast<double>(clamped));
  }
  // Out of float's range is an error, as it was for from_chars: past the largest float by half its
  // step rounds to infinity, and a non-zero value that rounds to zero is too small.
  const double largest = static_cast<double>(std::numeric_limits<float>::max()) +
                         std::ldexp(1.0, std::numeric_limits<float>::max_exponent - 25);
  if (!(value < largest)) {
    return std::nullopt;
  }
  const float seconds = static_cast<float>(value);
  if (mantissa != 0 && seconds == 0.0f) {
    return std::nullopt;
  }
  if (negative && mantissa != 0) {
    return std::nullopt;
  }
  return negative ? -seconds : seconds;
}

std::string commandLineUsage(CaptureApplication application) {
  const bool editor = application == CaptureApplication::Editor;
  std::string usage = editor ? "usage: sonnet_editor [project] [capture flags]\n\n"
                               "Opens the editor, on the project folder when one is given. The capture flags instead\n"
                               "write screenshots and quit, for agents and scripts that cannot capture the screen\n"
                               "(docs/editor.md, \"Screenshots\"). A capture needs a project and --screenshot,\n"
                               "--screenshot-window or both.\n\n"
                             : "usage: sonnet_player [game] [capture flags]\n\n"
                               "Runs a project folder or a cooked bundle, or game.sbundle beside the binary when none\n"
                               "is given. The capture flags instead write a screenshot and quit, for agents and\n"
                               "scripts that cannot capture the screen (docs/player.md, \"Capture runs\"). A capture\n"
                               "needs --screenshot; a relative path is written under the player's preferences\n"
                               "directory, which the log names. --scene alone runs that scene instead of the start\n"
                               "scene, without capturing.\n\n";
  std::size_t width = 0;
  for (const CommandLineOption &option : Options) {
    if (takes(application, option)) {
      width = std::max(width, option.flag.size() + 1 + option.value.size());
    }
  }
  for (const CommandLineOption &option : Options) {
    if (takes(application, option)) {
      const std::string_view help = !editor && !option.playerHelp.empty() ? option.playerHelp : option.help;
      usage += std::format("  {:<{}}  {}\n", std::format("{} {}", option.flag, option.value), width, help);
    }
  }
  usage += "\nShading terms: " + shadingTerms() + ".\n\n";
  if (editor) {
    usage += "The exit code is 0 once every screenshot is written and 1 on any error, which the log\n"
             "explains. The same flags give the same image run after run.\n\n"
             "Example:\n"
             "  sonnet_editor apps/samples/basic --scene scenes/playground.scene.json --play 3 \\\n"
             "    --select Ball --screenshot shots/view.png --screenshot-window shots/window.png\n";
  } else {
    usage += "The exit code is 0 once the screenshot is written and 1 on any error, which the log\n"
             "explains; the log's last line says which. The same flags give the same image run\n"
             "after run on the same GPU.\n\n"
             "Example:\n"
             "  sonnet_player apps/samples/basic --scene scenes/playground.scene.json --play 3 \\\n"
             "    --screenshot playground.png\n";
  }
  return usage;
}

core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args, CaptureApplication application) {
  const bool editor = application == CaptureApplication::Editor;
  CommandLine line;
  CaptureOptions capture;
  bool captureFlags = false;
  bool sceneOnly = true; // the player's --scene and no other flag is a plain run of that scene
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      line.help = true;
      continue;
    }
    if (!arg.starts_with("--")) {
      if (!line.content.empty()) {
        return std::unexpected(
            captureError(std::format("a second {} \"{}\"", editor ? "project folder" : "game", arg)));
      }
      line.content = std::filesystem::path{arg};
      continue;
    }
    if (std::ranges::none_of(Options, [&](const CommandLineOption &option) {
          return option.flag == arg && takes(application, option);
        })) {
      return std::unexpected(captureError(std::format("unknown option {}; --help lists them", arg)));
    }
    if (i + 1 == args.size()) {
      return std::unexpected(captureError(std::format("{} needs a value", arg)));
    }
    const std::string_view value = args[++i];
    captureFlags = true;
    sceneOnly = sceneOnly && arg == "--scene";
    if (arg == "--screenshot") {
      capture.viewport = std::filesystem::path{value};
    } else if (arg == "--screenshot-window") {
      capture.window = std::filesystem::path{value};
    } else if (arg == "--scene") {
      capture.scene = std::filesystem::path{value};
    } else if (arg == "--select") {
      capture.select = std::string{value};
    } else if (arg == "--play") {
      capture.playSeconds = parseSeconds(value);
      if (!capture.playSeconds) {
        return std::unexpected(captureError(std::format("--play takes seconds, not \"{}\"", value)));
      }
    } else if (arg == "--settle-frames") {
      std::uint32_t frames = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
      if (error != std::errc{} || end != value.data() + value.size()) {
        return std::unexpected(captureError(std::format("--settle-frames takes a count, not \"{}\"", value)));
      }
      capture.settleFrames = frames;
    } else if (arg == "--shading-term") {
      capture.shadingTerm = shadingTerm(value);
      if (!capture.shadingTerm) {
        return std::unexpected(
            captureError(std::format("unknown shading term \"{}\"; the terms are {}", value, shadingTerms())));
      }
    } else {
      return std::unexpected(captureError(std::format("option {} is listed but not handled", arg)));
    }
  }
  if (!captureFlags || line.help) {
    return line;
  }
  if (!editor && sceneOnly) {
    line.scene = std::move(capture.scene);
    return line;
  }
  if (capture.viewport.empty() && capture.window.empty()) {
    return std::unexpected(captureError(editor ? "the capture options need --screenshot or --screenshot-window"
                                               : "the capture options need --screenshot"));
  }
  // The player without a game runs the one it ships with.
  if (editor && line.content.empty()) {
    return std::unexpected(captureError("a capture needs a project folder"));
  }
  line.capture = std::move(capture);
  return line;
}

void resolveOutputs(CaptureOptions &options, const std::filesystem::path &directory) {
  for (std::filesystem::path *file : {&options.viewport, &options.window}) {
    if (!file->empty() && file->is_relative()) {
      *file = directory / *file;
    }
  }
}

CaptureRun::CaptureRun(CaptureOptions options) : m_options(std::move(options)) {
}

CaptureRun::Status CaptureRun::fail(const core::Error &error) {
  SONNET_LOG_ERROR("capture failed: {}", error.toString());
  return Status::Failed;
}

CaptureRun::Status CaptureRun::step(ICaptureTarget &target) {
  switch (m_stage) {
  case Stage::Start:
    if (const auto begun = target.begin(m_options); !begun) {
      return fail(begun.error());
    }
    m_loadStart = std::chrono::steady_clock::now();
    m_stage = Stage::Settle;
    return Status::Running;
  case Stage::Settle:
    // Imports finish on workers and publish between frames, and the image the screenshot copies
    // exists only once a frame has drawn into it: the editor's viewport once its panel has been
    // laid out, the player's once a swapchain image was acquired.
    if (!target.loaded()) {
      m_frames = 0;
      if (std::chrono::steady_clock::now() - m_loadStart > LoadTimeout) {
        return fail(captureError(std::format("the assets were still loading after {}", LoadTimeout)));
      }
      return Status::Running;
    }
    if (++m_frames < m_options.settleFrames) {
      return Status::Running;
    }
    m_frames = 0;
    if (m_options.playSeconds) {
      target.play(RandomSeed);
      m_stage = Stage::Play;
      return Status::Running;
    }
    m_stage = Stage::Select;
    return step(target);
  case Stage::Play:
    if (static_cast<float>(++m_frames) * FrameSeconds < *m_options.playSeconds) {
      return Status::Running;
    }
    m_stage = Stage::Select;
    return step(target);
  case Stage::Select:
    // Some frames ahead of the capture: the first update after it turns the selection into the
    // outline, and the panels take a frame or two to lay out what it shows. Capturing on the
    // first frame photographed the inspector with every value a few pixels wide.
    m_frames = 0;
    m_stage = Stage::Capture;
    if (m_options.select.empty()) {
      return step(target);
    }
    if (const auto selected = target.select(m_options.select); !selected) {
      return fail(selected.error());
    }
    return Status::Running;
  case Stage::Capture:
    if (!m_options.select.empty() && ++m_frames < SelectionFrames) {
      return Status::Running;
    }
    target.requestScreenshots(m_options.viewport, m_options.window);
    m_stage = Stage::Write;
    return Status::Running;
  case Stage::Write:
    if (std::optional<core::Result<void>> written = target.takeScreenshotResult()) {
      return *written ? Status::Done : fail(written->error());
    }
    return Status::Running;
  }
  return Status::Running;
}

} // namespace sonnet::runtime
