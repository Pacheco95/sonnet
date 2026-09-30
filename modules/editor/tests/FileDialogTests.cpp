#include <sonnet/editor/Editor.h>
#include <sonnet/editor/FileDialog.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

using namespace sonnet;
using editor::FileDialogMode;
using editor::FileDialogRequest;
using editor::FileDialogResult;

namespace {

// Holds the request and the callback until the test decides how the user answered.
class FakeBackend final : public editor::IFileDialogBackend {
public:
  struct State {
    bool available{true};
    int shown{0};
    FileDialogRequest last;
    std::function<void(FileDialogResult)> done;
  };

  explicit FakeBackend(std::shared_ptr<State> state) : m_state(std::move(state)) {
  }
  [[nodiscard]] bool available() const override {
    return m_state->available;
  }
  void show(const FileDialogRequest &request, std::function<void(FileDialogResult)> done) override {
    ++m_state->shown;
    m_state->last = request;
    m_state->done = std::move(done);
  }

private:
  std::shared_ptr<State> m_state;
};

FileDialogResult pick(const char *path) {
  return {.kind = FileDialogResult::Kind::Picked, .path = path};
}

} // namespace

TEST_CASE("the file dialog hands a pick, a cancel and an error to the main thread once", "[editor][filedialog]") {
  auto state = std::make_shared<FakeBackend::State>();
  editor::FileDialog dialog{std::make_unique<FakeBackend>(state)};
  REQUIRE_FALSE(dialog.busy());
  REQUIRE_FALSE(dialog.poll().has_value());

  REQUIRE(dialog.open({.mode = FileDialogMode::Folder}));
  REQUIRE(dialog.busy());
  REQUIRE_FALSE(dialog.poll().has_value()); // still waiting for the user

  // From another thread, as SDL does it.
  std::thread{[&] { state->done(pick("/tmp/chosen")); }}.join();
  const auto picked = dialog.poll();
  REQUIRE(picked.has_value());
  CHECK(picked->kind == FileDialogResult::Kind::Picked);
  CHECK(picked->path == std::filesystem::path{"/tmp/chosen"});
  CHECK_FALSE(dialog.poll().has_value());
  CHECK_FALSE(dialog.busy());

  REQUIRE(dialog.open({.mode = FileDialogMode::SaveFile}));
  state->done({.kind = FileDialogResult::Kind::Cancelled});
  CHECK(dialog.poll()->kind == FileDialogResult::Kind::Cancelled);

  REQUIRE(dialog.open({.mode = FileDialogMode::OpenFile}));
  state->done({.kind = FileDialogResult::Kind::Failed, .error = "no portal"});
  const auto failed = dialog.poll();
  REQUIRE(failed.has_value());
  CHECK(failed->kind == FileDialogResult::Kind::Failed);
  CHECK(failed->error == "no portal");
}

TEST_CASE("a second dialog while one is open is refused, and an unavailable backend opens none",
          "[editor][filedialog]") {
  auto state = std::make_shared<FakeBackend::State>();
  editor::FileDialog dialog{std::make_unique<FakeBackend>(state)};
  REQUIRE(dialog.open({}));
  CHECK_FALSE(dialog.open({}));
  CHECK(state->shown == 1);

  state->available = false;
  state->done(pick("/x"));
  REQUIRE(dialog.poll().has_value());
  CHECK_FALSE(dialog.open({}));
  CHECK(state->shown == 1);
  CHECK_FALSE(dialog.busy());

  editor::FileDialog none{editor::makeNoFileDialogBackend()};
  CHECK_FALSE(none.available());
  CHECK_FALSE(none.open({}));
}

TEST_CASE("the result may arrive after the dialog object is gone", "[editor][filedialog]") {
  auto state = std::make_shared<FakeBackend::State>();
  {
    editor::FileDialog dialog{std::make_unique<FakeBackend>(state)};
    REQUIRE(dialog.open({}));
  }
  state->done(pick("/late")); // must not touch freed memory
}

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "file_dialog_tests", .size = {800, 600}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "file_dialog_tests"});
    } catch (const core::Exception &e) {
      SKIP("no usable Vulkan 1.4 device: " << e.what());
    }
    const rhi::DeviceInfo &info = device->info();
    if (info.loaderVersion < VK_API_VERSION_1_4 && info.driverName != "llvmpipe") {
      SKIP("headless surfaces are not trusted on this loader and driver");
    }
    try {
      swapchain = device->createSwapchain(*window);
    } catch (const core::Exception &e) {
      SKIP("headless surfaces are not supported here: " << e.what());
    }
  }

  void frame(editor::Editor &editor) {
    editor.update(1.0f / 60.0f);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = swapchain->acquire();
    REQUIRE(image.has_value());
    editor.render(commands, image);
    device->endFrame();
    editor.afterPresent();
  }
};

} // namespace

