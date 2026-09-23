#include <sonnet/editor/Capture.h>

#include "Screenshot.h"

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/editor/Editor.h>
#include <sonnet/world/Components.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <source_location>
#include <string>
#include <vector>

// stb_image_write is header-only; this is its one implementation in the engine. The macros are
// stb's names.
// NOLINTBEGIN(readability-identifier-naming)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
// NOLINTEND(readability-identifier-naming)
#include <stb_image_write.h>

namespace sonnet::editor {

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

// The first root of that name, then its descendants by World::findByPath.
flecs::entity findEntity(world::World &world, std::string_view path) {
  const std::size_t slash = path.find('/');
  const std::string_view rootName = path.substr(0, slash);
  for (const flecs::entity root : world.roots()) {
    const world::Name *name = root.try_get<world::Name>();
    if (name != nullptr && name->value == rootName) {
      return slash == std::string_view::npos ? root : world.findByPath(root, path.substr(slash + 1));
    }
  }
  return {};
}

} // namespace

namespace {

constexpr std::array<CommandLineOption, 7> Options{{
    {"--screenshot", "FILE", "the viewport's scene, at the viewport's size, as a PNG", "shots/view.png"},
    {"--screenshot-window", "FILE", "the whole editor window, with its panels, as a PNG", "shots/window.png"},
    {"--scene", "FILE", "open this scene, relative to the project, instead of its start scene",
     "scenes/playground.scene.json"},
    {"--play", "SECONDS", "play for this many seconds first (60 fixed steps a second) and capture while playing",
     "2.5"},
    {"--select", "PATH", "select the entity at this path of names, Parent/Child, so it is outlined", "Ball"},
    {"--shading-term", "TERM", "show one term of the forward shading instead of the final image", "sun-direct"},
    {"--settle-frames", "N", "frames drawn after the assets have loaded, before anything else (10)", "4"},
}};

} // namespace

std::span<const CommandLineOption> commandLineOptions() noexcept {
  return Options;
}

std::string commandLineUsage() {
  std::string usage = "usage: sonnet_editor [project] [capture flags]\n\n"
                      "Opens the editor, on the project folder when one is given. The capture flags instead\n"
                      "write screenshots and quit, for agents and scripts that cannot capture the screen\n"
                      "(docs/editor.md, \"Screenshots\"). A capture needs a project and --screenshot,\n"
                      "--screenshot-window or both.\n\n";
  std::size_t width = 0;
  for (const CommandLineOption &option : Options) {
    width = std::max(width, option.flag.size() + 1 + option.value.size());
  }
  for (const CommandLineOption &option : Options) {
    usage += std::format("  {:<{}}  {}\n", std::format("{} {}", option.flag, option.value), width, option.help);
  }
  usage += "\nShading terms:";
  for (std::uint32_t t = 0; t < renderer::DebugViewCount; ++t) {
    usage += (t == 0 ? " " : ", ") + flagName(renderer::debugViewName(static_cast<renderer::DebugView>(t)));
  }
  usage += ".\n\nThe exit code is 0 once every screenshot is written and 1 on any error, which the log\n"
           "explains. The same flags give the same image run after run.\n\n"
           "Example:\n"
           "  sonnet_editor apps/samples/basic --scene scenes/playground.scene.json --play 3 \\\n"
           "    --select Ball --screenshot shots/view.png --screenshot-window shots/window.png\n";
  return usage;
}

core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args) {
  CommandLine line;
  CaptureOptions capture;
  bool captureFlags = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      line.help = true;
      continue;
    }
    if (!arg.starts_with("--")) {
      if (!line.project.empty()) {
        return std::unexpected(captureError(std::format("a second project folder \"{}\"", arg)));
      }
      line.project = std::filesystem::path{arg};
      continue;
    }
    if (std::ranges::none_of(Options, [&](const CommandLineOption &option) { return option.flag == arg; })) {
      return std::unexpected(captureError(std::format("unknown option {}; --help lists them", arg)));
    }
    if (i + 1 == args.size()) {
      return std::unexpected(captureError(std::format("{} needs a value", arg)));
    }
    const std::string_view value = args[++i];
    captureFlags = true;
    if (arg == "--screenshot") {
      capture.viewport = std::filesystem::path{value};
    } else if (arg == "--screenshot-window") {
      capture.window = std::filesystem::path{value};
    } else if (arg == "--scene") {
      capture.scene = std::filesystem::path{value};
    } else if (arg == "--select") {
      capture.select = std::string{value};
    } else if (arg == "--play") {
      float seconds = 0.0f;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seconds);
      if (error != std::errc{} || end != value.data() + value.size() || !std::isfinite(seconds) || seconds < 0.0f) {
        return std::unexpected(captureError(std::format("--play takes seconds, not \"{}\"", value)));
      }
      capture.playSeconds = seconds;
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
        std::string terms;
        for (std::uint32_t t = 0; t < renderer::DebugViewCount; ++t) {
          terms += (t == 0 ? "" : ", ") + flagName(renderer::debugViewName(static_cast<renderer::DebugView>(t)));
        }
        return std::unexpected(
            captureError(std::format("unknown shading term \"{}\"; the terms are {}", value, terms)));
      }
    } else {
      return std::unexpected(captureError(std::format("option {} is listed but not handled", arg)));
    }
  }
  if (!captureFlags || line.help) {
    return line;
  }
  if (capture.viewport.empty() && capture.window.empty()) {
    return std::unexpected(captureError("the capture options need --screenshot or --screenshot-window"));
  }
  if (line.project.empty()) {
    return std::unexpected(captureError("a capture needs a project folder"));
  }
  line.capture = std::move(capture);
  return line;
}

