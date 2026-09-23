#include <sonnet/editor/Capture.h>
#include <sonnet/editor/Editor.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <stb_image.h>

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;

namespace {

core::Result<editor::CommandLine> parse(std::initializer_list<std::string_view> args) {
  const std::vector<std::string_view> list{args};
  return editor::parseCommandLine(list);
}

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

  // Frames at the capture's fixed step, as the application runs them, until the run ends.
  editor::CaptureRun::Status run(editor::Editor &editor, editor::CaptureRun &capture) {
    editor::CaptureRun::Status status = editor::CaptureRun::Status::Running;
    for (int frame = 0; frame < 600 && status == editor::CaptureRun::Status::Running; ++frame) {
      editor.update(editor::CaptureRun::FrameSeconds);
      rhi::ICommandList &commands = device->beginFrame();
      const auto image = swapchain->acquire();
      REQUIRE(image.has_value());
      editor.render(commands, image);
      device->endFrame();
      editor.afterPresent();
      status = capture.step(editor);
    }
    // No statement after a FAIL: MSVC reads it as unreachable, which is an error there.
    REQUIRE(status != editor::CaptureRun::Status::Running); // finished within 600 frames
    return status;
  }
};

struct Image {
  int width{0};
  int height{0};
  std::vector<unsigned char> rgba;

  [[nodiscard]] std::array<int, 3> at(int x, int y) const {
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2]};
  }
};

Image readPng(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  Image image;
  int channels = 0;
  unsigned char *pixels =
      stbi_load_from_memory(reinterpret_cast<const unsigned char *>(bytes->data()), static_cast<int>(bytes->size()),
                            &image.width, &image.height, &channels, 4);
  REQUIRE(pixels != nullptr);
  image.rgba.assign(pixels,
                    pixels + static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
  stbi_image_free(pixels);
  return image;
}

} // namespace

TEST_CASE("the editor's arguments are a project folder and the capture flags", "[editor][capture]") {
  SECTION("a project folder alone is the interactive editor") {
    const auto line = parse({"apps/samples/basic"});
    REQUIRE(line.has_value());
    REQUIRE(line->project == "apps/samples/basic");
    REQUIRE(!line->capture.has_value());
    REQUIRE(parse({}).has_value());
  }
  SECTION("every capture flag") {
    const auto line = parse({"apps/samples/basic", "--scene", "scenes/playground.scene.json", "--play", "2.5",
                             "--select", "Ball/Light", "--shading-term", "sun-direct", "--settle-frames", "4",
                             "--screenshot", "out/viewport.png", "--screenshot-window", "out/window.png"});
    REQUIRE(line.has_value());
    REQUIRE(line->capture.has_value());
    const editor::CaptureOptions &capture = *line->capture;
    REQUIRE(capture.scene == "scenes/playground.scene.json");
    REQUIRE(capture.playSeconds == 2.5f);
    REQUIRE(capture.select == "Ball/Light");
    REQUIRE(capture.shadingTerm == renderer::DebugView::SunDirect);
    REQUIRE(capture.settleFrames == 4);
    REQUIRE(capture.viewport == "out/viewport.png");
    REQUIRE(capture.window == "out/window.png");
  }
  SECTION("shading terms are the menu's names in any case, spaces or hyphens") {
    for (const std::string_view term : {"BRDF LUT", "brdf-lut", "Brdf-Lut"}) {
      const auto line = parse({"p", "--shading-term", term, "--screenshot", "a.png"});
      REQUIRE(line.has_value());
      REQUIRE(line->capture->shadingTerm == renderer::DebugView::BrdfLut);
    }
  }
  SECTION("mistakes are errors, not a silently different run") {
    REQUIRE(!parse({"p", "--screenshot"}).has_value());                               // no value
    REQUIRE(!parse({"p", "--frobnicate", "1", "--screenshot", "a.png"}).has_value()); // unknown
    REQUIRE(!parse({"p", "--play", "soon", "--screenshot", "a.png"}).has_value());
    REQUIRE(!parse({"p", "--play", "-1", "--screenshot", "a.png"}).has_value());
    REQUIRE(!parse({"p", "--settle-frames", "2x", "--screenshot", "a.png"}).has_value());
    REQUIRE(!parse({"p", "--shading-term", "sparkle", "--screenshot", "a.png"}).has_value());
    REQUIRE(!parse({"p", "--play", "1"}).has_value());                // nothing to capture
    REQUIRE(!parse({"--screenshot", "a.png"}).has_value());           // no project
    REQUIRE(!parse({"p", "q", "--screenshot", "a.png"}).has_value()); // two projects
  }
}