TEST_CASE("Browse fills the modal's path from a pick and leaves it alone on cancel or error",
          "[editor][filedialog][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  auto state = std::make_shared<FakeBackend::State>();
  editor.setFileDialogBackend(std::make_unique<FakeBackend>(state));

  editor.showOpenProjectModal();
  fixture.frame(editor);
  const std::string typed = editor.modalPath();
  REQUIRE(editor.pathModalOpen());

  editor.browseModalPath();
  REQUIRE(state->shown == 1);
  CHECK(state->last.mode == FileDialogMode::Folder);
  editor.browseModalPath(); // a second Browse while one is open does nothing
  CHECK(state->shown == 1);

  state->done({.kind = FileDialogResult::Kind::Cancelled});
  fixture.frame(editor);
  CHECK(editor.modalPath() == typed);
  CHECK(editor.modalError().empty());

  editor.browseModalPath();
  REQUIRE(state->shown == 2);
  state->done({.kind = FileDialogResult::Kind::Failed, .error = "no portal"});
  fixture.frame(editor);
  CHECK(editor.modalPath() == typed);
  CHECK(editor.modalError().find("no portal") != std::string::npos);

  editor.browseModalPath();
  REQUIRE(state->shown == 3);
  state->done(pick("/tmp/some/project"));
  fixture.frame(editor);
  CHECK(editor.modalPath() == "/tmp/some/project");
  CHECK(editor.modalError().empty());
}

TEST_CASE("Browse without a chooser shows an error and keeps the typed path", "[editor][filedialog][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  // A headless editor has no chooser of its own.
  editor.showOpenProjectModal();
  fixture.frame(editor);
  const std::string typed = editor.modalPath();
  editor.browseModalPath();
  CHECK(editor.modalPath() == typed);
  CHECK_FALSE(editor.modalError().empty());
}

TEST_CASE("saving an untitled scene opens the save chooser and saves to the pick", "[editor][filedialog][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  auto state = std::make_shared<FakeBackend::State>();
  editor.setFileDialogBackend(std::make_unique<FakeBackend>(state));
  REQUIRE(editor.scenePath().empty());

  editor.saveSceneOrChoose();
  REQUIRE(state->shown == 1);
  CHECK(state->last.mode == FileDialogMode::SaveFile);
  CHECK(state->last.filterPattern == "scene.json");

  state->done({.kind = FileDialogResult::Kind::Cancelled});
  fixture.frame(editor);
  CHECK(editor.scenePath().empty());

  const std::filesystem::path file = std::filesystem::temp_directory_path() / "sonnet_file_dialog" / "a.scene.json";
  std::filesystem::remove_all(file.parent_path());
  editor.saveSceneOrChoose();
  REQUIRE(state->shown == 2);
  state->done({.kind = FileDialogResult::Kind::Picked, .path = file});
  fixture.frame(editor);
  CHECK(editor.scenePath() == file);
  CHECK(std::filesystem::exists(file));

  // With a file the scene saves without asking.
  editor.saveSceneOrChoose();
  CHECK(state->shown == 2);
}

TEST_CASE("Open scene... loads the picked file like the asset browser does", "[editor][filedialog][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "sonnet_file_dialog" / "b.scene.json";
  std::filesystem::remove_all(file.parent_path());
  REQUIRE(editor.saveSceneAs(file));
  editor.newScene();
  REQUIRE(editor.tabCount() == 2);

  auto state = std::make_shared<FakeBackend::State>();
  editor.setFileDialogBackend(std::make_unique<FakeBackend>(state));
  editor.chooseSceneToOpen();
  REQUIRE(state->shown == 1);
  CHECK(state->last.mode == FileDialogMode::OpenFile);
  state->done({.kind = FileDialogResult::Kind::Picked, .path = file});
  fixture.frame(editor);
  CHECK(editor.scenePath() == std::filesystem::absolute(file).lexically_normal());
  CHECK(editor.tabCount() == 2); // the tab that already had it
}
