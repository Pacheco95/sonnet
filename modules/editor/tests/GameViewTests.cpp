#include <sonnet/editor/Editor.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/DrawList.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <spdlog/sinks/base_sink.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

struct Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<platform::IWindow> window;
  std::unique_ptr<rhi::IDevice> device;
  std::unique_ptr<rhi::ISwapchain> swapchain;

  Fixture() {
    try {
      window = platform.createWindow({.title = "editor_tests", .size = {800, 600}});
      device = rhi::createDevice({.platform = &platform, .applicationName = "editor_tests"});
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

  // Shows the Game panel docked to the right of the Viewport, as a user splits them.
  void showGame(editor::Editor &editor) {
    editor.setShowGame(true);
    for (int i = 0; i < 3; ++i) {
      frame(editor);
    }
    const ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");
    REQUIRE(viewport != nullptr);
    ImGuiID centre = viewport->DockId;
    const ImGuiID root = ImGui::DockNodeGetRootNode(ImGui::DockBuilderGetNode(centre))->ID;
    const ImGuiID side = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.5f, nullptr, &centre);
    ImGui::DockBuilderDockWindow("Game", side);
    ImGui::DockBuilderFinish(root);
    for (int i = 0; i < 3; ++i) {
      frame(editor);
    }
  }
};

flecs::entity byName(world::World &world, std::string_view name) {
  for (const flecs::entity root : world.roots()) {
    if (root.get<world::Name>().value == name) {
      return root;
    }
  }
  FAIL("no entity " << name);
}

class ProblemSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  std::vector<std::string> problems;

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    if (message.level >= spdlog::level::warn) {
      problems.emplace_back(message.payload.data(), message.payload.size());
    }
  }
  void flush_() override {
  }
};

} // namespace

