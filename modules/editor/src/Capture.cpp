#include <sonnet/editor/Capture.h>

#include <sonnet/editor/Editor.h>
#include <sonnet/world/Components.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

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
  EditorCaptureTarget(Editor &editor, bool viewportShot) : m_editor(editor), m_viewportShot(viewportShot) {
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
    if (options.renderSize) {
      return std::unexpected(captureError("--render-size is the player's: the editor's viewport is its panel's size"));
    }
    if (options.transmission) {
      return std::unexpected(captureError("--transmission is the player's: the editor always draws it"));
    }
    m_editor.setCaptureGrid(options.grid);
    if (options.assetsGrid || options.tileSize) {
      AssetsView view = m_editor.assetBrowser().view();
      view.grid = options.assetsGrid.value_or(view.grid);
      if (options.tileSize) {
        const auto size = static_cast<float>(*options.tileSize);
        if (size < AssetsView::MinTileSize || size > AssetsView::MaxTileSize) {
          return std::unexpected(captureError(std::format("--tile-size takes {} to {}, not {}", AssetsView::MinTileSize,
                                                          AssetsView::MaxTileSize, *options.tileSize)));
        }
        view.tileSize = size;
      }
      m_editor.assetBrowser().setView(view); // not through the view handler, which saves the preferences
    }
    if (!options.front.empty()) {
      if (const auto front = m_editor.bringToFront(options.front); !front) {
        return front;
      }
    }
    if (!options.dialog.empty()) {
      if (const auto opened = m_editor.openDialog(options.dialog); !opened) {
        return opened;
      }
    }
    if (options.shadingTerm) {
      m_editor.setShadingTerm(*options.shadingTerm);
    }
    return {};
  }

  std::vector<std::string> waitingFor() override {
    std::vector<std::string> waiting;
    // A view has a target only once its panel has been laid out. --screenshot copies the Viewport's; the window needs
    // the scene view in front to have drawn, which is the Game view's under --front game: the default layout tabs
    // the two together, so the Viewport behind it never makes a target (#516).
    const bool viewport = m_editor.viewport().target().isValid();
    if (m_viewportShot && !viewport) {
      waiting.emplace_back("the Viewport's first frame");
    } else if (!viewport && !m_editor.gamePanel().target().isValid()) {
      waiting.emplace_back("the first frame of the Viewport or the Game view");
    }
    std::ranges::move(runtime::pendingAssetNames(m_editor.assets()), std::back_inserter(waiting));
    for (const core::Uuid &uuid : m_editor.thumbnails().busyAssets()) {
      const assets::AssetInfo *info = m_editor.assets().find(uuid);
      const flecs::entity prefab = info == nullptr ? m_editor.world().find(uuid) : flecs::entity{};
      const world::Name *name = prefab ? prefab.try_get<world::Name>() : nullptr;
      waiting.push_back(std::format("the thumbnail of {}", info != nullptr   ? info->name
                                                           : name != nullptr ? name->value
                                                                             : uuid.toString()));
    }
    return waiting;
  }

  void cutHistory() override {
    m_editor.cutHistory();
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
  bool m_viewportShot;
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

CaptureRun::CaptureRun(CaptureOptions options) : m_viewportShot(!options.viewport.empty()), m_run(std::move(options)) {
}

CaptureRun::Status CaptureRun::step(Editor &editor) {
  EditorCaptureTarget target{editor, m_viewportShot};
  return m_run.step(target);
}

} // namespace sonnet::editor