TEST_CASE("--help lists every capture flag, and every listed flag parses", "[editor][capture]") {
  for (const std::string_view help : {"--help", "-h"}) {
    const auto line = parse({help});
    REQUIRE(line.has_value());
    REQUIRE(line->help);
  }
  const std::string usage = editor::commandLineUsage();
  REQUIRE(usage.starts_with("usage: sonnet_editor"));
  REQUIRE(usage.contains("docs/editor.md"));
  for (const editor::CommandLineOption &option : editor::commandLineOptions()) {
    CAPTURE(option.flag);
    REQUIRE(usage.contains(option.flag));
    REQUIRE(!option.help.empty());
    // Every flag the usage offers is one the parser takes, with the value it shows.
    const auto line = parse({"p", option.flag, option.example, "--screenshot", "a.png"});
    REQUIRE(line.has_value());
    REQUIRE(line->capture.has_value());
  }
  for (std::uint32_t t = 0; t < renderer::DebugViewCount; ++t) {
    const auto view = static_cast<renderer::DebugView>(t);
    std::string term{renderer::debugViewName(view)};
    std::ranges::transform(term, term.begin(), [](char c) {
      return c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    CAPTURE(term);
    REQUIRE(usage.contains(term));
    REQUIRE(parse({"p", "--shading-term", term, "--screenshot", "a.png"})->capture->shadingTerm == view);
  }
  // A flag that is not in the table points at the help.
  const auto unknown = parse({"p", "--screenshots", "a.png"});
  REQUIRE(!unknown.has_value());
  REQUIRE(unknown.error().message.contains("--help"));
}

TEST_CASE("a capture writes the viewport and the whole window as PNG on a GPU", "[editor][capture][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "capture";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Capture").has_value());
    REQUIRE(fixture.swapchain->readable());
    editor::CaptureOptions options;
    options.playSeconds = 0.25f;
    options.select = "Box";
    options.viewport = directory / "shots" / "viewport.png";
    options.window = directory / "shots" / "window.png";
    options.settleFrames = 2;
    editor::CaptureRun capture{options};
    REQUIRE(fixture.run(editor, capture) == editor::CaptureRun::Status::Done);
    REQUIRE(editor.isPlaying()); // captured while playing, as asked
    REQUIRE(editor.outlineIds().size() == 1);
    // Captured once the panels had laid out the selection: the inspector's value columns hold what
    // their labels leave, where the frame right after selecting still had them a few pixels wide.
    ImGuiContext &context = *ImGui::GetCurrentContext();
    int members = 0;
    for (int i = 0; i < context.Tables.GetMapSize(); ++i) {
      const ImGuiTable *table = context.Tables.TryGetMapData(i);
      if (table == nullptr || table->ColumnsCount != 2 || table->LastFrameActive < context.FrameCount - 1 ||
          !std::string_view{table->OuterWindow->Name}.starts_with("Inspector")) {
        continue;
      }
      ++members;
      // All of it but the spacing between cells, which the frames right after selecting fall short of.
      CAPTURE(table->OuterRect.GetWidth(), table->Columns[1].WidthGiven);
      REQUIRE(table->Columns[1].WidthGiven >= table->OuterRect.GetWidth() - table->Columns[0].WidthGiven - 16.0f);
    }
    REQUIRE(members >= 3); // the box's Transform

    const Image viewport = readPng(directory / "shots" / "viewport.png");
    const glm::uvec2 target = editor.viewport().target().size();
    REQUIRE(viewport.width == static_cast<int>(target.x));
    REQUIRE(viewport.height == static_cast<int>(target.y));
    const Image window = readPng(directory / "shots" / "window.png");
    REQUIRE(window.width == static_cast<int>(fixture.swapchain->extent().x));
    REQUIRE(window.height == static_cast<int>(fixture.swapchain->extent().y));
    // The starter scene fills the viewport's middle with something lit, not the clear colour, and
    // the window holds the panels' colours around it.
    const auto centre = viewport.at(viewport.width / 2, viewport.height / 2);
    REQUIRE(centre[0] + centre[1] + centre[2] > 30);
    std::vector<std::array<int, 3>> colours;
    for (int y = 0; y < window.height; y += 7) {
      for (int x = 0; x < window.width; x += 7) {
        colours.push_back(window.at(x, y));
      }
    }
    std::ranges::sort(colours);
    REQUIRE(std::ranges::unique(colours).begin() - colours.begin() > 10);
  }
  REQUIRE(fixture.device->validationMessageCount() == 0); // the copies out of the swapchain too
  std::filesystem::remove_all(directory);
}

TEST_CASE("a capture of an entity that is not there fails without writing", "[editor][capture][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "capture_missing";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Capture").has_value());
    editor::CaptureOptions options;
    options.select = "Box/Lid";
    options.viewport = directory / "shots" / "viewport.png";
    options.settleFrames = 1;
    editor::CaptureRun capture{options};
    REQUIRE(fixture.run(editor, capture) == editor::CaptureRun::Status::Failed);
    REQUIRE(!std::filesystem::exists(directory / "shots" / "viewport.png"));
  }
  std::filesystem::remove_all(directory);
}