TEST_CASE("the Game view draws through the scene camera beside the Scene view", "[editor][gpu][game-view]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "game_view";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "GameView").has_value());
    const glm::vec3 scenePosition = byName(editor.world(), "Camera").get<world::Transform>().position;

    // Closed, the panel costs the renderer no second view.
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE_FALSE(editor.gamePanel().target().isValid());
    REQUIRE(editor.renderer().statistics(1).drawCount == 0);
    const glm::uvec2 viewportSize = editor.viewport().target().size();
    REQUIRE(viewportSize.x > 0);

    // The editor's camera starts where the starter scene's is; move it so the two differ.
    editor.viewport().camera().lookAt({-9.0f, 12.0f, -9.0f}, {0.0f, 0.0f, 0.0f});
    fixture.showGame(editor);
    REQUIRE(editor.gamePanel().target().isValid());
    REQUIRE(editor.viewport().target().isValid()); // the Scene view keeps its own target
    const renderer::RenderStatistics &sceneStatistics = editor.renderer().statistics(0);
    const renderer::RenderStatistics &gameStatistics = editor.renderer().statistics(1);
    REQUIRE(sceneStatistics.drawCount > 0);
    REQUIRE(gameStatistics.drawCount == sceneStatistics.drawCount);
    // Both views resolve against a history of their own (ADR-0024).
    REQUIRE(sceneStatistics.temporalHistoryUsed);
    REQUIRE(gameStatistics.temporalHistoryUsed);

    // Not playing, scripts and the listener follow the editor's camera over the Scene view.
    REQUIRE(editor.scriptView().camera.position == editor.viewport().camera().camera().position);
    REQUIRE(editor.scriptView().size == editor.viewport().input().size);

    // Playing, they follow the scene's camera over the Game panel's image.
    editor.play();
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(editor.scriptView().camera.position.x == Approx(scenePosition.x));
    REQUIRE(editor.scriptView().camera.position.y == Approx(scenePosition.y));
    REQUIRE(editor.scriptView().camera.position.z == Approx(scenePosition.z));
    REQUIRE(editor.scriptView().size == editor.gamePanel().input().size);
    REQUIRE(editor.scriptView().camera.position != editor.viewport().camera().camera().position);

    // Closing the panel hands them back and drops the second view.
    editor.setShowGame(false);
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE_FALSE(editor.gamePanel().target().isValid());
    REQUIRE(editor.renderer().statistics(1).drawCount == 0);
    REQUIRE(editor.scriptView().camera.position == editor.viewport().camera().camera().position);
    // The Scene view went on resolving once its panel had settled at its new size; the Game view,
    // absent for frames, starts over.
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(editor.renderer().statistics(0).temporalHistoryUsed);
    editor.setShowGame(true);
    for (int frame = 0; frame < 8 && editor.renderer().statistics(1).drawCount == 0; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(editor.renderer().statistics(1).drawCount > 0);
    REQUIRE_FALSE(editor.renderer().statistics(1).temporalHistoryUsed);
    fixture.frame(editor);
    REQUIRE(editor.renderer().statistics(1).temporalHistoryUsed);
    editor.stop();
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

// The mode View > Anti-aliasing sets.
TEST_CASE("the anti-aliasing mode can be switched while the editor runs", "[editor][gpu][game-view]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "aa_menu";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "AaMenu").has_value());
    REQUIRE(editor.renderer().settings().antialiasing == renderer::AntiAliasing::Taa);
    for (int frame = 0; frame < 4; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(editor.renderer().statistics(0).temporalHistoryUsed);
    for (const renderer::AntiAliasing mode :
         {renderer::AntiAliasing::Fxaa, renderer::AntiAliasing::None, renderer::AntiAliasing::Taa}) {
      editor.setAntiAliasing(mode);
      fixture.frame(editor);
      // The first frame of TAA has no history; the next resolves against it. The others never do.
      REQUIRE_FALSE(editor.renderer().statistics(0).temporalHistoryUsed);
      fixture.frame(editor);
      fixture.frame(editor);
      REQUIRE(editor.renderer().statistics(0).temporalHistoryUsed == (mode == renderer::AntiAliasing::Taa));
      REQUIRE(editor.renderer().statistics(0).drawCount > 0);
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("a scene without a camera shows the fallback view in the Game panel, with one warning",
          "[editor][gpu][game-view]") {
  Fixture fixture;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "game_view_fallback";
  std::filesystem::remove_all(directory);
  const auto sink = std::make_shared<ProblemSink>();
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "GameViewFallback").has_value());
    byName(editor.world(), "Camera").destruct();
    core::Log::addSink(sink);
    // Nothing is warned about while the panel is closed.
    fixture.frame(editor);
    REQUIRE(sink->problems.empty());

    fixture.showGame(editor);
    editor.play();
    for (int frame = 0; frame < 5; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(editor.renderer().statistics(1).drawCount > 0);
    REQUIRE(editor.scriptView().camera.position == world::fallbackCamera().position);
    REQUIRE(sink->problems.size() == 1);
    REQUIRE(sink->problems[0].find("no Camera") != std::string::npos);
    core::Log::removeSink(sink);
    editor.stop();
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("an emitter previews while it is selected and always plays in play mode", "[editor][gpu][particles]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "particles";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Particles").has_value());
    const flecs::entity sparks = editor.world().createEntity("Sparks");
    sparks.set<world::Transform>({.position = {0.0f, 1.0f, 0.0f}});
    sparks.set<world::ParticleEmitter>({.burst = 20});
    const auto emitters = [&] { return editor.renderer().statistics(0).particleEmitterCount; };

    // Unselected in edit mode, the scene is still: no emitter reaches the renderer at all.
    editor.selection().clear();
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(emitters() == 0);

    // Selected, it previews.
    editor.selection().select(editor.world().uuidOf(sparks));
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(emitters() == 1);
    REQUIRE(editor.renderer().statistics(0).particleSlotCount == 1000);

    // Playing, it is there whether selected or not.
    editor.selection().clear();
    editor.play();
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(emitters() == 1);
    editor.stop();
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(emitters() == 0);
  }
  std::filesystem::remove_all(directory);
}
