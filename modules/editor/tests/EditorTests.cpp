#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/Editor.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/Export.h>
#include <sonnet/editor/Layout.h>
#include <sonnet/editor/Preferences.h>

#include <sonnet/assets/Thumbnail.h>
#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/runtime/Screenshot.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/Scene.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <regex>
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

  explicit Fixture(glm::uvec2 size = {800, 600}) {
    try {
      window = platform.createWindow({.title = "editor_tests", .size = size});
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

  void frame(editor::Editor &editor, bool withImage = true) {
    editor.update(1.0f / 60.0f);
    rhi::ICommandList &commands = device->beginFrame();
    const auto image = withImage ? swapchain->acquire() : std::nullopt;
    if (withImage) {
      REQUIRE(image.has_value());
    }
    editor.render(commands, image);
    device->endFrame();
    editor.afterPresent();
  }
};

std::filesystem::path scratch(const char *name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path.parent_path());
  return path;
}

} // namespace

TEST_CASE("the editor runs frames headless without validation errors", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.world().roots().size() == 4); // the starter scene
    REQUIRE(!editor.isDirty());
    for (int frame = 0; frame < 4; ++frame) {
      editor.event(platform::MouseMoved{{10.0f, 10.0f}, {1.0f, 0.0f}});
      fixture.frame(editor);
      REQUIRE(!editor.quitRequested());
    }
    // A frame without a swapchain image (minimised window) still records the scene.
    fixture.frame(editor, false);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("the panel layout is kept across sessions and resets to the default", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path file = scratch("layout") / "layout.ini";
  std::filesystem::create_directories(file.parent_path());
  const auto dockOf = [](const char *name) {
    const ImGuiWindow *window = ImGui::FindWindowByName(name);
    REQUIRE(window != nullptr);
    return window->DockId;
  };
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(file);
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    // The default docks the assets and the log as tabs of one node; the user splits them.
    REQUIRE(dockOf("Assets") == dockOf("Log"));
    ImGuiID log = dockOf("Log");
    ImGuiDockNode *root = ImGui::DockNodeGetRootNode(ImGui::DockBuilderGetNode(log));
    const ImGuiID rootId = root->ID;
    const ImGuiID side = ImGui::DockBuilderSplitNode(log, ImGuiDir_Right, 0.5f, nullptr, &log);
    ImGui::DockBuilderDockWindow("Assets", side);
    ImGui::DockBuilderFinish(rootId);
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(dockOf("Assets") != dockOf("Log"));
  }
  REQUIRE(std::filesystem::exists(file));
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(file);
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(dockOf("Assets") != dockOf("Log"));
    editor.resetLayout();
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(dockOf("Assets") == dockOf("Log"));
  }
  {
    // Without a layout file the default is built, as before.
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(dockOf("Assets") == dockOf("Log"));
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

namespace {

ImGuiID dockOf(const char *name) {
  const ImGuiWindow *window = ImGui::FindWindowByName(name);
  REQUIRE(window != nullptr);
  return window->DockId;
}

ImVec2 dockFraction(const char *name) {
  ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockOf(name));
  const ImGuiDockNode *root = ImGui::DockNodeGetRootNode(node);
  return {node->Size.x / root->Size.x, node->Size.y / root->Size.y};
}

void frames(Fixture &fixture, editor::Editor &editor, int count = 3) {
  for (int frame = 0; frame < count; ++frame) {
    fixture.frame(editor);
  }
}

// Splits the assets from the log and hides the statistics: a layout and view that are not the default.
void changeLayout(Fixture &fixture, editor::Editor &editor) {
  ImGuiID log = dockOf("Log");
  const ImGuiID rootId = ImGui::DockNodeGetRootNode(ImGui::DockBuilderGetNode(log))->ID;
  const ImGuiID side = ImGui::DockBuilderSplitNode(log, ImGuiDir_Right, 0.5f, nullptr, &log);
  ImGui::DockBuilderDockWindow("Assets", side);
  ImGui::DockBuilderFinish(rootId);
  editor.setShowColliders(true);
  frames(fixture, editor);
}

} // namespace

TEST_CASE("a saved layout restores the arrangement and the view, and the built-ins differ", "[editor][gpu][layout]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("presets");
  std::filesystem::create_directories(directory);
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(directory / "layout.ini");
  frames(fixture, editor);
  REQUIRE(dockOf("Assets") == dockOf("Log"));
  REQUIRE(!editor.viewState().game);

  changeLayout(fixture, editor);
  REQUIRE(dockOf("Assets") != dockOf("Log"));
  REQUIRE(editor.viewState().colliders);
  REQUIRE(editor.saveLayout("My layout"));
  REQUIRE(std::filesystem::exists(directory / "layouts" / "My layout.ini"));
  REQUIRE(editor.savedLayouts() == std::vector<std::string>{"My layout"});

  // The Play built-in changes both.
  editor.applyLayout("Play");
  frames(fixture, editor);
  CHECK(dockOf("Viewport") != dockOf("Game"));
  CHECK(dockOf("Hierarchy") != dockOf("Inspector"));
  CHECK(editor.viewState() == editor::builtinViewState(editor::BuiltinLayout::Play));
  CHECK(editor.viewState().colliders);
  CHECK(!editor.viewState().log);
  CHECK(dockFraction("Hierarchy").x < 0.16f);

  // Default is today's arrangement with colliders and gizmos off, and shares View > Reset layout.
  editor.applyLayout("Default");
  frames(fixture, editor);
  CHECK(dockOf("Assets") == dockOf("Log"));
  CHECK(editor.viewState() == editor::builtinViewState(editor::BuiltinLayout::Default));
  CHECK(!editor.viewState().colliders);
  CHECK(dockFraction("Hierarchy").x > 0.16f);

  // Loading the saved preset brings both back.
  editor.applyLayout("My layout");
  frames(fixture, editor);
  CHECK(dockOf("Assets") != dockOf("Log"));
  CHECK(editor.viewState().colliders);

  // Reset layout leaves the Game panel closed, as the default does.
  editor.resetLayout();
  frames(fixture, editor);
  CHECK(!editor.viewState().game);
  CHECK(dockOf("Assets") == dockOf("Log"));
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("saving a layout refuses reserved and existing names", "[editor][gpu][layout]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("preset_names");
  std::filesystem::create_directories(directory);
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  frames(fixture, editor, 1);
  CHECK(!editor.saveLayout("mine")); // a headless editor keeps no layouts
  editor.setLayoutFile(directory / "layout.ini");
  CHECK(!editor.saveLayout("default"));
  CHECK(!editor.saveLayout("  "));
  REQUIRE(editor.saveLayout("a/b"));
  CHECK(editor.savedLayouts() == std::vector<std::string>{"a_b"});
  CHECK(!editor.saveLayout("a/b"));
  CHECK(editor.saveLayout("a/b", true));
  CHECK(!editor.deleteLayout("Play"));
  CHECK(editor.deleteLayout("a_b"));
  CHECK(editor.savedLayouts().empty());
  CHECK(!editor.deleteLayout("a_b"));
}

TEST_CASE("a missing or corrupt preset is ignored", "[editor][gpu][layout]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("preset_corrupt");
  std::filesystem::create_directories(directory / "layouts");
  std::ofstream{directory / "layouts" / "broken.ini"} << "[Docking][Data]\nDockSpace ID=0x1 trunc";
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(directory / "layout.ini");
  frames(fixture, editor);
  changeLayout(fixture, editor);
  const editor::ViewState before = editor.viewState();
  for (const char *name : {"broken", "absent"}) {
    editor.applyLayout(name);
    frames(fixture, editor);
    CHECK(dockOf("Assets") != dockOf("Log"));
    CHECK(editor.viewState() == before);
  }
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("layout.ini restores the shown panels and overlays", "[editor][gpu][layout]") {
  Fixture fixture;
  const std::filesystem::path file = scratch("layout_view") / "layout.ini";
  std::filesystem::create_directories(file.parent_path());
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(file);
    editor.applyLayout("Play");
    frames(fixture, editor);
  }
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(file);
  frames(fixture, editor);
  CHECK(editor.viewState() == editor::builtinViewState(editor::BuiltinLayout::Play));
  CHECK(dockOf("Viewport") != dockOf("Game"));
}

TEST_CASE("a preset saved at twice the size keeps its split fractions", "[editor][gpu][layout]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("preset_scale");
  std::filesystem::create_directories(directory);
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(directory / "layout.ini");
  frames(fixture, editor);
  const ImVec2 hierarchy = dockFraction("Hierarchy");
  const ImVec2 log = dockFraction("Log");
  REQUIRE(editor.saveLayout("wide"));

  // Pretend it was saved on a display twice as large: every recorded size doubles.
  const std::filesystem::path file = directory / "layouts" / "wide.ini";
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  const std::string ini{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
  std::string doubled;
  const std::regex size{R"((Size|SizeRef|DockSize)=(\d+),(\d+))"};
  std::size_t last = 0;
  for (auto it = std::sregex_iterator(ini.begin(), ini.end(), size); it != std::sregex_iterator{}; ++it) {
    doubled += ini.substr(last, static_cast<std::size_t>(it->position()) - last);
    doubled +=
        (*it)[1].str() + "=" + std::to_string(std::stoi((*it)[2]) * 2) + "," + std::to_string(std::stoi((*it)[3]) * 2);
    last = static_cast<std::size_t>(it->position() + it->length());
  }
  doubled += ini.substr(last);
  REQUIRE(doubled != ini);
  REQUIRE(core::writeFile(file, doubled).has_value());

  editor.applyLayout("wide");
  frames(fixture, editor);
  CHECK(dockFraction("Hierarchy").x == Approx(hierarchy.x).margin(0.02));
  CHECK(dockFraction("Log").y == Approx(log.y).margin(0.02));
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

namespace {

// Layouts ImGui must never be handed (ADR-0036). Each takes an ini this editor saved and breaks one thing.
using Defect = std::string (*)(const std::string &);

// `ini` with `change` applied to every line of the dock tree and nothing else.
std::string mapDockLines(const std::string &ini, const std::function<std::string(const std::string &)> &change) {
  std::string out;
  bool inDock = false;
  std::size_t position = 0;
  while (position < ini.size()) {
    const std::size_t newline = ini.find('\n', position);
    const std::size_t end = newline == std::string::npos ? ini.size() : newline;
    const std::string line = ini.substr(position, end - position);
    if (!line.empty() && line.front() == '[') {
      inDock = line == "[Docking][Data]";
    }
    out += inDock && line.find("Dock") != std::string::npos && line.front() != '[' ? change(line) : line;
    if (newline != std::string::npos) {
      out += '\n';
    }
    position = end + 1;
  }
  return out;
}

// Every dock Size and SizeRef is zero.
std::string zeroSize(const std::string &ini) {
  return mapDockLines(ini, [](const std::string &line) {
    return std::regex_replace(line, std::regex{R"((Size|SizeRef)=-?\d+,-?\d+)"}, "$1=0,0");
  });
}

// Only the dock root has no size.
std::string zeroRoot(const std::string &ini) {
  return mapDockLines(ini, [](const std::string &line) {
    return line.find("DockSpace") != std::string::npos
               ? std::regex_replace(line, std::regex{R"(Size=\d+,\d+)"}, "Size=0,0")
               : line;
  });
}

// The first child points at a node that is not in the file.
std::string missingParent(const std::string &ini) {
  bool done = false;
  return mapDockLines(ini, [&](const std::string &line) {
    if (done || line.find("Parent=0x") == std::string::npos) {
      return line;
    }
    done = true;
    return std::regex_replace(line, std::regex{R"(Parent=0x[0-9A-Fa-f]+)"}, "Parent=0xDEADBEEF");
  });
}

// The file ends in the middle of the third line of the dock tree.
std::string truncatedDock(const std::string &ini) {
  std::size_t position = ini.find("[Docking][Data]");
  for (int line = 0; line < 3; ++line) {
    position = ini.find('\n', position) + 1;
  }
  return ini.substr(0, position + 28);
}

// The file ends in the middle of the view section, which comes before the dock tree.
std::string truncatedView(const std::string &ini) {
  const std::size_t view = ini.find("[SonnetView][View]");
  return ini.substr(0, ini.find("Game", view) + 3);
}

// A node with the id of an earlier one.
std::string duplicateNode(const std::string &ini) {
  std::string firstId;
  return mapDockLines(ini, [&](const std::string &line) {
    std::smatch id;
    if (!std::regex_search(line, id, std::regex{R"(ID=0x[0-9A-Fa-f]+)"})) {
      return line;
    }
    if (firstId.empty()) {
      firstId = id[0];
      return line;
    }
    return std::regex_replace(line, std::regex{R"(ID=0x[0-9A-Fa-f]+)"}, firstId);
  });
}

// Every window claims a dock node that is not in the file.
std::string danglingWindowDock(const std::string &ini) {
  return std::regex_replace(ini, std::regex{R"(DockId=0x[0-9A-Fa-f]+)"}, "DockId=0x0BADBAD1");
}

// A dock node comes before its parent.
std::string parentAfterChild(const std::string &ini) {
  const std::size_t start = ini.find("DockSpace");
  const std::size_t end = ini.find('\n', start) + 1;
  const std::string line = ini.substr(start, end - start);
  std::string out = ini;
  out.erase(start, line.size());
  const std::size_t tail = out.find("\n\n", start);
  out.insert(tail + 1, line);
  return out;
}

// A node that is its own parent.
std::string selfParent(const std::string &ini) {
  bool done = false;
  return mapDockLines(ini, [&](const std::string &line) {
    std::smatch id;
    if (done || line.find("Parent=0x") == std::string::npos ||
        !std::regex_search(line, id, std::regex{R"(ID=(0x[0-9A-Fa-f]+))"})) {
      return line;
    }
    done = true;
    return std::regex_replace(line, std::regex{R"(Parent=0x[0-9A-Fa-f]+)"}, "Parent=" + id[1].str());
  });
}

// An ini saved by an editor whose Assets are split from its Log, so that a layout that loads is told from the default.
std::string splitLayoutIni(Fixture &fixture) {
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  frames(fixture, editor);
  changeLayout(fixture, editor);
  return ImGui::SaveIniSettingsToMemory();
}

// The editor starts on `bad` as its layout.ini: ImGui never sees it, the default arrangement is built, and the
// file is renamed rather than deleted.
void expectStartupRejects(const char *name, Defect defect) {
  Fixture fixture;
  const std::filesystem::path directory = scratch(name);
  std::filesystem::create_directories(directory);
  const std::string good = splitLayoutIni(fixture);
  const std::string bad = defect(good);
  REQUIRE(bad != good);
  const std::filesystem::path file = directory / "layout.ini";
  REQUIRE(core::writeFile(file, bad).has_value());
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(file);
    frames(fixture, editor);
    CHECK(dockOf("Assets") == dockOf("Log"));
    CHECK(editor.viewState() == editor::builtinViewState(editor::BuiltinLayout::Default));
    CHECK(!std::filesystem::exists(file));
    const auto kept = core::readFile(directory / "layout.ini.rejected");
    REQUIRE(kept.has_value());
    CHECK(std::string(reinterpret_cast<const char *>(kept->data()), kept->size()) == bad);
  }
  // The editor saves the layout it used when it closes.
  CHECK(std::filesystem::exists(file));
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

// `bad` as a saved preset: refused, and the layout and view in use stay.
void expectPresetRefused(const char *name, Defect defect) {
  Fixture fixture;
  const std::filesystem::path directory = scratch(name);
  std::filesystem::create_directories(directory / "layouts");
  const std::string good = splitLayoutIni(fixture);
  const std::string bad = defect(good);
  REQUIRE(bad != good);
  REQUIRE(core::writeFile(directory / "layouts" / "bad.ini", bad).has_value());
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(directory / "layout.ini");
  frames(fixture, editor);
  REQUIRE(dockOf("Assets") == dockOf("Log"));
  const editor::ViewState before = editor.viewState();
  editor.applyLayout("bad");
  frames(fixture, editor);
  CHECK(dockOf("Assets") == dockOf("Log"));
  CHECK(editor.viewState() == before);
  CHECK(std::filesystem::exists(directory / "layouts" / "bad.ini")); // a preset is not renamed or deleted
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

} // namespace

namespace {

// Every layout an editor with a window of `size` can write, in the order a user reaches them: the default, a split
// one, both built-ins, a saved preset, the reset, and what the files hold (the preset and layout.ini at exit).
std::vector<std::string> layoutsSavedAt(glm::uvec2 size, const char *name) {
  Fixture fixture{size};
  const std::filesystem::path directory = scratch(name);
  std::filesystem::create_directories(directory);
  std::vector<std::string> inis;
  const auto save = [&](editor::Editor &editor) {
    frames(fixture, editor);
    inis.emplace_back(ImGui::SaveIniSettingsToMemory());
  };
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(directory / "layout.ini");
    save(editor);
    changeLayout(fixture, editor);
    save(editor);
    REQUIRE(editor.saveLayout("mine"));
    for (const char *builtin : {"Play", "Default", "mine"}) {
      editor.applyLayout(builtin);
      save(editor);
    }
    editor.resetLayout();
    save(editor);
  }
  for (const std::filesystem::path &file : {directory / "layouts" / "mine.ini", directory / "layout.ini"}) {
    const auto bytes = core::readFile(file);
    REQUIRE(bytes.has_value());
    inis.emplace_back(reinterpret_cast<const char *>(bytes->data()), bytes->size());
  }
  return inis;
}

} // namespace

TEST_CASE("the layouts this editor saves pass the check at two window sizes and load unchanged",
          "[editor][gpu][layout]") {
  const std::vector<std::string> small = layoutsSavedAt({800, 600}, "saved_small");
  const std::vector<std::string> large = layoutsSavedAt({1280, 720}, "saved_large");
  REQUIRE(small.size() == large.size());
  REQUIRE(small.front() != large.front()); // the dock root's size differs
  for (const auto *set : {&small, &large}) {
    for (const std::string &ini : *set) {
      INFO(ini);
      CHECK(!editor::layoutIniProblem(ini));
      CHECK(editor::isLayoutIni(ini));
      // What the editor does to a preset saved at another size.
      CHECK(!editor::layoutIniProblem(editor::scaleLayoutIni(ini, 1600.0f, 900.0f)));
    }
  }

  // The split layout saved at 1280x720 starts an 800x600 editor as it was, not as the default.
  Fixture fixture;
  const std::filesystem::path file = scratch("saved_startup") / "layout.ini";
  std::filesystem::create_directories(file.parent_path());
  REQUIRE(core::writeFile(file, large[1]).has_value());
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(file);
  frames(fixture, editor);
  CHECK(dockOf("Assets") != dockOf("Log"));
  CHECK(editor.viewState().colliders);
  CHECK(std::filesystem::exists(file));
  CHECK(!std::filesystem::exists(file.string() + ".rejected"));
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("a layout.ini with a dock tree of zero size is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_zero_size", zeroSize);
}

TEST_CASE("a layout.ini with a dock root of zero size is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_zero_root", zeroRoot);
}

TEST_CASE("a layout.ini with a dock node whose parent is missing is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_missing_parent", missingParent);
}

TEST_CASE("a layout.ini truncated in the dock tree is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_truncated_dock", truncatedDock);
}