CaptureRun::CaptureRun(CaptureOptions options) : m_options(std::move(options)) {
}

CaptureRun::Status CaptureRun::fail(const core::Error &error) {
  SONNET_LOG_ERROR("capture failed: {}", error.toString());
  return Status::Failed;
}

CaptureRun::Status CaptureRun::step(Editor &editor) {
  switch (m_stage) {
  case Stage::Start: {
    if (!editor.project()) {
      return fail(captureError("no project is open"));
    }
    if (!m_options.scene.empty()) {
      if (const auto opened = editor.openScene(editor.project()->root / m_options.scene); !opened) {
        return fail(opened.error());
      }
    }
    if (m_options.shadingTerm) {
      editor.setShadingTerm(*m_options.shadingTerm);
    }
    m_loadStart = std::chrono::steady_clock::now();
    m_stage = Stage::Settle;
    return Status::Running;
  }
  case Stage::Settle:
    // Imports finish on workers and publish between frames, and the viewport has a target only
    // once its panel has been laid out.
    if (!editor.viewport().target().isValid() || editor.assets().loading()) {
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
      editor.scripts().seedRandom(RandomSeed);
      editor.play();
      m_stage = Stage::Play;
      return Status::Running;
    }
    m_stage = Stage::Select;
    return step(editor);
  case Stage::Play:
    if (static_cast<float>(++m_frames) * FrameSeconds < *m_options.playSeconds) {
      return Status::Running;
    }
    m_stage = Stage::Select;
    return step(editor);
  case Stage::Select:
    // Some frames ahead of the capture: the first update after it turns the selection into the
    // outline, and the panels take a frame or two to lay out what it shows. Capturing on the
    // first frame photographed the inspector with every value a few pixels wide.
    m_frames = 0;
    m_stage = Stage::Capture;
    if (m_options.select.empty()) {
      return step(editor);
    }
    if (const flecs::entity entity = findEntity(editor.world(), m_options.select);
        entity && entity.has<world::Identity>()) {
      editor.selection().select(entity.get<world::Identity>().uuid);
      return Status::Running;
    }
    return fail(captureError(std::format("no entity at \"{}\" in the scene", m_options.select)));
  case Stage::Capture:
    if (!m_options.select.empty() && ++m_frames < SelectionFrames) {
      return Status::Running;
    }
    editor.requestScreenshots(m_options.viewport, m_options.window);
    m_stage = Stage::Write;
    return Status::Running;
  case Stage::Write:
    if (std::optional<core::Result<void>> written = editor.takeScreenshotResult()) {
      return *written ? Status::Done : fail(written->error());
    }
    return Status::Running;
  }
  return Status::Running;
}

core::Result<void> writeScreenshot(const std::filesystem::path &file, glm::uvec2 size, rhi::Format format,
                                   std::span<const std::byte> pixels) {
  const std::size_t count = std::size_t{size.x} * size.y;
  if (pixels.size() < count * 4) {
    return std::unexpected(core::Error{"the screenshot's readback is smaller than its image", core::ErrorCategory::Io});
  }
  const bool bgra = format == rhi::Format::B8G8R8A8Unorm || format == rhi::Format::B8G8R8A8Srgb;
  std::vector<unsigned char> rgba(count * 4);
  for (std::size_t i = 0; i < count; ++i) {
    const auto channel = [&](std::size_t c) { return std::to_integer<unsigned char>(pixels[i * 4 + c]); };
    rgba[i * 4 + 0] = channel(bgra ? 2 : 0);
    rgba[i * 4 + 1] = channel(1);
    rgba[i * 4 + 2] = channel(bgra ? 0 : 2);
    rgba[i * 4 + 3] = 255;
  }
  std::vector<std::byte> png;
  const auto append = [](void *context, void *data, int length) {
    auto *out = static_cast<std::vector<std::byte> *>(context);
    const auto *bytes = static_cast<const std::byte *>(data);
    out->insert(out->end(), bytes, bytes + length);
  };
  if (stbi_write_png_to_func(append, &png, static_cast<int>(size.x), static_cast<int>(size.y), 4, rgba.data(),
                             static_cast<int>(size.x) * 4) == 0) {
    return std::unexpected(core::Error{"encoding the screenshot as PNG failed", core::ErrorCategory::Io});
  }
  return core::writeFile(file, png);
}

} // namespace sonnet::editor
