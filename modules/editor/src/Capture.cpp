#include <sonnet/editor/Capture.h>

#include <sonnet/editor/Editor.h>
#include <sonnet/world/Components.h>

#include <format>
#include <source_location>
#include <string>
#include <utility>

namespace sonnet::editor {

namespace {

// At the caller's location, as an Error created there would be.
core::Error captureError(std::string message, std::source_location location = std::source_location::current()) {
  return core::Error{std::move(message), core::ErrorCategory::Unknown, location};
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

// The editor as a capture steps through it: the viewport is the scene, and the selection and
// the window with its panels are the editor's own.
class EditorCaptureTarget final : public runtime::ICaptureTarget {
public:
  explicit EditorCaptureTarget(Editor &editor) : m_editor(editor) {
  }

  core::Result<void> begin(const CaptureOptions &options) override {
    if (!m_editor.project()) {
      return std::unexpected(captureError("no project is open"));
    }
    if (!options.scene.empty()) {
      if (const auto opened = m_editor.openScene(m_editor.project()->root / options.scene); !opened) {
        return opened;
      }
    }
    if (options.shadingTerm) {
      m_editor.setShadingTerm(*options.shadingTerm);
    }
    return {};
  }

  bool loaded() override {
    // The viewport has a target only once its panel has been laid out.
    return m_editor.viewport().target().isValid() && !m_editor.assets().loading();
  }

  void play(std::uint64_t randomSeed) override {
    m_editor.scripts().seedRandom(randomSeed);
    m_editor.play();
  }

  core::Result<void> select(std::string_view path) override {
    if (const flecs::entity entity = findEntity(m_editor.world(), path); entity && entity.has<world::Identity>()) {
      m_editor.selection().select(entity.get<world::Identity>().uuid);
      return {};
    }
    return std::unexpected(captureError(std::format("no entity at \"{}\" in the scene", path)));
  }

  void requestScreenshots(const std::filesystem::path &viewport, const std::filesystem::path &window) override {
    m_editor.requestScreenshots(viewport, window);
  }

  std::optional<core::Result<void>> takeScreenshotResult() override {
    return m_editor.takeScreenshotResult();
  }

private:
  Editor &m_editor;
};

} // namespace

std::span<const CommandLineOption> commandLineOptions() noexcept {
  return runtime::commandLineOptions();
}

std::string commandLineUsage() {
  return runtime::commandLineUsage(runtime::CaptureApplication::Editor);
}

core::Result<CommandLine> parseCommandLine(std::span<const std::string_view> args) {
  auto line = runtime::parseCommandLine(args, runtime::CaptureApplication::Editor);
  if (!line) {
    return std::unexpected(line.error());
  }
  return CommandLine{.project = std::move(line->content), .capture = std::move(line->capture), .help = line->help};
}

CaptureRun::CaptureRun(CaptureOptions options) : m_run(std::move(options)) {
}

CaptureRun::Status CaptureRun::step(Editor &editor) {
  EditorCaptureTarget target{editor};
  return m_run.step(target);
}

} // namespace sonnet::editor