TEST_CASE("a layout.ini truncated in the view section is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_truncated_view", truncatedView);
}

TEST_CASE("a layout.ini with two dock nodes of one id is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("startup_duplicate", duplicateNode);
}

TEST_CASE("a preset with a dock tree of zero size is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_zero_size", zeroSize);
}

TEST_CASE("a preset with a dock root of zero size is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_zero_root", zeroRoot);
}

TEST_CASE("a preset with a dock node whose parent is missing is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_missing_parent", missingParent);
}

TEST_CASE("a preset truncated in the dock tree is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_truncated_dock", truncatedDock);
}

TEST_CASE("a preset truncated in the view section is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_truncated_view", truncatedView);
}

TEST_CASE("a preset with two dock nodes of one id is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("preset_duplicate", duplicateNode);
}

TEST_CASE("a layout.ini docking a window in a node it lacks is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("p1", danglingWindowDock);
}
TEST_CASE("a layout.ini listing a dock node before its parent is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("p2", parentAfterChild);
}
TEST_CASE("a layout.ini with a dock node that is its own parent is not loaded", "[editor][gpu][layout][defect]") {
  expectStartupRejects("p3", selfParent);
}
TEST_CASE("a preset docking a window in a node it lacks is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("p4", danglingWindowDock);
}
TEST_CASE("a preset listing a dock node before its parent is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("p5", parentAfterChild);
}
TEST_CASE("a preset with a dock node that is its own parent is refused", "[editor][gpu][layout][defect]") {
  expectPresetRefused("p6", selfParent);
}

TEST_CASE("viewports are requested only where a window can be shown and positioned", "[editor]") {
  // Platform cannot be faked, so the request is a function of the two facts it reads from it.
  CHECK(editor::wantsViewports(false, true));   // X11, Windows, macOS, XWayland
  CHECK(!editor::wantsViewports(false, false)); // native Wayland
  CHECK(!editor::wantsViewports(true, true));   // the headless drivers
  CHECK(!editor::wantsViewports(true, false));
}

TEST_CASE("a layout saved with viewports on opens its floating panel inside the window when they are off",
          "[editor][gpu][layout]") {
  Fixture fixture; // headless: the editor asks for no viewports
  const std::filesystem::path file = scratch("layout_floating") / "layout.ini";
  std::filesystem::create_directories(file.parent_path());
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setLayoutFile(file);
    frames(fixture, editor);
  }
  REQUIRE(std::filesystem::exists(file));
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  std::string ini{reinterpret_cast<const char *>(bytes->data()), bytes->size()};

  // What an editor with viewports writes for the Log panel dragged out to a second display: no
  // dock, a position far beyond the main window, and the viewport it lived in.
  const std::size_t header = ini.find("[Window][Log]\n");
  REQUIRE(header != std::string::npos);
  const std::size_t end = ini.find("\n\n", header);
  REQUIRE(end != std::string::npos);
  ini.replace(header, end - header,
              "[Window][Log]\nPos=5200,2100\nSize=640,360\nViewportId=0x4F1B2C3D\nViewportPos=5200,2100\nCollapsed=0");
  // The edited file must be one the editor accepts, so ImGui's own placement is what is tested.
  REQUIRE(!editor::layoutIniProblem(ini).has_value());
  REQUIRE(core::writeFile(file, ini).has_value());

  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  editor.setLayoutFile(file);
  REQUIRE(!std::filesystem::exists(file.string() + ".rejected"));
  frames(fixture, editor);
  const ImGuiWindow *log = ImGui::FindWindowByName("Log");
  REQUIRE(log != nullptr);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  INFO("Log at " << log->Pos.x << "," << log->Pos.y << " docked " << (log->DockId != 0) << " in a display of "
                 << display.x << "x" << display.y);
  CHECK(log->Active);
  CHECK(log->Viewport == ImGui::GetMainViewport());
  // ImGui pulls the window back into the main window, to its corner with a sliver showing: reachable
  // to drag or dock, though not placed well (layout validation is where that improves).
  CHECK(log->Pos.x < display.x);
  CHECK(log->Pos.y < display.y);
  CHECK(log->Pos.x + log->Size.x > 0.0f);
  CHECK(log->Pos.y + log->Size.y > 0.0f);
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("regaining the focus picks up assets and prefab files changed outside the editor",
          "[editor][gpu][prefab][rescan]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("rescan-on-focus");
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  REQUIRE(editor.createProject(directory, "Test").has_value());
  world::World &world = editor.world();
  core::Uuid box;
  for (const flecs::entity root : world.roots()) {
    if (root.get<world::Name>().value == "Box") {
      box = world.uuidOf(root);
    }
  }
  REQUIRE(!box.isNil());
  const auto prefab = editor.saveAsPrefab(box);
  REQUIRE(prefab.has_value());
  fixture.frame(editor);
  const std::size_t undoable = editor.commands().size();

  // What a file manager does: a new image, and a prefab file moved to another name.
  const std::filesystem::path image = directory / "assets" / "outside.png";
  const std::filesystem::path renamed = directory / "prefabs" / "Renamed.prefab.json";
  REQUIRE(core::writeFile(image, std::string_view{"png"}).has_value());
  std::filesystem::rename(*prefab, renamed);
  fixture.frame(editor);
  CHECK(editor.assets().findByPath(image) == nullptr); // nothing looks until the window is focused

  editor.event(platform::WindowFocusChanged{.focused = true});
  fixture.frame(editor);
  fixture.frame(editor);
  CHECK(editor.assets().findByPath(image) != nullptr);
  REQUIRE(world.isInstance(world.find(box)));
  CHECK(world.prefabOf(world.find(box)).get<world::PrefabFile>().path == renamed);
  // The rebuild of the prefabs cuts the history, as after a re-import; a plain addition does not.
  CHECK(editor.commands().size() <= undoable);

  const std::filesystem::path second = directory / "assets" / "again.png";
  REQUIRE(core::writeFile(second, std::string_view{"png"}).has_value());
  const std::size_t before = editor.commands().size();
  editor.event(platform::WindowFocusChanged{.focused = true});
  fixture.frame(editor);
  CHECK(editor.assets().findByPath(second) != nullptr);
  CHECK(editor.commands().size() == before);
}

TEST_CASE("scenes open in tabs that keep their own edits, undo history and selection", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("tabs");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Tabs").has_value());
    REQUIRE(editor.tabCount() == 1);
    world::World &world = editor.world();
    const std::size_t startRoots = world.roots().size();
    const std::filesystem::path first = editor.scenePath();
    fixture.frame(editor);

    // A second scene opens beside the first, and opening the first again switches back to it.
    editor.newScene();
    REQUIRE(editor.tabCount() == 2);
    REQUIRE(editor.activeTab() == 1);
    REQUIRE(editor.scenePath().empty());
    REQUIRE(editor.saveSceneAs(directory / "scenes" / "second.scene.json").has_value());
    editor.commands().push(editor::deleteEntityCommand(world.uuidOf(world.roots().front())), world);
    REQUIRE(world.roots().size() == startRoots - 1);
    REQUIRE(editor.tabDirty(1));
    fixture.frame(editor);

    editor.switchToTab(0);
    REQUIRE(editor.scenePath() == first);
    REQUIRE(world.roots().size() == startRoots);
    REQUIRE(!editor.commands().canUndo());
    REQUIRE(editor.tabDirty(1));
    fixture.frame(editor);
    REQUIRE(editor.openScene(directory / "scenes" / "second.scene.json").has_value());
    REQUIRE(editor.tabCount() == 2); // switched, not reopened
    REQUIRE(editor.activeTab() == 1);
    REQUIRE(world.roots().size() == startRoots - 1); // the unsaved edit is still there
    REQUIRE(editor.commands().canUndo());
    REQUIRE(editor.commands().undo(world));
    REQUIRE(world.roots().size() == startRoots);

    // A scene that fails to load leaves the tabs as they were.
    REQUIRE(!editor.openScene(directory / "scenes" / "missing.scene.json").has_value());
    REQUIRE(editor.tabCount() == 2);
    REQUIRE(editor.activeTab() == 1);
    REQUIRE(world.roots().size() == startRoots);

    // Closing the active tab shows its neighbour; closing the last one leaves a fresh scene.
    editor.closeTab(1);
    REQUIRE(editor.tabCount() == 1);
    REQUIRE(editor.scenePath() == first);
    REQUIRE(world.roots().size() == startRoots);
    editor.closeTab(0);
    REQUIRE(editor.tabCount() == 1);
    REQUIRE(editor.scenePath().empty());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("a project reopens the scene tabs it had open when it was closed", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("reopen_tabs");
  const std::filesystem::path preferences = scratch("reopen_tabs_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  const std::filesystem::path second = directory / "scenes" / "second.scene.json";
  const std::filesystem::path third = directory / "scenes" / "third.scene.json";
  std::filesystem::path first;
  const auto makeEditor = [&](bool remember) {
    auto editor =
        std::make_unique<editor::Editor>(fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain);
    editor->setPreferencesFile(preferences);
    editor->setRememberOpenScenes(remember);
    return editor;
  };
  {
    const auto editor = makeEditor(true);
    REQUIRE(editor->createProject(directory, "Reopen").has_value());
    first = editor->scenePath();
    editor->newScene();
    REQUIRE(editor->saveSceneAs(second).has_value());
    editor->newScene();
    REQUIRE(editor->saveSceneAs(third).has_value());
    editor->newScene(); // untitled: recovery's business, not recorded
    editor->switchToTab(1);
    fixture.frame(*editor);
    REQUIRE(editor->openScenes() == editor::OpenScenes{.scenes = {first, second, third}, .active = 1});
  }
  {
    // Quitting recorded them: the same tabs, in the same order, with the same one active.
    const auto editor = makeEditor(true);
    REQUIRE(editor->openProject(directory).has_value());
    REQUIRE(editor->tabCount() == 3);
    REQUIRE(editor->activeTab() == 1);
    REQUIRE(editor->scenePath() == second);
    REQUIRE(editor->tabTitle(0) == first.filename().string());
    REQUIRE(editor->tabTitle(2) == "third.scene.json");
    fixture.frame(*editor);
  }
  {
    // An editor that does not remember opens the start scene and leaves the record alone.
    const auto editor = makeEditor(false);
    REQUIRE(editor->openProject(directory).has_value());
    REQUIRE(editor->tabCount() == 1);
    REQUIRE(editor->scenePath() == first);
    fixture.frame(*editor);
  }
  std::filesystem::remove(third);
  {
    // A scene that is gone is skipped; the rest open.
    const auto editor = makeEditor(true);
    REQUIRE(editor->openProject(directory).has_value());
    REQUIRE(editor->tabCount() == 2);
    REQUIRE(editor->scenePath() == second);
    fixture.frame(*editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("the Assets panel's grid shows thumbnails and the choice survives a restart", "[editor][gpu][grid]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("assets_grid");
  const std::filesystem::path preferences = scratch("assets_grid_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  const auto makeEditor = [&] {
    auto editor =
        std::make_unique<editor::Editor>(fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain);
    editor->setPreferencesFile(preferences);
    return editor;
  };
  std::filesystem::path root;
  {
    const auto editor = makeEditor();
    REQUIRE(editor->createProject(directory, "Grid").has_value());
    root = editor->project()->root;
    // Three uncompressed 4 x 4 TGA files: a header and the pixels.
    for (const char *name : {"a.tga", "b.tga", "c.tga"}) {
      std::vector<std::byte> tga(18, std::byte{0});
      tga[2] = std::byte{2};
      tga[12] = std::byte{4};
      tga[14] = std::byte{4};
      tga[16] = std::byte{32};
      tga[17] = std::byte{8};
      tga.resize(18 + 4 * 4 * 4, std::byte{200});
      REQUIRE(core::writeFile(root / "assets" / name, tga).has_value());
    }
    static_cast<void>(editor->assets().rescan());
    editor->assetBrowser().navigate(root / "assets");
    for (int i = 0; i < 3; ++i) {
      fixture.frame(*editor);
    }
    REQUIRE_FALSE(editor->assetBrowser().view().grid);

    // The grid button of the panel, clicked the way a user does.
    auto &io = ImGui::GetIO();
    const editor::ScreenRect button = editor->assetBrowser().gridButton();
    io.AddMousePosEvent((button.min.x + button.max.x) * 0.5f, (button.min.y + button.max.y) * 0.5f);
    fixture.frame(*editor);
    io.AddMouseButtonEvent(0, true);
    fixture.frame(*editor);
    io.AddMouseButtonEvent(0, false);
    fixture.frame(*editor);
    REQUIRE(editor->assetBrowser().view().grid);
    REQUIRE(editor::Preferences::load(preferences).assetsView.at(root).grid);

    // The tiles ask for their pictures; once the workers have made them they are uploaded and drawn.
    fixture.frame(*editor);
    REQUIRE(editor->assetBrowser().tiles().size() >= 3);
    editor->thumbnails().settle();
    REQUIRE(editor->thumbnails().resident() == 3);
    REQUIRE_FALSE(editor->thumbnails().busy());
    for (int i = 0; i < 3; ++i) {
      fixture.frame(*editor);
    }
    for (const char *name : {"a", "b", "c"}) {
      const assets::AssetInfo *info = editor->assets().findByPath(root / "assets" / (std::string{name} + ".tga"));
      REQUIRE(info != nullptr);
      const editor::ThumbnailImage picture = editor->thumbnails().get(info->uuid);
      CHECK(picture.texture != 0);
      CHECK(picture.size == glm::uvec2{4, 4});
      CHECK(std::filesystem::exists(editor->assets().cacheDirectory() / (info->uuid.toString() + ".thumb.ktx2")));
    }
  }
  {
    // The next run of the editor opens the project in the grid.
    const auto editor = makeEditor();
    REQUIRE(editor->openProject(directory).has_value());
    REQUIRE(editor->assetBrowser().view().grid);
    editor->assetBrowser().navigate(root / "assets");
    for (int i = 0; i < 4; ++i) {
      fixture.frame(*editor);
    }
    // The pictures come back from the cache.
    editor->thumbnails().settle();
    REQUIRE(editor->thumbnails().resident() == 3);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

namespace {

// The checkout's golden image directory, found by walking up from the test binary; empty outside one.
std::filesystem::path goldenDirectory(platform::Platform &platform) {
  std::error_code error;
  for (std::filesystem::path base = std::filesystem::absolute(platform.basePath(), error); !base.empty();
       base = base.parent_path()) {
    const std::filesystem::path candidate = base / "modules" / "editor" / "tests" / "golden";
    if (std::filesystem::is_directory(candidate, error)) {
      return candidate.lexically_normal();
    }
    if (base == base.root_path()) {
      break;
    }
  }
  return {};
}

// On Lavapipe, where a golden image is reproducible, the picture is the one on disk (SONNET_UPDATE_GOLDEN=1 writes it).
void expectGolden(Fixture &fixture, const char *name, const renderer::TextureData &picture) {
  if (fixture.device->info().driverName == "llvmpipe") {
    const std::filesystem::path golden = goldenDirectory(fixture.platform) / name;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
    const bool update = std::getenv("SONNET_UPDATE_GOLDEN") != nullptr; // NOLINT(concurrency-mt-unsafe)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (update) {
      REQUIRE(runtime::writeScreenshot(golden, picture.size, picture.format, std::as_bytes(std::span{picture.data}))
                  .has_value());
    }
    const auto bytes = core::readFile(golden);
    REQUIRE(bytes.has_value());
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(bytes->data()),
                                                  static_cast<int>(bytes->size()), &width, &height, &channels, 4);
    REQUIRE(pixels != nullptr);
    REQUIRE(glm::uvec2(width, height) == picture.size);
    double sum = 0.0;
    int worst = 0;
    for (std::size_t i = 0; i < std::size_t{picture.size.x} * picture.size.y; ++i) {
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const int error = std::abs(int{pixels[i * 4 + channel]} - std::to_integer<int>(picture.data[i * 4 + channel]));
        sum += error;
        worst = std::max(worst, error);
      }
    }
    stbi_image_free(pixels);
    const double mean = sum / (3.0 * picture.size.x * picture.size.y);
    INFO("against the golden: mean " << mean << ", max " << worst);
    CHECK(mean <= 1.5);
    CHECK(worst <= 48);
  }
}

// A material's cached picture, which the test reads the way the next run of the editor does.
renderer::TextureData cachedPicture(editor::Editor &editor, const core::Uuid &uuid) {
  const auto request = editor.assets().thumbnailRequest(uuid);
  INFO("the thumbnail of " << uuid.toString());
  REQUIRE(request.has_value());
  const auto picture = assets::cachedThumbnail(*request);
  REQUIRE(picture.has_value());
  return *picture;
}

std::array<int, 3> pixelOf(const renderer::TextureData &picture, std::uint32_t x, std::uint32_t y) {
  const std::size_t at = (std::size_t{y} * picture.size.x + x) * 4;
  return {std::to_integer<int>(picture.data[at]), std::to_integer<int>(picture.data[at + 1]),
          std::to_integer<int>(picture.data[at + 2])};
}

} // namespace

TEST_CASE("the grid renders a material on a sphere, caches it, and renders it again after an edit",
          "[editor][gpu][grid][golden]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("assets_material_thumbnails");
  const std::filesystem::path preferences = scratch("assets_material_thumbnails_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  const auto makeEditor = [&] {
    auto editor =
        std::make_unique<editor::Editor>(fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain);
    editor->setPreferencesFile(preferences);
    return editor;
  };
  // Frames until every tile has its picture: a material is copied back FramesInFlight frames after its view.
  const auto settle = [&](editor::Editor &editor) {
    for (int i = 0; i < 6 || (editor.thumbnails().busy() && i < 120); ++i) {
      fixture.frame(editor);
    }
    REQUIRE_FALSE(editor.thumbnails().busy());
  };
  const auto centre = [](const renderer::TextureData &picture) {
    return pixelOf(picture, picture.size.x / 2, picture.size.y / 2);
  };

  assets::MaterialSource red;
  red.baseColor = {0.9f, 0.08f, 0.06f, 1.0f};
  red.metallic = 0.0f;
  red.roughness = 0.4f;
  assets::MaterialSource blue = red;
  blue.baseColor = {0.06f, 0.1f, 0.9f, 1.0f};

  core::Uuid uuid;
  std::filesystem::path root;
  {
    const auto editor = makeEditor();
    REQUIRE(editor->createProject(directory, "Materials").has_value());
    root = editor->project()->root;
    const auto created = editor->assets().createMaterial(root / "assets" / "paint.material.json", red);
    REQUIRE(created.has_value());
    uuid = *created;
    REQUIRE(editor->assets().saveMaterial(uuid).has_value());
    editor->assetBrowser().navigate(root / "assets");
    editor->assetBrowser().setView({.grid = true, .tileSize = 96.0f});
    settle(*editor);
    CHECK(editor->thumbnails().rendered() == 1);
    CHECK(editor->thumbnails().resident() == 1);
    CHECK(std::filesystem::exists(assets::thumbnailFile(editor->assets().cacheDirectory(), uuid)));
    const renderer::TextureData picture = cachedPicture(*editor, uuid);
    CHECK(picture.size == glm::uvec2{assets::ThumbnailSize, assets::ThumbnailSize});
    const auto middle = centre(picture);
    CHECK(middle[0] > 2 * middle[1]);
    CHECK(middle[0] > 2 * middle[2]);
    const auto corner = pixelOf(picture, 2, 2);
    CHECK(corner[0] < 2 * corner[2] + 40); // the sky behind the sphere is not red

    // On Lavapipe, where a golden image is reproducible, the picture is the one on disk.
    expectGolden(fixture, "material_thumbnail.png", picture);

    // An edit that is not saved is shown, and not cached: the file would not match it.
    editor->assets().setMaterialSource(uuid, blue);
    settle(*editor);
    CHECK(editor->thumbnails().rendered() == 2);
    CHECK_FALSE(std::filesystem::exists(assets::thumbnailFile(editor->assets().cacheDirectory(), uuid)));
    // Saved, it is rendered once more and cached.
    REQUIRE(editor->assets().saveMaterial(uuid).has_value());
    settle(*editor);
    CHECK(editor->thumbnails().rendered() == 3);
    const auto saved = centre(cachedPicture(*editor, uuid));
    CHECK(saved[2] > 2 * saved[0]);
    CHECK(saved[2] > 2 * saved[1]);
  }
  {
    // The next run of the editor reads the picture from the cache.
    const auto editor = makeEditor();
    REQUIRE(editor->openProject(directory).has_value());
    editor->assetBrowser().navigate(root / "assets");
    editor->assetBrowser().setView({.grid = true, .tileSize = 96.0f});
    settle(*editor);
    CHECK(editor->thumbnails().rendered() == 0);
    CHECK(editor->thumbnails().resident() == 1);
    // A material file changed on disk since is rendered again.
    std::filesystem::last_write_time(root / "assets" / "paint.material.json",
                                     std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    REQUIRE(editor->assets().reimport(uuid).has_value());
    editor->thumbnails().invalidate(uuid);
    settle(*editor);
    CHECK(editor->thumbnails().rendered() == 1);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

namespace {

std::unique_ptr<editor::Editor> openEditor(Fixture &fixture, const std::filesystem::path &preferences) {
  auto editor =
      std::make_unique<editor::Editor>(fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain);
  editor->setPreferencesFile(preferences);
  return editor;
}

// Frames until every tile has its picture: one is copied back FramesInFlight frames after its view.
void settleThumbnails(Fixture &fixture, editor::Editor &editor) {
  for (int i = 0; i < 6 || (editor.thumbnails().busy() && i < 240); ++i) {
    fixture.frame(editor);
  }
  REQUIRE_FALSE(editor.thumbnails().busy());
}

void showGrid(editor::Editor &editor, const std::filesystem::path &folder) {
  editor.assetBrowser().navigate(folder);
  editor.assetBrowser().setView({.grid = true, .tileSize = 96.0f});
}

} // namespace

TEST_CASE("the grid renders models, caches them, and renders one again after a reimport",
          "[editor][gpu][grid][golden]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest.has_value());
  const std::filesystem::path samples = manifest->parent_path() / "assets" / "models";
  const std::filesystem::path directory = scratch("assets_model_thumbnails");
  const std::filesystem::path preferences = scratch("assets_model_thumbnails_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->createProject(directory, "Models").has_value());
    fixture.frame(*editor); // the renderer's uploads are submitted before another editor makes its own
  }
  const std::filesystem::path models = directory / "assets" / "models";
  std::filesystem::create_directories(models);
  std::filesystem::copy_file(samples / "crate.glb", models / "crate.glb");
  std::filesystem::copy_file(samples / "walker.glb", models / "walker.glb"); // skinned: drawn in its bind pose

  const auto corner = [](const renderer::TextureData &picture) { return pixelOf(picture, 2, 2); };
  const auto centre = [](const renderer::TextureData &picture) {
    return pixelOf(picture, picture.size.x / 2, picture.size.y / 2);
  };
  core::Uuid crate;
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->openProject(directory).has_value());
    const assets::AssetInfo *crateInfo = editor->assets().findByPath(models / "crate.glb");
    const assets::AssetInfo *walkerInfo = editor->assets().findByPath(models / "walker.glb");
    REQUIRE(crateInfo != nullptr);
    REQUIRE(walkerInfo != nullptr);
    crate = crateInfo->uuid;
    showGrid(*editor, models);
    // The panel asks for the tiles in view, which are few in a window this size.
    static_cast<void>(editor->thumbnails().get(crate));
    static_cast<void>(editor->thumbnails().get(walkerInfo->uuid));
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() >= 2);
    CHECK(std::filesystem::exists(assets::thumbnailFile(editor->assets().cacheDirectory(), crate)));
    const renderer::TextureData picture = cachedPicture(*editor, crate);
    CHECK(picture.size == glm::uvec2{assets::ThumbnailSize, assets::ThumbnailSize});
    CHECK(centre(picture) != corner(picture)); // the model, not just the sky
    const renderer::TextureData walker = cachedPicture(*editor, walkerInfo->uuid);
    CHECK(centre(walker) != corner(walker));
    expectGolden(fixture, "model_thumbnail.png", picture);
  }
  {
    // The next run of the editor reads the pictures from the cache.
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->openProject(directory).has_value());
    showGrid(*editor, models);
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() == 0);
    CHECK(editor->thumbnails().resident() >= 2);
    // A model file changed on disk since is rendered again.
    std::filesystem::last_write_time(models / "crate.glb",
                                     std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    REQUIRE(editor->assets().reimport(crate).has_value());
    editor->thumbnails().invalidate(crate);
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() >= 1);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

// The numbers in the pull request of #330: how long the frames take while a folder of models fills in, against frames
// with nothing to render. Run by name; on Lavapipe, whose frames are far slower than a GPU's, the cost is a ceiling.
TEST_CASE("measure the frames while a folder of models fills in", "[.thumbnail-budget][editor][gpu][grid]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest.has_value());
  const std::filesystem::path samples = manifest->parent_path() / "assets" / "models";
  const std::filesystem::path directory = scratch("thumbnail_budget");
  const std::filesystem::path preferences = scratch("thumbnail_budget_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->createProject(directory, "Budget").has_value());
    fixture.frame(*editor);
  }
  const std::filesystem::path models = directory / "assets" / "models";
  std::filesystem::create_directories(models);
  constexpr int count = 300;
  const char *files[] = {"crate.glb", "walker.glb", "beacon.glb", "reed.glb", "jelly.glb"};
  for (int i = 0; i < count; ++i) {
    std::filesystem::copy_file(samples / files[i % 5], models / std::format("model_{:03}.glb", i));
  }
  const auto milliseconds = [&](editor::Editor &editor) {
    const auto start = std::chrono::steady_clock::now();
    fixture.frame(editor);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  const auto report = [](const char *label, std::vector<double> times) {
    std::ranges::sort(times);
    double sum = 0.0;
    for (const double time : times) {
      sum += time;
    }
    const auto at = [&](double fraction) {
      return times[static_cast<std::size_t>(fraction * static_cast<double>(times.size() - 1))];
    };
    WARN(label << ": " << times.size() << " frames, mean " << sum / static_cast<double>(times.size()) << " ms, median "
               << at(0.5) << " ms, 95th " << at(0.95) << " ms, worst " << times.back() << " ms");
  };
  for (const bool cached : {false, true}) {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->openProject(directory).has_value());
    showGrid(*editor, models);
    std::vector<double> idle;
    idle.reserve(30);
    for (int i = 0; i < 30; ++i) {
      idle.push_back(milliseconds(*editor));
    }
    // Every model asked for in one frame: more than a window ever shows, so the worst case for the scheduling.
    for (const assets::AssetInfo *info : editor->assets().assets(assets::AssetType::Model)) {
      static_cast<void>(editor->thumbnails().get(info->uuid));
    }
    std::vector<double> filling;
    int frames = 0;
    while ((frames < 6 || editor->thumbnails().busy()) && frames < 4000) {
      filling.push_back(milliseconds(*editor));
      ++frames;
    }
    REQUIRE_FALSE(editor->thumbnails().busy());
    WARN((cached ? "second run, read from the cache" : "first run, rendered")
         << ": " << editor->thumbnails().rendered() << " pictures rendered");
    report("  frames with nothing to make", idle);
    report("  frames while the folder fills in", filling);
  }
  fixture.device->waitIdle();
}

TEST_CASE("a prefab's picture is rendered and is as old as the prefabs it nests", "[editor][gpu][grid][prefab]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("assets_prefab_thumbnails");
  const std::filesystem::path preferences = scratch("assets_prefab_thumbnails_preferences") / "preferences.json";
  std::filesystem::create_directories(preferences.parent_path());
  core::Uuid inner;
  core::Uuid outer;
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->createProject(directory, "Prefabs").has_value());
    world::World &world = editor->world();
    core::Uuid box;
    for (const flecs::entity root : world.roots()) {
      if (root.get<world::Name>().value == "Box") {
        box = world.uuidOf(root);
      }
    }
    REQUIRE_FALSE(box.isNil());
    REQUIRE(editor->saveAsPrefab(box).has_value());
    inner = world.uuidOf(world.prefabOf(world.find(box)));
    // The entity is an instance of the first prefab now, so the second one nests it.
    REQUIRE(editor->saveAsPrefab(box).has_value());
    outer = world.uuidOf(world.prefabOf(world.find(box)));
    showGrid(*editor, directory / "prefabs");
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() == 2);
    CHECK(editor->thumbnails().resident() == 2);
    CHECK(std::filesystem::exists(assets::thumbnailFile(editor->assets().cacheDirectory(), inner)));
    CHECK(std::filesystem::exists(assets::thumbnailFile(editor->assets().cacheDirectory(), outer)));
  }
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->openProject(directory).has_value());
    showGrid(*editor, directory / "prefabs");
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() == 0);
    CHECK(editor->thumbnails().resident() == 2);
  }
  // The nested prefab's file changed since: its own picture is stale, and so is the one of the prefab that places it.
  std::filesystem::last_write_time(directory / "prefabs" / "Box.prefab.json",
                                   std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
  {
    const auto editor = openEditor(fixture, preferences);
    REQUIRE(editor->openProject(directory).has_value());
    showGrid(*editor, directory / "prefabs");
    settleThumbnails(fixture, *editor);
    CHECK(editor->thumbnails().rendered() == 2);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("each scene tab keeps its own viewport camera", "[editor][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  const glm::vec3 home = editor.viewport().camera().camera().position;
  const glm::vec3 a{-20.0f, 4.0f, 9.0f};
  editor.viewport().camera().lookAt(a, {0.0f, 0.0f, 0.0f});
  editor.viewport().camera().scaleSpeed(2.0f);
  const float speed = editor.viewport().camera().speed();
  fixture.frame(editor);

  // A new scene starts from the default camera, not from where the last one was left.
  editor.newScene();
  REQUIRE(editor.viewport().camera().camera().position == home);
  const glm::vec3 b{3.0f, 12.0f, -6.0f};
  editor.viewport().camera().lookAt(b, {0.0f, 0.0f, 0.0f});
  fixture.frame(editor);

  editor.switchToTab(0);
  CHECK(editor.viewport().camera().camera().position == a);
  CHECK(editor.viewport().camera().speed() == Approx(speed));
  editor.switchToTab(1);
  CHECK(editor.viewport().camera().camera().position == b);

  // Playing and stopping leaves the camera where it was.
  editor.play();
  fixture.frame(editor);
  editor.stop();
  CHECK(editor.viewport().camera().camera().position == b);
}

TEST_CASE("quitting asks about unsaved scenes before it exits", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("quit");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Quit").has_value());

    // Nothing unsaved: quit at once, with no prompt.
    REQUIRE(editor.dirtyTabs().empty());
    editor.requestQuit();
    REQUIRE(editor.quitRequested());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory / "again", "Quit").has_value());
    world::World &world = editor.world();
    const auto rename = [&](std::string after) {
      const core::Uuid root = world.uuidOf(world.roots().front());
      const std::string before = world.roots().front().get<world::Name>().value;
      editor.commands().push(editor::renameCommand(root, before, std::move(after)), world);
    };
    rename("edited");
    editor.newScene(); // tab 1, untitled and clean
    REQUIRE(editor.saveSceneAs(directory / "again" / "scenes" / "second.scene.json").has_value());
    rename("edited too");
    editor.newScene(); // tab 2, untitled, edited
    rename("untitled edit");
    editor.switchToTab(1);
    REQUIRE(editor.dirtyTabs() == std::vector<std::size_t>{0, 1, 2});
    fixture.frame(editor);

    // Dirty: the prompt opens instead, once however often it is asked, and Cancel keeps running.
    editor.requestQuit();
    editor.requestQuit();
    REQUIRE(!editor.quitRequested());
    REQUIRE(editor.quitPromptOpen());
    fixture.frame(editor);
    editor.cancelQuit();
    REQUIRE(!editor.quitPromptOpen());
    REQUIRE(!editor.quitRequested());
    REQUIRE(editor.activeTab() == 1);
    fixture.frame(editor);

    // Save All stops at the untitled scene, which stays in front, and reports it.
    editor.requestQuit();
    editor.saveAllAndQuit();
    REQUIRE(!editor.quitRequested());
    REQUIRE(editor.quitPromptOpen());
    REQUIRE(editor.activeTab() == 2);
    REQUIRE(!editor.tabDirty(0));
    REQUIRE(!editor.tabDirty(1));
    fixture.frame(editor);
    editor.cancelQuit();

    // With the untitled scene given a file, Save All saves the rest and quits.
    REQUIRE(editor.saveSceneAs(directory / "again" / "scenes" / "third.scene.json").has_value());
    rename("more");
    editor.switchToTab(0);
    rename("again");
    REQUIRE(editor.dirtyTabs() == std::vector<std::size_t>{0, 2});
    editor.requestQuit();
    editor.saveAllAndQuit();
    REQUIRE(editor.quitRequested());
    REQUIRE(editor.dirtyTabs().empty());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  {
    // Discard quits with the scenes as they were.
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory / "discard", "Quit").has_value());
    world::World &world = editor.world();
    editor.commands().push(editor::renameCommand(world.uuidOf(world.roots().front()), "a", "b"), world);
    editor.requestQuit();
    REQUIRE(editor.quitPromptOpen());
    editor.discardAndQuit();
    REQUIRE(editor.quitRequested());
    REQUIRE(editor.isDirty());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("the scene is unsaved exactly when it differs from the saved one", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("dirty");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Dirty").has_value());
    world::World &world = editor.world();
    const core::Uuid root = world.uuidOf(world.roots().front());
    const auto rename = [&](std::string before, std::string after) {
      editor.commands().push(editor::renameCommand(root, std::move(before), std::move(after)), world);
    };
    const std::string name = world.roots().front().get<world::Name>().value;
    REQUIRE(!editor.isDirty());

    // Undoing back to the saved state is clean, redoing away from it is dirty.
    rename(name, "one");
    REQUIRE(editor.isDirty());
    REQUIRE(editor.commands().undo(world));
    REQUIRE(!editor.isDirty());
    REQUIRE(editor.commands().redo(world));
    REQUIRE(editor.isDirty());

    // Saved with an edit in the history, an edit and its undo are clean again.
    REQUIRE(editor.saveScene().has_value());
    REQUIRE(!editor.isDirty());
    rename("one", "two");
    REQUIRE(editor.isDirty());
    REQUIRE(editor.commands().undo(world));
    REQUIRE(!editor.isDirty());

    // A push that drops the saved redo branch stays dirty, even back at the same depth.
    REQUIRE(editor.commands().undo(world));
    REQUIRE(editor.isDirty());
    rename(name, "three");
    REQUIRE(editor.isDirty());
    REQUIRE(editor.commands().undo(world));
    REQUIRE(editor.isDirty());
    REQUIRE(editor.saveScene().has_value());
    REQUIRE(!editor.isDirty());

    // Play, edit and stop returns to the state before play: clean...
    editor.play();
    rename(name, "playing");
    REQUIRE(editor.isDirty());
    editor.stop();
    REQUIRE(!editor.isDirty());

    // ... or dirty.
    rename(name, "kept");
    REQUIRE(editor.isDirty());
    editor.play();
    editor.stop();
    REQUIRE(editor.isDirty());
    editor.play();
    rename(name, "playing");
    editor.stop();
    REQUIRE(editor.isDirty());

    // Saving while playing writes the snapshot, so the stopped scene matches the file.
    editor.play();
    REQUIRE(editor.saveScene().has_value());
    editor.stop();
    REQUIRE(!editor.isDirty());

    // Each tab keeps its own state across switches.
    rename(name, "tab one");
    editor.newScene();
    REQUIRE(!editor.isDirty());
    REQUIRE(editor.tabDirty(0));
    editor.commands().push(editor::createEntityCommand("Extra", {}, {}), world);
    REQUIRE(editor.commands().undo(world));
    REQUIRE(!editor.tabDirty(1));
    editor.switchToTab(0);
    REQUIRE(editor.isDirty());
    REQUIRE(!editor.tabDirty(1));
    REQUIRE(editor.commands().undo(world));
    REQUIRE(!editor.isDirty());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("viewport mouse look keeps motion out of ImGui while turning the camera", "[editor][gpu]") {
  Fixture fixture;
  editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
  fixture.frame(editor);
  fixture.frame(editor); // the dock builder settles the viewport's size on its second frame

  const editor::ViewportInput &input = editor.viewport().input();
  const ImVec2 start{std::floor(input.origin.x + input.size.x * 0.5f),
                     std::floor(input.origin.y + input.size.y * 0.5f)};
  SDL_WarpMouseInWindow(fixture.window->nativeHandle(), start.x, start.y);
  SDL_Event motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.windowID = SDL_GetWindowID(fixture.window->nativeHandle());
  motion.motion.x = start.x;
  motion.motion.y = start.y;
  editor.nativeEvent(motion);

  SDL_Event button{};
  button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  button.button.windowID = motion.motion.windowID;
  button.button.button = SDL_BUTTON_RIGHT;
  editor.nativeEvent(button);
  fixture.frame(editor);
  REQUIRE(editor.viewport().cameraActive());
  const ImVec2 heldPosition = ImGui::GetIO().MousePos;
  const float initialYaw = editor.viewport().camera().yaw();

  motion.motion.x += 200.0f;
  motion.motion.y += 100.0f;
  editor.nativeEvent(motion);
  editor.event(platform::MouseMoved{{motion.motion.x, motion.motion.y}, {200.0f, 100.0f}});
  fixture.frame(editor);
  REQUIRE(ImGui::GetIO().MousePos.x == Approx(heldPosition.x));
  REQUIRE(ImGui::GetIO().MousePos.y == Approx(heldPosition.y));
  REQUIRE(editor.viewport().camera().yaw() != Approx(initialYaw));

  button.type = SDL_EVENT_MOUSE_BUTTON_UP;
  editor.nativeEvent(button);
  fixture.frame(editor);
  REQUIRE_FALSE(editor.viewport().cameraActive());
  float x = 0.0f;
  float y = 0.0f;
  SDL_GetMouseState(&x, &y);
  REQUIRE(x == Approx(start.x));
  REQUIRE(y == Approx(start.y));
  fixture.frame(editor);
  REQUIRE(ImGui::GetIO().MousePos.x == Approx(start.x));
  REQUIRE(ImGui::GetIO().MousePos.y == Approx(start.y));
}

TEST_CASE("the editor opens a project, edits, plays, stops and saves", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("project");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Test").has_value());
    REQUIRE(editor.project().has_value());
    REQUIRE(editor.scenePath() == editor.project()->resolve(editor.project()->startScene));
    REQUIRE(!editor.isDirty());
    fixture.frame(editor);

    // An edit through the command stack makes the scene dirty; saving clears it.
    world::World &world = editor.world();
    const flecs::entity box = world.find([&] {
      for (const flecs::entity root : world.roots()) {
        if (root.get<world::Name>().value == "Box") {
          return world.uuidOf(root);
        }
      }
      return core::Uuid{};
    }());
    REQUIRE(box.is_valid());
    editor.selection().select(world.uuidOf(box));
    box.set<world::Spin>({.speed = glm::radians(90.0f)});
    editor.commands().push(
        editor::componentCommand(world.uuidOf(box), "Spin", std::nullopt, std::make_optional(nlohmann::json{}), "add"),
        world);
    REQUIRE(editor.isDirty());
    fixture.frame(editor); // the selection outline and id pass run with a selection
    REQUIRE(editor.saveScene().has_value());
    REQUIRE(!editor.isDirty());

    // Play advances the simulation; stop restores the snapshot, including the rotation.
    const glm::quat before = box.get<world::Transform>().rotation;
    editor.play();
    REQUIRE(editor.isPlaying());
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(world.find(world.uuidOf(box)).get<world::Transform>().rotation != before);
    const core::Uuid boxUuid = world.uuidOf(box);
    editor.stop();
    REQUIRE(!editor.isPlaying());
    REQUIRE(world.find(boxUuid).get<world::Transform>().rotation == before);
    REQUIRE(editor.selection().primary() == boxUuid); // survives by identity
    REQUIRE(!editor.isDirty());
    fixture.frame(editor);

    // Reopening the project reloads the saved scene with the added component.
    REQUIRE(editor.openProject(directory).has_value());
    REQUIRE(world.find(boxUuid).has<world::Spin>());
    REQUIRE(!editor.openProject(scratch("nowhere")).has_value());
    editor.newScene();
    REQUIRE(editor.scenePath().empty());
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("selecting a parent outlines its whole subtree", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    world::World &world = editor.world();
    const flecs::entity parent = world.createEntity("Parent");
    const flecs::entity child = world.createEntity("Child", parent);
    const flecs::entity grandchild = world.createEntity("Grandchild", child);
    const flecs::entity other = world.createEntity("Other");
    const auto outlined = [&] {
      fixture.frame(editor);
      std::vector<std::uint32_t> ids(editor.outlineIds().begin(), editor.outlineIds().end());
      std::ranges::sort(ids);
      return ids;
    };
    const auto sortedIds = [&](std::initializer_list<flecs::entity> entities) {
      std::vector<std::uint32_t> ids;
      for (const flecs::entity entity : entities) {
        ids.push_back(world::World::pickId(entity));
      }
      std::ranges::sort(ids);
      return ids;
    };

    editor.selection().select(world.uuidOf(parent));
    REQUIRE(outlined() == sortedIds({parent, child, grandchild}));
    editor.selection().select(world.uuidOf(child));
    REQUIRE(outlined() == sortedIds({child, grandchild}));
    // The sibling is untouched, and a selected descendant of a selected parent is listed twice at
    // most, which the renderer folds.
    editor.selection().select(world.uuidOf(other));
    editor.selection().select(world.uuidOf(parent), editor::Selection::Mode::Add);
    editor.selection().select(world.uuidOf(child), editor::Selection::Mode::Add);
    REQUIRE(outlined() == sortedIds({other, parent, child, child, grandchild, grandchild}));
    editor.selection().clear();
    REQUIRE(outlined().empty());
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("focusing frames the meshes under the selection by their bounds", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    world::World &world = editor.world();
    // An empty parent with a box two metres wide ten metres away: the scale of the parent says
    // nothing about where the geometry is.
    const flecs::entity parent = world.createEntity("Parent");
    const flecs::entity child = world.createEntity("Child", parent);
    child.set(world::MeshRenderer{});
    child.set(world::Transform{.position = {10.0f, 0.0f, 0.0f}, .scale = glm::vec3{2.0f}});
    for (int i = 0; i < 4; ++i) {
      fixture.frame(editor);
    }
    editor.selection().select(world.uuidOf(parent));
    editor.focusSelection();
    const glm::vec3 position = editor.viewport().camera().camera().position;
    // The box's half-diagonal is sqrt(3), and the camera sits 2.5 radii away.
    REQUIRE(glm::distance(position, glm::vec3{10.0f, 0.0f, 0.0f}) == Approx(std::sqrt(3.0f) * 2.5f).margin(0.05));

    // A camera turned to the sky must not end up beneath the object.
    editor.viewport().camera().lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 10.0f, -1.0f});
    editor.focusSelection();
    const renderer::Camera &skyward = editor.viewport().camera().camera();
    REQUIRE(skyward.position.y > 0.0f);
    REQUIRE(glm::distance(skyward.position, glm::vec3{10.0f, 0.0f, 0.0f}) ==
            Approx(std::sqrt(3.0f) * 2.5f).margin(0.05));
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("middle-dragging the viewport pans the camera and Alt + left-dragging orbits it around the focused object",
          "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    world::World &world = editor.world();
    const flecs::entity box = world.createEntity("Box");
    box.set(world::MeshRenderer{});
    box.set(world::Transform{.position = {4.0f, 1.0f, -2.0f}});
    for (int i = 0; i < 4; ++i) {
      fixture.frame(editor);
    }
    editor.selection().select(world.uuidOf(box));
    editor.focusSelection();
    const auto position = [&] { return editor.viewport().camera().camera().position; };
    const glm::vec3 pivot{4.0f, 1.0f, -2.0f};
    const glm::vec3 before = position();

    const editor::ViewportInput &input = editor.viewport().input();
    const ImVec2 start{std::floor(input.origin.x + input.size.x * 0.5f),
                       std::floor(input.origin.y + input.size.y * 0.5f)};
    SDL_WarpMouseInWindow(fixture.window->nativeHandle(), start.x, start.y);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = SDL_GetWindowID(fixture.window->nativeHandle());
    motion.motion.x = start.x;
    motion.motion.y = start.y;
    editor.nativeEvent(motion);
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = motion.motion.windowID;
    button.button.button = SDL_BUTTON_MIDDLE;
    editor.nativeEvent(button);
    fixture.frame(editor);
    REQUIRE(editor.viewport().cameraActive());

    const auto drag = [&] {
      motion.motion.x += 150.0f;
      editor.nativeEvent(motion);
      editor.event(platform::MouseMoved{{motion.motion.x, motion.motion.y}, {150.0f, 0.0f}});
      fixture.frame(editor);
    };
    const glm::vec3 forward = editor.viewport().camera().camera().forward();
    drag();
    const glm::vec3 panned = position() - before;
    REQUIRE(glm::length(panned) > 0.5f);
    REQUIRE(glm::dot(editor.viewport().camera().camera().forward(), forward) == Approx(1.0f)); // slid, not turned
    REQUIRE(std::abs(glm::dot(panned, forward)) < 0.001f);                                     // in the view plane

    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    editor.nativeEvent(button);
    fixture.frame(editor);
    REQUIRE_FALSE(editor.viewport().cameraActive());

    // The focused object travelled with the camera, so Alt + left turns around it where it is now.
    const glm::vec3 target = pivot + panned;
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.button = SDL_BUTTON_LEFT;
    editor.nativeEvent(button);
    fixture.frame(editor);
    REQUIRE(editor.viewport().cameraActive());
    const glm::vec3 beforeOrbit = position();
    drag();
    REQUIRE(glm::distance(position(), beforeOrbit) > 0.5f);
    REQUIRE(glm::distance(position(), target) == Approx(glm::distance(beforeOrbit, target)).margin(0.01));

    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    editor.nativeEvent(button);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    fixture.frame(editor);
    REQUIRE_FALSE(editor.viewport().cameraActive());
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("the wheel over the viewport dollies the camera unless the right button or the game has it",
          "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    for (int i = 0; i < 4; ++i) {
      fixture.frame(editor);
    }
    ImGuiIO &io = ImGui::GetIO();
    const editor::ViewportInput &input = editor.viewport().input();
    REQUIRE(input.visible);
    editor.viewport().camera().lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
    // Hover a point in the image, away from the gizmo and the overlay.
    io.AddMousePosEvent(input.origin.x + input.size.x * 0.9f, input.origin.y + input.size.y * 0.9f);
    fixture.frame(editor);
    REQUIRE(input.hovered);
    const auto z = [&] { return editor.viewport().camera().camera().position.z; };

    io.AddMouseWheelEvent(0.0f, -1.0f);
    fixture.frame(editor);
    REQUIRE(z() > 10.0f); // scrolling down moves back
    io.AddMouseWheelEvent(0.0f, 1.0f);
    fixture.frame(editor);
    REQUIRE(z() == Approx(10.0f)); // and up moves forward again by the same step

    // Playing with the viewport focused: the wheel is the game's.
    editor.viewport().camera().lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
    editor.play();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); // clicking into the viewport focuses it
    fixture.frame(editor);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(editor.viewport().input().focused);
    io.AddMouseWheelEvent(0.0f, 1.0f);
    fixture.frame(editor);
    REQUIRE(z() == Approx(10.0f));
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

// Each member of a component is a two-column table: the name at 90 px, then the widget told to
// fill the rest. On the first frame after a selection the value column used to fall to ImGui's
// minimum width, so every field drew as a sliver, and grew back over the next frames.
TEST_CASE("the inspector gives its value columns the width the label leaves", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    world::World &world = editor.world();
    const flecs::entity sun = world.createEntity("Lit");
    sun.set<world::DirectionalLight>({});
    // The panels laid out first, then the frame after the selection: the tables' first, when their
    // value column had no auto width yet and its weight came out as 0/0.
    for (int i = 0; i < 3; ++i) {
      fixture.frame(editor);
    }
    editor.selection().select(world.uuidOf(sun));
    fixture.frame(editor);
    ImGuiContext &context = *ImGui::GetCurrentContext();
    int members = 0;
    for (int i = 0; i < context.Tables.GetMapSize(); ++i) {
      const ImGuiTable *table = context.Tables.TryGetMapData(i);
      if (table == nullptr || table->ColumnsCount != 2 || table->LastFrameActive < context.FrameCount - 1 ||
          !std::string_view{table->OuterWindow->Name}.starts_with("Inspector")) {
        continue;
      }
      ++members;
      CAPTURE(members);
      const float left = table->OuterRect.GetWidth() - table->Columns[0].WidthGiven;
      CAPTURE(table->OuterRect.GetWidth(), table->Columns[1].WidthGiven);
      REQUIRE(table->Columns[0].WidthGiven == Approx(90.0f));
      // The value column takes what the label leaves, less the spacing between cells; the bug left
      // it at ImGui's four-pixel minimum.
      REQUIRE(table->Columns[1].WidthGiven >= left / 2.0f);
    }
    // The Transform's three and the light's two.
    REQUIRE(members >= 5);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("clicking the menu bar's play button toggles play mode", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(!editor.isPlaying());
    // The button is centred on the main menu bar, which is one frame height tall.
    const ImGuiStyle &style = ImGui::GetStyle();
    const float x = static_cast<float>(fixture.window->size().x) * 0.5f + style.FramePadding.x;
    const float y = ImGui::GetFrameHeight() * 0.5f;
    ImGuiIO &io = ImGui::GetIO();
    const auto click = [&] {
      io.AddMousePosEvent(x, y);
      fixture.frame(editor);
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
      fixture.frame(editor);
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      fixture.frame(editor); // the click lands, and the state flips mid-frame
      fixture.frame(editor);
    };
    click();
    REQUIRE(editor.isPlaying());
    click();
    REQUIRE(!editor.isPlaying());
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("a mouse drag on the gizmo's X handle moves the selected entity", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    world::World &world = editor.world();
    flecs::entity box;
    for (const flecs::entity root : world.roots()) {
      if (root.get<world::Name>().value == "Box") {
        box = root;
      }
    }
    REQUIRE(box.is_valid());
    editor.selection().select(world.uuidOf(box));
    // Three frames so the dock layout and the scene tab bar settle and the viewport reports its
    // rectangle.
    fixture.frame(editor);
    fixture.frame(editor);
    fixture.frame(editor);
    const editor::ViewportInput &input = editor.viewport().input();
    REQUIRE(input.visible);
    REQUIRE(input.size.x > 100.0f);

    const auto viewOf = [&](glm::vec2 mouse) {
      const renderer::Camera &camera = editor.viewport().camera().camera();
      const glm::uvec2 targetSize = editor.viewport().target().size();
      return editor::GizmoView{
          .view = camera.view(),
          .projection = camera.projection(static_cast<float>(targetSize.x) / static_cast<float>(targetSize.y)),
          .cameraPosition = camera.position,
          .origin = input.origin,
          .size = input.size,
          .mouse = mouse};
    };
    const glm::vec3 boxPosition{box.get<world::WorldTransform>().matrix[3]};
    const float length = glm::distance(viewOf({}).cameraPosition, boxPosition) * 0.18f;
    const auto handle = editor::Gizmo::project(viewOf({}), boxPosition + glm::vec3{length * 0.6f, 0.0f, 0.0f});
    const auto target = editor::Gizmo::project(viewOf({}), boxPosition + glm::vec3{length * 0.6f + 1.0f, 0.0f, 0.0f});
    REQUIRE(handle.has_value());
    REQUIRE(target.has_value());

    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(handle->x, handle->y);
    fixture.frame(editor);
    REQUIRE(editor.gizmo().hoveredAxis() == editor::GizmoAxis::X);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    fixture.frame(editor);
    REQUIRE(editor.gizmo().isDragging());
    io.AddMousePosEvent(target->x, target->y);
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(box.get<world::Transform>().position.x == Approx(1.0f).margin(0.05f));
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    fixture.frame(editor);
    REQUIRE(!editor.gizmo().isDragging());
    REQUIRE(editor.commands().canUndo());
    REQUIRE(editor.commands().undoDescription() == "move Box");
    REQUIRE(box.get<world::Transform>().position.x == Approx(1.0f).margin(0.05f));
    REQUIRE(box.get<world::WorldTransform>().matrix[3].x == Approx(1.0f).margin(0.05f));
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("viewport view labels keep a contrasting backing at each font scale", "[editor][gpu][view-label]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    for (int frame = 0; frame < 3; ++frame) {
      fixture.frame(editor);
    }
    for (const float scale : {1.0f, 1.5f, 2.0f}) {
      ImGui::GetStyle().FontScaleMain = scale;
      for (const editor::AxisView axis :
           {editor::AxisView::User, editor::AxisView::Front, editor::AxisView::Back, editor::AxisView::Left,
            editor::AxisView::Right, editor::AxisView::Top, editor::AxisView::Bottom}) {
        editor.viewport().setView({});
        editor.viewport().snapView(axis);
        for (const renderer::ProjectionKind projection :
             {renderer::ProjectionKind::Perspective, renderer::ProjectionKind::Orthographic}) {
          if (editor.viewport().camera().camera().projectionKind != projection) {
            editor.viewport().toggleProjection();
          }
          CAPTURE(scale, axis, projection);
          fixture.frame(editor);
          fixture.frame(editor);
          const auto &input = editor.viewport().input();
          const glm::uvec2 size = editor.viewport().target().size();
          REQUIRE(size == glm::uvec2{input.size});
          const ImDrawList *drawList = ImGui::FindWindowByName("Viewport")->DrawList;
          ImVec2 min{FLT_MAX, FLT_MAX};
          ImVec2 max{-FLT_MAX, -FLT_MAX};
          int backingVertices = 0;
          int textVertices = 0;
          for (const ImDrawVert &vertex : drawList->VtxBuffer) {
            if (vertex.col == IM_COL32(0, 0, 0, 200)) {
              min.x = std::min(min.x, vertex.pos.x);
              min.y = std::min(min.y, vertex.pos.y);
              max.x = std::max(max.x, vertex.pos.x);
              max.y = std::max(max.y, vertex.pos.y);
              ++backingVertices;
            }
          }
          REQUIRE(backingVertices > 0);
          REQUIRE(min.x >= input.origin.x);
          REQUIRE(min.y >= input.origin.y);
          REQUIRE(max.x <= input.origin.x + input.size.x);
          REQUIRE(max.y <= input.origin.y + input.size.y);
          for (const ImDrawVert &vertex : drawList->VtxBuffer) {
            if (vertex.col == IM_COL32(240, 240, 240, 255)) {
              REQUIRE(vertex.pos.x > min.x);
              REQUIRE(vertex.pos.y > min.y);
              REQUIRE(vertex.pos.x < max.x);
              REQUIRE(vertex.pos.y < max.y);
              ++textVertices;
            }
          }
          REQUIRE(textVertices > 0);

          // Clicking the label still belongs to the viewport, with the same image rectangle.
          ImGuiIO &io = ImGui::GetIO();
          io.AddMousePosEvent((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
          fixture.frame(editor);
          io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
          fixture.frame(editor);
          REQUIRE(editor.viewport().input().hovered);
          REQUIRE(editor.viewport().cameraActive());
          REQUIRE(editor.viewport().target().size() == size);
          io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
          fixture.frame(editor);
          REQUIRE_FALSE(editor.viewport().cameraActive());
        }
      }
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("capture viewport label contrast over the Basic sample", "[.view-label-captures][editor][gpu]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest.has_value());
  const auto directory = scratch("view-label-basic");
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    for (int frame = 0; frame < 240; ++frame) {
      fixture.frame(editor);
      if (frame > 3 && !editor.assets().loading()) {
        break;
      }
    }
    REQUIRE_FALSE(editor.assets().loading());
    ImGui::LoadIniSettingsFromMemory("[SonnetView][View]\nOverlay=0\n");
    REQUIRE_FALSE(editor.viewState().overlay);
    const auto output = std::filesystem::temp_directory_path() / "sonnet-view-label-captures";
    std::filesystem::create_directories(output);
    for (const float scale : {1.0f, 2.0f}) {
      ImGui::GetStyle().FontScaleMain = scale;
      for (const editor::AxisView axis : {editor::AxisView::Front, editor::AxisView::Top, editor::AxisView::Bottom}) {
        for (const flecs::entity root : editor.world().roots()) {
          if (root.has<world::Environment>()) {
            auto environment = root.get<world::Environment>();
            environment.intensity = axis == editor::AxisView::Bottom ? 0.0f : 1.0f;
            root.set<world::Environment>(environment);
          }
        }
        editor.viewport().snapView(axis);
        for (int frame = 0; frame < 10; ++frame) {
          fixture.frame(editor);
        }
        const std::string name = std::string{editor.viewport().camera().viewName()} + (scale == 1.0f ? "-100" : "-200");
        editor.requestScreenshots({}, output / (name + ".png"));
        fixture.frame(editor);
        const auto result = editor.takeScreenshotResult();
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
      }
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("orthographic picking and translate drag select and move the box", "[editor][gpu][orthographic]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    SECTION("Ctrl shortcuts") {
      ImGui::GetIO().ConfigMacOSXBehaviors = false;
    }
    SECTION("Cmd shortcuts") {
      ImGui::GetIO().ConfigMacOSXBehaviors = true;
    }
    world::World &world = editor.world();
    flecs::entity box;
    for (const flecs::entity root : world.roots()) {
      if (root.get<world::Name>().value == "Box") {
        box = root;
      }
    }
    REQUIRE(box.is_valid());

    // Three frames so the dock layout and the scene tab bar settle and the viewport reports its
    // rectangle.
    fixture.frame(editor);
    fixture.frame(editor);
    fixture.frame(editor);
    const editor::ViewportInput &input = editor.viewport().input();
    REQUIRE(input.visible);
    REQUIRE(input.size.x > 100.0f);
    ImGuiIO &keys = ImGui::GetIO();
    keys.AddMousePosEvent(input.origin.x + input.size.x * 0.5f, input.origin.y + input.size.y * 0.5f);
    fixture.frame(editor);
    keys.AddKeyEvent(ImGuiKey_Keypad7, true);
    fixture.frame(editor);
    REQUIRE(editor.viewport().camera().axisView() == editor::AxisView::Top);
    REQUIRE(editor.viewport().camera().camera().projectionKind == renderer::ProjectionKind::Orthographic);
    keys.AddKeyEvent(ImGuiKey_Keypad7, false);
    keys.AddKeyEvent(ImGuiKey_Keypad1, true);
    fixture.frame(editor);
    keys.AddKeyEvent(ImGuiKey_Keypad1, false);
    fixture.frame(editor);
    REQUIRE(editor.viewport().camera().axisView() == editor::AxisView::Front);

    const auto viewOf = [&](glm::vec2 mouse) {
      const renderer::Camera &camera = editor.viewport().camera().camera();
      const glm::uvec2 targetSize = editor.viewport().target().size();
      return editor::GizmoView{
          .view = camera.view(),
          .projection = camera.projection(static_cast<float>(targetSize.x) / static_cast<float>(targetSize.y)),
          .cameraPosition = camera.position,
          .origin = input.origin,
          .size = input.size,
          .mouse = mouse};
    };
    const glm::vec3 boxPosition{box.get<world::WorldTransform>().matrix[3]};
    const auto centre = editor::Gizmo::project(viewOf({}), boxPosition);
    REQUIRE(centre.has_value());
    ImGuiIO &pickIo = ImGui::GetIO();
    pickIo.AddMousePosEvent(centre->x, centre->y);
    fixture.frame(editor);
    pickIo.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    fixture.frame(editor);
    pickIo.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    for (int i = 0; i < 5; ++i) {
      fixture.frame(editor);
    }
    REQUIRE(editor.selection().primary() == world.uuidOf(box));
    const float length = editor.viewport().camera().camera().orthoHeight * 0.18f;
    const auto handle = editor::Gizmo::project(viewOf({}), boxPosition + glm::vec3{length * 0.6f, 0.0f, 0.0f});
    const auto target = editor::Gizmo::project(viewOf({}), boxPosition + glm::vec3{length * 0.6f + 1.2f, 0.0f, 0.0f});
    REQUIRE(handle.has_value());
    REQUIRE(target.has_value());

    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent(handle->x, handle->y);
    fixture.frame(editor);
    REQUIRE(editor.gizmo().hoveredAxis() == editor::GizmoAxis::X);
    // ImGui swaps Ctrl and Super on macOS; physical Ctrl-left-click becomes a right-click.
    const ImGuiKey snapModifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    io.AddKeyEvent(snapModifier, true);
    fixture.frame(editor);
    REQUIRE(io.KeyCtrl);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    fixture.frame(editor);
    REQUIRE(io.MouseDown[ImGuiMouseButton_Left]);
    REQUIRE(editor.gizmo().isDragging());
    io.AddMousePosEvent(target->x, target->y);
    fixture.frame(editor);
    fixture.frame(editor);
    REQUIRE(box.get<world::Transform>().position.x == Approx(1.0f).margin(0.05f));
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    fixture.frame(editor);
    REQUIRE(!editor.gizmo().isDragging());
    REQUIRE(editor.commands().canUndo());
    REQUIRE(editor.commands().undoDescription() == "move Box");
    REQUIRE(box.get<world::Transform>().position.x == Approx(1.0f).margin(0.05f));
    REQUIRE(box.get<world::WorldTransform>().matrix[3].x == Approx(1.0f).margin(0.05f));
    io.AddKeyEvent(snapModifier, false);
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("the asset browser lists a project's assets and the inspector edits a material", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("assets");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Assets").has_value());
    assets::MaterialSource painted;
    painted.baseColor = {0.2f, 0.4f, 0.6f, 1.0f};
    const auto material = editor.assets().createMaterial(directory / "assets" / "painted.material.json", painted);
    REQUIRE(material.has_value());
    REQUIRE(editor.assets().find(*material) != nullptr);
    REQUIRE(editor::assetLabel(editor.assets(), *material) == "painted (Material)");
    REQUIRE(editor::assetLabel(editor.assets(), {}) == "(none)");
    REQUIRE(editor::assetLabel(editor.assets(), core::Uuid::generate()) == "(missing)");
    REQUIRE(editor::InspectorPanel::assetTypeOfMember("mesh") == assets::AssetType::Mesh);
    REQUIRE(editor::InspectorPanel::assetTypeOfMember("map") == assets::AssetType::Environment);
    REQUIRE(!editor::InspectorPanel::assetTypeOfMember("speed").has_value());

    // The browser inspects the material; the inspector draws it and the box uses it.
    world::World &world = editor.world();
    REQUIRE(!world.roots().empty());
    const core::Uuid selected = world.uuidOf(world.roots().front());
    editor.selection().select(selected);
    editor.assetBrowser().inspect(*material);
    fixture.frame(editor);
    fixture.frame(editor); // the Asset tab's SetSelected lands a frame late
    REQUIRE(editor.selection().primary() == selected);
    REQUIRE(editor.assetBrowser().inspected() == *material);
    for (const flecs::entity root : world.roots()) {
      if (root.has<world::MeshRenderer>()) {
        root.ensure<world::MeshRenderer>().material = *material;
      }
    }
    fixture.frame(editor);
    REQUIRE(editor.commands().size() == 0);
    // Reopening the project keeps the material's identity through its sidecar-less file.
    REQUIRE(editor.openProject(directory).has_value());
    REQUIRE(editor.assets().find(*material) != nullptr);
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("the editor recompiles the engine shaders from the checkout's sources", "[editor][gpu]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    fixture.frame(editor);
    // The test binary lives in a build directory inside the checkout, where the sources are found.
    const auto reloaded = editor.reloadShaders();
    if (!reloaded) {
      SKIP("shader sources not found: " << reloaded.error().message);
    }
    fixture.frame(editor);
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("exporting a project writes a bundle and what runs it", "[editor][gpu]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("export-project");
  const std::filesystem::path out = scratch("export-out");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Exported").has_value());
    // An export before a project is open is an error, not a half-written directory.
    fixture.frame(editor);

    const auto report = editor.exportProject({.outputDirectory = out,
                                              .platform = assets::CookPlatform::Linux,
                                              .playerDirectory = {}}); // the editor's own directory
    REQUIRE(report.has_value());
    REQUIRE(report->cook.bundle == out / "game.sbundle");
    REQUIRE(std::filesystem::exists(report->cook.bundle));
    REQUIRE(report->cook.fileCount == 1); // the starter scene
    // The starter scene is made of primitives, which every database registers for itself.
    REQUIRE(report->cook.assetCount == 0);
    // The engine shaders travel with the player; the test binary has them beside it.
    REQUIRE(report->supportFileCount > 0);
    REQUIRE(std::filesystem::is_directory(out / "shaders"));

    // The player binary is not next to a test binary, so that is a warning and the bundle is
    // still written: an export can be finished by dropping a player built elsewhere beside it.
    REQUIRE(report->player.empty());
    REQUIRE(!report->warnings.empty());
    REQUIRE(editor::playerFileName(assets::CookPlatform::Windows) == "sonnet_player.exe");
    REQUIRE(editor::playerFileName(assets::CookPlatform::Linux) == "sonnet_player");

    // What was exported is what the player opens: the manifest names the start scene by the
    // path the project gave it.
    const auto bundle = assets::Bundle::open(report->cook.bundle);
    REQUIRE(bundle.has_value());
    REQUIRE(bundle->manifest().name == "Exported");
    REQUIRE(bundle->manifest().platform == assets::CookPlatform::Linux);
    REQUIRE(bundle->contains(bundle->manifest().startScene));
    // Given a directory that does hold a player, it travels with the bundle and stays runnable.
    const std::filesystem::path players = scratch("export-players");
    std::filesystem::create_directories(players / "shaders");
    REQUIRE(core::writeFile(players / "sonnet_player", std::string_view{"#!/bin/sh\nexit 0\n"}).has_value());
    REQUIRE(core::writeFile(players / "shaders" / "forward.spv", std::string_view{"spv"}).has_value());
    const auto second = editor.exportProject(
        {.outputDirectory = out, .platform = assets::CookPlatform::Linux, .playerDirectory = players});
    REQUIRE(second.has_value());
    REQUIRE(second->player == out / "sonnet_player");
    REQUIRE(std::filesystem::exists(second->player));
    REQUIRE((std::filesystem::status(second->player).permissions() & std::filesystem::perms::owner_exec) !=
            std::filesystem::perms::none);
    REQUIRE(second->supportFileCount == 1); // the one shader that directory holds
    REQUIRE(second->warnings.empty());
    REQUIRE(editor.saveSceneAs(directory / "scenes" / "other.scene.json").has_value());
    const auto selected = editor.exportProject({.outputDirectory = out,
                                                .platform = assets::CookPlatform::Linux,
                                                .scene = editor.scenePath(),
                                                .playerDirectory = players});
    REQUIRE(selected.has_value());
    REQUIRE(selected->cook.fileCount == 1);
    const auto selectedBundle = assets::Bundle::open(selected->cook.bundle);
    REQUIRE(selectedBundle.has_value());
    REQUIRE(selectedBundle->manifest().startScene == "scenes/other.scene.json");
    REQUIRE(selectedBundle->contains("scenes/other.scene.json"));
    REQUIRE_FALSE(selectedBundle->contains("scenes/main.scene.json"));
    // A phone's export is the bundle alone, even from a directory that holds a desktop player.
    const std::filesystem::path phone = out / "android";
    const auto mobile = editor.exportProject(
        {.outputDirectory = phone, .platform = assets::CookPlatform::Android, .playerDirectory = players});
    REQUIRE(mobile.has_value());
    REQUIRE(mobile->player.empty());
    REQUIRE(mobile->supportFileCount == 0);
    REQUIRE(mobile->warnings.empty());
    REQUIRE(std::filesystem::exists(phone / "game.sbundle"));
    REQUIRE_FALSE(std::filesystem::exists(phone / "sonnet_player"));
    REQUIRE_FALSE(std::filesystem::exists(phone / "shaders"));
    const auto mobileBundle = assets::Bundle::open(mobile->cook.bundle);
    REQUIRE(mobileBundle.has_value());
    REQUIRE(mobileBundle->manifest().platform == assets::CookPlatform::Android);
    std::filesystem::remove_all(players);

    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
  std::filesystem::remove_all(out);
}

namespace {

std::size_t recoveryFiles(const std::filesystem::path &directory) {
  std::size_t count = 0;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
    count += (entry.is_regular_file() && entry.path().filename().string().ends_with(".recovery.scene.json")) ? 1u : 0u;
  }
  return count;
}

void deleteFirstRoot(editor::Editor &editor) {
  world::World &world = editor.world();
  editor.commands().push(editor::deleteEntityCommand(world.uuidOf(world.roots().front())), world);
}

// What a kill -9 leaves: the recovery directory as it is while the editor still runs.
std::filesystem::path snapshotOf(const std::filesystem::path &root, const char *name) {
  const std::filesystem::path copy = scratch(name);
  std::filesystem::copy(root, copy, std::filesystem::copy_options::recursive);
  return copy;
}

} // namespace

TEST_CASE("a killed editor offers its unsaved scene back and restores it exactly", "[editor][gpu][recovery]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("recovery_project");
  const std::filesystem::path root = scratch("recovery_root");
  nlohmann::json edited;
  std::filesystem::path killed;
  std::filesystem::path sceneFile;
  std::filesystem::file_time_type sceneTime;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Recovery").has_value());
    editor.setRecoveryDirectory(root);
    REQUIRE(!editor.recoveryPromptOpen());
    sceneFile = editor.scenePath();
    sceneTime = std::filesystem::last_write_time(sceneFile);
    const std::filesystem::path recoveryDirectory = editor.recovery()->directory();

    // Nothing is written for a scene that is not dirty, nor before the interval.
    editor.autosaveNow();
    REQUIRE(recoveryFiles(recoveryDirectory) == 0);
    deleteFirstRoot(editor);
    fixture.frame(editor);
    REQUIRE(recoveryFiles(recoveryDirectory) == 0);

    // The window losing the focus writes it, and the project's own scene file is left alone.
    editor.event(platform::WindowFocusChanged{false});
    REQUIRE(recoveryFiles(recoveryDirectory) == 1);
    REQUIRE((std::filesystem::last_write_time(sceneFile) == sceneTime));
    edited = world::saveScene(editor.world());

    // Not while playing: the scene is the snapshot.
    editor.commands().push(editor::deleteEntityCommand(editor.world().uuidOf(editor.world().roots().front())),
                           editor.world());
    editor.play();
    const std::filesystem::path written = std::filesystem::directory_iterator(recoveryDirectory)->path();
    const auto before = core::readFile(written);
    REQUIRE(before.has_value());
    editor.autosaveNow();
    REQUIRE(core::readFile(written).value() == *before);
    editor.stop();

    killed = snapshotOf(root, "recovery_killed");
  }
  // A normal exit left no recovery files, and nothing to ask about.
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    editor.setRecoveryDirectory(root);
    REQUIRE(!editor.recoveryPromptOpen());
    REQUIRE(recoveryFiles(editor.recovery()->directory()) == 0);
    fixture.frame(editor);
  }

  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    const std::size_t startRoots = editor.world().roots().size();
    editor.setRecoveryDirectory(killed);
    REQUIRE(editor.recoveryPromptOpen());
    REQUIRE(editor.recoverableScenes() == std::vector<std::string>{"main.scene.json"});
    fixture.frame(editor); // the dialog is drawn
    REQUIRE(editor.pathModalOpen() == false);

    // Until the answer nothing is written, whatever happens.
    deleteFirstRoot(editor);
    const std::size_t offered = recoveryFiles(editor.recovery()->directory());
    editor.autosaveNow();
    REQUIRE(recoveryFiles(editor.recovery()->directory()) == offered);
    REQUIRE(editor.commands().undo(editor.world()));
    REQUIRE(editor.world().roots().size() == startRoots);

    editor.restoreRecovered();
    REQUIRE(!editor.recoveryPromptOpen());
    REQUIRE(editor.tabCount() == 1); // the tab of the same file was replaced, not doubled
    REQUIRE(editor.scenePath() == sceneFile);
    REQUIRE(editor.isDirty());
    REQUIRE(world::saveScene(editor.world()) == edited);
    REQUIRE(editor.world().roots().size() == startRoots - 1);
    // The guard is in place while the scene proves itself, and no autosave overwrites the set.
    REQUIRE(std::filesystem::exists(editor.recovery()->markerFile()));
    REQUIRE(editor.recovery()->restoring());

    for (int frame = 0; frame < editor::Recovery::SettleFrames; ++frame) {
      fixture.frame(editor, false);
    }
    REQUIRE(!editor.recovery()->restoring());
    REQUIRE(!std::filesystem::exists(editor.recovery()->markerFile()));
    // The restored scene has a recovery file of its own now, and is still unsaved.
    REQUIRE(recoveryFiles(editor.recovery()->directory()) == 1);
    REQUIRE(editor.isDirty());
    REQUIRE((std::filesystem::last_write_time(sceneFile) == sceneTime));
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("saving or closing a tab, and quitting, remove its recovery file", "[editor][gpu][recovery]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("recovery_remove");
  const std::filesystem::path root = scratch("recovery_remove_root");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Remove").has_value());
    editor.setRecoveryDirectory(root);
    const std::filesystem::path recoveryDirectory = editor.recovery()->directory();

    deleteFirstRoot(editor);
    editor.autosaveNow();
    REQUIRE(recoveryFiles(recoveryDirectory) == 1);
    REQUIRE(editor.saveScene().has_value());
    REQUIRE(recoveryFiles(recoveryDirectory) == 0);

    editor.newScene();
    deleteFirstRoot(editor);
    editor.switchToTab(0);
    editor.autosaveNow(); // the inactive tab's file comes from its stashed scene
    REQUIRE(recoveryFiles(recoveryDirectory) == 1);
    editor.closeTab(1); // discarding it
    REQUIRE(recoveryFiles(recoveryDirectory) == 0);

    editor.newScene();
    deleteFirstRoot(editor);
    editor.autosaveNow();
    REQUIRE(recoveryFiles(recoveryDirectory) == 1);
    editor.requestQuit();
    REQUIRE(editor.quitPromptOpen());
    editor.discardAndQuit();
    REQUIRE(editor.quitRequested());
  }
  const std::filesystem::path key = std::filesystem::directory_iterator(root)->path();
  REQUIRE(recoveryFiles(key) == 0);
  for (const auto &entry : std::filesystem::directory_iterator(key)) {
    REQUIRE(!entry.path().filename().string().ends_with(".lock")); // the exit removed the session's lock
  }
}

TEST_CASE("the choices on the restore dialog", "[editor][gpu][recovery]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("recovery_choices");
  const std::filesystem::path root = scratch("recovery_choices_root");
  std::filesystem::path killed;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Choices").has_value());
    editor.setRecoveryDirectory(root);
    deleteFirstRoot(editor);
    editor.autosaveNow();
    killed = snapshotOf(root, "recovery_choices_killed");
  }
  const auto start = [&](const std::filesystem::path &from) { return snapshotOf(from, "recovery_choices_copy"); };

  SECTION("discard deletes the files and starts autosave") {
    const std::filesystem::path copy = start(killed);
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    editor.setRecoveryDirectory(copy);
    REQUIRE(editor.recoveryPromptOpen());
    editor.discardRecovered();
    REQUIRE(!editor.recoveryPromptOpen());
    REQUIRE(recoveryFiles(editor.recovery()->directory()) == 0);
    REQUIRE(!editor.isDirty());
    REQUIRE(editor.recovery()->autosaveAllowed());
  }
  SECTION("opening without restoring keeps the files, and the next start offers them again") {
    const std::filesystem::path copy = start(killed);
    {
      editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
      REQUIRE(editor.openProject(directory).has_value());
      editor.setRecoveryDirectory(copy);
      editor.keepRecovered();
      REQUIRE(!editor.isDirty());
      REQUIRE(recoveryFiles(editor.recovery()->directory()) == 1);
    }
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    editor.setRecoveryDirectory(copy);
    REQUIRE(editor.recoveryPromptOpen());
  }
  SECTION("a restore that crashes is quarantined, and the next start is clean") {
    std::filesystem::path crashed;
    {
      const std::filesystem::path copy = start(killed);
      editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
      REQUIRE(editor.openProject(directory).has_value());
      editor.setRecoveryDirectory(copy);
      editor.restoreRecovered();
      REQUIRE(editor.isDirty());
      fixture.frame(editor);
      crashed = snapshotOf(copy, "recovery_choices_crashed"); // killed during the first seconds
    }
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    const std::size_t startRoots = editor.world().roots().size();
    editor.setRecoveryDirectory(crashed);
    REQUIRE(!editor.recoveryPromptOpen());
    REQUIRE(!editor.isDirty());
    REQUIRE(editor.world().roots().size() == startRoots);
    REQUIRE(recoveryFiles(editor.recovery()->directory()) == 0);
    REQUIRE(std::filesystem::exists(editor::Recovery::quarantineRoot(editor.recovery()->directory())));
  }
  SECTION("a scene that cannot be loaded is quarantined and the others are restored") {
    const std::filesystem::path copy = start(killed);
    const std::filesystem::path key = std::filesystem::directory_iterator(copy)->path();
    {
      // Parses as a recovery file, but not as a scene.
      std::ofstream{key / "aaa-1.recovery.scene.json"}
          << R"({"version": 9999, "entities": [], "recovery": {"version": 1, "path": ""}})";
    }
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    editor.setRecoveryDirectory(copy);
    REQUIRE(editor.recoverableScenes().size() == 2);
    editor.restoreRecovered();
    REQUIRE(editor.tabCount() == 1);
    REQUIRE(editor.isDirty()); // the good one came back
    REQUIRE(!std::filesystem::exists(key / "aaa-1.recovery.scene.json"));
    REQUIRE(std::filesystem::exists(editor::Recovery::quarantineRoot(key)));
  }
}

TEST_CASE("save as prefab writes a prefab file the reopened project places again", "[editor][gpu][prefab]") {
  Fixture fixture;
  const std::filesystem::path directory = scratch("save-as-prefab");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "Test").has_value());
    world::World &world = editor.world();
    core::Uuid box;
    for (const flecs::entity root : world.roots()) {
      if (root.get<world::Name>().value == "Box") {
        box = world.uuidOf(root);
      }
    }
    REQUIRE(!box.isNil());
    world.createEntity("Lid", world.find(box));
    const auto file = editor.saveAsPrefab(box);
    REQUIRE(file.has_value());
    REQUIRE(*file == directory / "prefabs" / "Box.prefab.json");
    REQUIRE(std::filesystem::exists(*file));
    REQUIRE(world.isInstance(world.find(box)));
    // The prefab knows its file, which is what lists it in the asset browser.
    REQUIRE(world.prefabOf(world.find(box)).get<world::PrefabFile>().path == *file);
    REQUIRE(editor.saveAsPrefab(box).value() == directory / "prefabs" / "Box 2.prefab.json");
    REQUIRE(editor.commands().undo(world)); // the second replacement
    fixture.frame(editor);
    REQUIRE(editor.saveScene().has_value());

    REQUIRE(editor.openProject(directory).has_value());
    const flecs::entity reopened = world.find(box);
    REQUIRE(world.isInstance(reopened));
    REQUIRE(world.children(reopened).size() == 1);
    REQUIRE(world.children(reopened)[0].get<world::Name>().value == "Lid");
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("the editor saves and restores its floor grid toggle", "[editor][gpu][grid]") {
  Fixture fixture;
  const auto file = scratch("grid-preferences.json");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setPreferencesFile(file);
    REQUIRE(editor.floorGrid());
    editor.setFloorGrid(false);
  }
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    editor.setPreferencesFile(file);
    REQUIRE_FALSE(editor.floorGrid());
    editor.setFloorGrid(true);
  }
  REQUIRE(editor::Preferences::load(file).floorGrid);
  std::filesystem::remove(file);
}

TEST_CASE("a capture run uses neither the saved layout nor the saved preferences", "[editor][capture][files]") {
  const std::filesystem::path directory{"/home/user/.local/share/sonnet/editor"};
  SECTION("an interactive desktop editor keeps both") {
    const editor::UserFiles files = editor::userFiles(directory, false, false);
    REQUIRE(files.preferences == directory / "preferences.json");
    REQUIRE(files.layout == directory / "layout.ini");
  }
  SECTION("a headless editor keeps the preferences and no layout") {
    const editor::UserFiles files = editor::userFiles(directory, true, false);
    REQUIRE(files.preferences == directory / "preferences.json");
    REQUIRE(files.layout.empty());
  }
  SECTION("a capture run keeps neither, on a desktop or headless") {
    for (const bool headless : {false, true}) {
      const editor::UserFiles files = editor::userFiles(directory, headless, true);
      REQUIRE(files.preferences.empty());
      REQUIRE(files.layout.empty());
    }
  }
}

#if defined(__linux__)
// SDL puts the per-user files under XDG_DATA_HOME, so a private one keeps the real files out of reach.
TEST_CASE("a capture run leaves preferences.json as it found it", "[editor][gpu][capture][files]") {
  const std::filesystem::path data = scratch("capture-user-files");
  std::filesystem::create_directories(data);
  const char *previous = std::getenv("XDG_DATA_HOME");
  const std::string restore = previous != nullptr ? previous : "";
  const auto setDataHome = [](const std::string &value) {
    setenv("XDG_DATA_HOME", value.c_str(), 1);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", value.c_str(), true);
  };
  setDataHome(data.string());
  struct Restore {
    std::function<void()> undo;
    ~Restore() {
      undo();
    }
  } restoreEnvironment{[&] {
    if (previous != nullptr) {
      setDataHome(restore);
    } else {
      unsetenv("XDG_DATA_HOME");
      SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME");
    }
  }};

  Fixture fixture;
  const std::filesystem::path file = fixture.platform.prefPath("sonnet", "editor") / "preferences.json";
  REQUIRE(file.string().starts_with(data.string()));
  const auto read = [&file] {
    const auto bytes = core::readFile(file);
    REQUIRE(bytes.has_value());
    return std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
  };
  {
    std::ofstream out{file, std::ios::binary};
    out << R"({"floorGrid": false, "smoothViewDuration": 1.5})";
  }
  const std::string before = read();
  const auto modified = std::filesystem::last_write_time(file);

  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain, false, nullptr, true};
    // The default (on), not what the file holds (off).
    REQUIRE(editor.floorGrid());
    // Everything that saves the preferences: a project becoming the most recent, a toggle.
    REQUIRE(editor.createProject(data / "project", "Capture files").has_value());
    editor.setFloorGrid(false);
    editor.setFloorGrid(true);
    fixture.frame(editor);
  }
  REQUIRE(read() == before);
  REQUIRE(std::filesystem::last_write_time(file) == modified);

  // The same actions in an interactive editor do rewrite it, which is what the test above guards.
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE_FALSE(editor.floorGrid()); // read from the file
    editor.setFloorGrid(true);
  }
  REQUIRE(read() != before);
}
#endif

TEST_CASE("capture the orthographic showcase for visual review", "[.orthographic-showcase][editor][gpu]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/showcase/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest.has_value());
  const auto directory = scratch("orthographic-showcase");
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    for (int frame = 0; frame < 240; ++frame) {
      fixture.frame(editor);
      if (frame > 3 && !editor.assets().loading()) {
        break;
      }
    }
    REQUIRE_FALSE(editor.assets().loading());
    editor.viewport().camera().lookAt({20.0f, 18.0f, 24.0f}, {0.0f, 2.0f, -3.0f});
    auto view = editor.viewport().view();
    view.pivot = {0.0f, 2.0f, -3.0f};
    view.hasPivot = true;
    editor.viewport().setView(view);
    editor.viewport().toggleProjection();
    const std::filesystem::path output = std::filesystem::temp_directory_path() / "sonnet-orthographic-captures";
    std::filesystem::create_directories(output);
    const auto capture = [&](const char *name) {
      editor.requestScreenshots(output / (std::string{name} + ".png"), {});
      fixture.frame(editor);
      const auto result = editor.takeScreenshotResult();
      REQUIRE(result.has_value());
      REQUIRE(result->has_value());
    };
    for (int frame = 0; frame < 20; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE(editor.renderer().statistics().temporalHistoryUsed);
    capture("user-final-taa");
    editor.setShadingTerm(renderer::DebugView::ShadowFactor);
    fixture.frame(editor);
    capture("user-shadow-factor");
    editor.setShadingTerm(renderer::DebugView::Final);
    editor.viewport().snapView(editor::AxisView::Top);
    for (int frame = 0; frame < 20; ++frame) {
      fixture.frame(editor);
    }
    capture("top-final-taa");
    editor.viewport().snapView(editor::AxisView::Front);
    for (int frame = 0; frame < 20; ++frame) {
      fixture.frame(editor);
    }
    capture("front-final-taa");
    editor.setCaptureGrid(true);
    for (const auto axis : {editor::AxisView::Top, editor::AxisView::Bottom, editor::AxisView::Front,
                            editor::AxisView::Back, editor::AxisView::Right, editor::AxisView::Left}) {
      editor.viewport().snapView(axis);
      editor.requestScreenshots(output / (std::string{editor.viewport().camera().viewName()} + "-grid.png"), {});
      fixture.frame(editor);
      const auto result = editor.takeScreenshotResult();
      REQUIRE(result.has_value());
      REQUIRE(result->has_value());
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("delete reference counts include inherited assets in every open scene", "[editor][assets][gpu]") {
  Fixture fixture;
  const auto directory = scratch("asset_references");
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.createProject(directory, "References"));
    auto &world = editor.world();
    world.clearScene();
    const auto material = editor.assets().createMaterial(directory / "assets/paint.material.json", {});
    REQUIRE(material);
    auto root = world.createEntity("Source");
    root.set<world::MeshRenderer>({.mesh = {}, .material = *material});
    const auto file = directory / "prefabs/Source.prefab.json";
    std::filesystem::create_directories(file.parent_path());
    REQUIRE(world::savePrefabFile(world, root, file));
    world.destroyEntity(root);
    const auto prefab = world::loadPrefabFile(world, file);
    REQUIRE(prefab);
    const auto prefabId = world.uuidOf(*prefab);
    world.instantiate(*prefab, "First");
    CHECK(editor.assetReferences(std::array{*material}).entities == 1);
    CHECK(editor.assetReferences(std::array{prefabId}).instances == 1);
    REQUIRE(editor.saveSceneAs(directory / "scenes/first.scene.json"));
    editor.newScene();
    world.instantiate(*prefab, "Second");
    CHECK(editor.assetReferences(std::array{*material}).entities == 2);
    CHECK(editor.assetReferences(std::array{prefabId}).instances == 2);
    // Selecting multiple sources counts each entity once, even when it references several of them.
    const auto mesh = editor.assets().assets(assets::AssetType::Mesh).front()->uuid;
    auto combined = world.createEntity("Both");
    combined.set<world::MeshRenderer>({.mesh = mesh, .material = *material});
    CHECK(editor.assetReferences(std::array{*material, mesh}).entities == 3);
    CHECK(editor.assetReferences(std::array{core::Uuid{}}).entities == 0);
    CHECK(editor.assetReferences(std::array{*material, mesh, prefabId}).instances == 2);
    world.destroyEntity(combined);
    // A name containing the UUID is text, not a reflected Uuid reference.
    world.createEntity(material->toString());
    CHECK(editor.assetReferences(std::array{*material}).entities == 2);
    const auto texture = core::Uuid::generate();
    assets::MaterialSource textured;
    textured.baseColorTexture = texture;
    textured.normalTexture = texture;
    const auto texturedId = editor.assets().createMaterial(directory / "assets/textured.material.json", textured);
    REQUIRE(texturedId);
    CHECK(editor.assetReferences(std::array{texture}).materials == 1);
    CHECK(editor.assetReferences(std::array{texture}).entities == 0);
    // Crash leftovers cannot appear in prefab discovery on reopening.
    std::filesystem::create_directories(directory / ".sonnet/trash/leftover");
    REQUIRE(core::writeFile(directory / ".sonnet/trash/leftover/old.prefab.json", "{}"));
    REQUIRE(editor.openProject(directory));
    CHECK_FALSE(std::filesystem::exists(directory / ".sonnet/trash"));
  }
  fixture.device->waitIdle();
  CHECK(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("viewport axis widget snaps, toggles projection and consumes drags", "[editor][gpu][axis-widget]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    for (int frame = 0; frame < 4; ++frame) {
      fixture.frame(editor);
    }
    auto &viewport = editor.viewport();
    ImGuiIO &io = ImGui::GetIO();
    const auto center = [&] {
      const auto &input = viewport.input();
      const float extent = ImGui::GetFontSize() * 3.25f;
      return glm::vec2{input.origin.x + input.size.x - extent, input.origin.y + extent};
    };
    const auto click = [&](glm::vec2 point) {
      io.AddMousePosEvent(point.x, point.y);
      fixture.frame(editor);
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
      fixture.frame(editor);
      REQUIRE_FALSE(viewport.input().leftClicked);
      REQUIRE_FALSE(viewport.input().leftDown);
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      fixture.frame(editor);
      for (int frame = 0; frame < 3; ++frame) {
        fixture.frame(editor);
      }
    };
    const auto box = editor.world().roots().front();
    editor.selection().select(editor.world().uuidOf(box));
    const auto selected = editor.selection().primary();
    for (const auto axis : {editor::AxisView::Right, editor::AxisView::Left, editor::AxisView::Top,
                            editor::AxisView::Bottom, editor::AxisView::Front, editor::AxisView::Back}) {
      CAPTURE(static_cast<int>(axis));
      viewport.camera().lookAt({5, 3.5f, 7}, {0, 0.5f, 0});
      fixture.frame(editor);
      const glm::vec3 direction = axis == editor::AxisView::Right    ? glm::vec3{1, 0, 0}
                                  : axis == editor::AxisView::Left   ? glm::vec3{-1, 0, 0}
                                  : axis == editor::AxisView::Top    ? glm::vec3{0, 1, 0}
                                  : axis == editor::AxisView::Bottom ? glm::vec3{0, -1, 0}
                                  : axis == editor::AxisView::Front  ? glm::vec3{0, 0, 1}
                                                                     : glm::vec3{0, 0, -1};
      const auto &camera = viewport.camera().camera();
      const glm::vec2 offset{glm::dot(direction, camera.right()), -glm::dot(direction, camera.up())};
      click(center() + offset * ImGui::GetFontSize() * 2.1f);
      REQUIRE(viewport.camera().axisView() == axis);
      REQUIRE(camera.projectionKind == renderer::ProjectionKind::Orthographic);
      REQUIRE(editor.selection().primary() == selected);
      // The near end projects onto the centre in an axis view and wins over the far end.
      click(center());
      const auto opposite = viewport.camera().axisView();
      REQUIRE(opposite != axis);
      REQUIRE(opposite != editor::AxisView::User);
      click(center());
      REQUIRE(viewport.camera().axisView() == axis);
    }
    click(center() - glm::vec2{ImGui::GetFontSize() * 4.25f, 0});
    REQUIRE(viewport.camera().camera().projectionKind == renderer::ProjectionKind::Perspective);
    click(center() - glm::vec2{ImGui::GetFontSize() * 4.25f, 0});
    REQUIRE(viewport.camera().camera().projectionKind == renderer::ProjectionKind::Orthographic);

    viewport.focus({0, 0, 0}, 1.0f);
    const glm::vec3 before = viewport.camera().camera().position;
    const float distance = glm::length(before);
    const glm::vec2 start = center();
    io.AddMousePosEvent(start.x, start.y);
    fixture.frame(editor);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    fixture.frame(editor);
    io.AddMousePosEvent(start.x - 40, start.y + 20);
    fixture.frame(editor);
    REQUIRE(glm::distance(before, viewport.camera().camera().position) > 0.1f);
    REQUIRE(glm::length(viewport.camera().camera().position) == Approx(distance).margin(0.01));
    REQUIRE_FALSE(viewport.input().leftDown);
    io.AddMousePosEvent(start.x - 200, start.y + 100);
    fixture.frame(editor);
    REQUIRE_FALSE(viewport.input().leftDown);
    REQUIRE(glm::length(viewport.camera().camera().position) == Approx(distance).margin(0.01));
    REQUIRE_FALSE(viewport.cameraActive()); // absolute pointer orbit needs no SDL relative mode
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    fixture.frame(editor);
    REQUIRE(viewport.camera().axisView() == editor::AxisView::User);
    REQUIRE(editor.selection().primary() == selected);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("capture the viewport axis widget", "[.axis-widget-captures][editor][gpu]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest.has_value());
  const auto directory = scratch("axis-widget-basic");
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory).has_value());
    for (int frame = 0; frame < 240; ++frame) {
      fixture.frame(editor);
      if (frame > 3 && !editor.assets().loading()) {
        break;
      }
    }
    REQUIRE_FALSE(editor.assets().loading());
    ImGui::LoadIniSettingsFromMemory("[SonnetView][View]\nOverlay=0\n");
    const auto output = std::filesystem::temp_directory_path() / "sonnet-axis-widget-captures";
    std::filesystem::create_directories(output);
    for (const float scale : {1.0f, 2.0f}) {
      ImGui::GetStyle().FontScaleMain = scale;
      for (const bool axis : {false, true}) {
        if (axis) {
          editor.viewport().snapView(editor::AxisView::Front);
        } else {
          editor.viewport().camera() = editor::FlyCamera{};
        }
        for (int frame = 0; frame < 10; ++frame) {
          fixture.frame(editor);
        }
        const std::string name = std::string{axis ? "front" : "perspective"} + (scale == 1.0f ? "-100" : "-200");
        editor.requestScreenshots({}, output / (name + ".png"));
        fixture.frame(editor);
        const auto result = editor.takeScreenshotResult();
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
      }
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}

TEST_CASE("viewport smooth views are immediate headless and cancellable with input", "[editor][gpu][smooth-view]") {
  Fixture fixture;
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    auto &viewport = editor.viewport();
    viewport.snapView(editor::AxisView::Front);
    REQUIRE_FALSE(viewport.camera().transitioning());
    viewport.setImmediateViews(false); // Explicit opt-in to exercise the interactive path.
    viewport.setTransitionDuration(0.2f);
    viewport.snapView(editor::AxisView::Right);
    REQUIRE(viewport.camera().transitioning());
    for (int frame = 0; frame < 15; ++frame) {
      fixture.frame(editor);
    }
    REQUIRE_FALSE(viewport.camera().transitioning());
    REQUIRE(viewport.camera().axisView() == editor::AxisView::Right);
    viewport.snapView(editor::AxisView::Top);
    fixture.frame(editor);
    auto &io = ImGui::GetIO();
    const auto &input = viewport.input();
    io.AddMousePosEvent(input.origin.x + input.size.x * 0.5f, input.origin.y + input.size.y * 0.5f);
    fixture.frame(editor);
    io.AddMouseWheelEvent(0, 1);
    fixture.frame(editor);
    REQUIRE_FALSE(viewport.camera().transitioning());
    const auto stopped = viewport.camera().camera().position;
    fixture.frame(editor);
    REQUIRE(glm::distance(viewport.camera().camera().position, stopped) < 1e-4f);
    viewport.setTransitionDuration(0.0f);
    viewport.snapView(editor::AxisView::Bottom);
    REQUIRE_FALSE(viewport.camera().transitioning());
    REQUIRE(viewport.camera().axisView() == editor::AxisView::Bottom);
    viewport.focus({0, 0, 0}, 1.0f);
    viewport.setTransitionDuration(0.2f);
    viewport.focus({4, 2, -1}, 0.6f);
    for (int frame = 0; frame < 4; ++frame) {
      fixture.frame(editor);
    }
    auto expected = viewport.camera();
    expected.advanceTransition(1.0f / 60.0f);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    fixture.frame(editor);
    REQUIRE_FALSE(viewport.camera().transitioning());
    REQUIRE(glm::distance(viewport.view().pivot, expected.transitionPivot()) < 1e-4f);
    REQUIRE(glm::distance(viewport.camera().camera().position, expected.camera().position) < 1e-4f);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    fixture.frame(editor);
    const auto selected = editor.world().createEntity("Frame me");
    selected.set(world::Transform{.position = {5, 0, 0}});
    editor.selection().select(editor.world().uuidOf(selected));
    io.AddMousePosEvent(input.origin.x + input.size.x * 0.5f, input.origin.y + input.size.y * 0.5f);
    fixture.frame(editor);
    REQUIRE(viewport.input().hovered);
    REQUIRE_FALSE(viewport.cameraActive());
    io.AddKeyEvent(ImGuiKey_F, true);
    fixture.frame(editor);
    REQUIRE(viewport.camera().transitioning()); // F must not cancel the transition it just started.
    io.AddKeyEvent(ImGuiKey_F, false);
    fixture.frame(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
}

TEST_CASE("record smooth viewport transitions", "[editor][gpu][.smooth-view-captures]") {
  Fixture fixture;
  const auto directory = scratch("smooth-view-project");
  const auto output = std::filesystem::temp_directory_path() / "sonnet-smooth-view-captures";
  std::filesystem::create_directories(output);
  REQUIRE(editor::createStarterProject(directory, "Smooth views"));
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory));
    editor.setAntiAliasing(renderer::AntiAliasing::None);
    for (int frame = 0; frame < 15; ++frame) {
      fixture.frame(editor);
    }
    auto &viewport = editor.viewport();
    viewport.focus({0, 0.5f, 0}, 1.5f);
    viewport.setImmediateViews(false);
    viewport.setTransitionDuration(0.2f);
    int index = 0;
    const auto record = [&](int frames) {
      for (int frame = 0; frame < frames; ++frame) {
        const auto name = std::format("frame-{:03}.png", index++);
        editor.requestScreenshots(output / name, {});
        fixture.frame(editor);
        const auto result = editor.takeScreenshotResult();
        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
      }
    };
    record(12);
    viewport.snapView(editor::AxisView::Right);
    record(24);
    viewport.snapView(editor::AxisView::Top);
    record(24);
    viewport.toggleProjection();
    record(24);
    viewport.toggleProjection();
    record(24);
    viewport.focus({0, 0.5f, 0}, 0.6f);
    record(24);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  std::filesystem::remove_all(directory);
}
