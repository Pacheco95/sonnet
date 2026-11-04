// Dropping assets from the Assets panel on the viewport, end to end (docs/editor.md, "Dropping assets on the
// viewport"): a drag from a grid tile, held over the viewport image beside the Game view, then let go.

#include <sonnet/editor/Editor.h>
#include <sonnet/editor/Gizmo.h>
#include <sonnet/editor/Preferences.h>

#include <sonnet/assets/Asset.h>
#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/scripting/Components.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/DrawList.h>

#include <TestSupport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
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
      window = platform.createWindow({.title = "editor_tests", .size = {1280, 720}});
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

  void frames(editor::Editor &editor, int count) {
    for (int i = 0; i < count; ++i) {
      frame(editor);
    }
  }

  // Frames until the database has nothing left to import, a few at least.
  void settle(editor::Editor &editor) {
    for (int i = 0; i < 240; ++i) {
      frame(editor);
      if (i > 3 && !editor.assets().loading()) {
        break;
      }
    }
  }

  // Shows the Game panel docked to the right of the Viewport, as a user splits them.
  void showGame(editor::Editor &editor) {
    editor.setShowGame(true);
    frames(editor, 3);
    const ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");
    REQUIRE(viewport != nullptr);
    ImGuiID centre = viewport->DockId;
    const ImGuiID root = ImGui::DockNodeGetRootNode(ImGui::DockBuilderGetNode(centre))->ID;
    const ImGuiID side = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.5f, nullptr, &centre);
    ImGui::DockBuilderDockWindow("Game", side);
    ImGui::DockBuilderFinish(root);
    frames(editor, 3);
  }

  // Presses on the tile, moves past Dear ImGui's drag threshold inside it so the Assets panel starts the
  // drag, then holds the asset over `to` for two frames. With `asset`, the viewport must report it held.
  void hold(editor::Editor &editor, const editor::ScreenRect &tile, glm::vec2 to, const core::Uuid &asset = {}) {
    ImGuiIO &io = ImGui::GetIO();
    const glm::vec2 from = (tile.min + tile.max) * 0.5f;
    io.AddMousePosEvent(from.x, from.y);
    frame(editor);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame(editor);
    io.AddMousePosEvent(from.x + 12.0f, from.y);
    frame(editor);
    io.AddMousePosEvent((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
    frame(editor);
    io.AddMousePosEvent(to.x, to.y);
    frames(editor, 2);
    if (!asset.isNil()) {
      REQUIRE(editor.viewport().input().draggedAsset == asset);
      REQUIRE_FALSE(editor.viewport().input().assetDropped);
    }
  }

  void release(editor::Editor &editor) {
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frames(editor, 3);
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

std::set<core::Uuid> rootIds(world::World &world) {
  std::set<core::Uuid> ids;
  for (const flecs::entity root : world.roots()) {
    ids.insert(world.uuidOf(root));
  }
  return ids;
}

// The one root that is not among `before`.
flecs::entity newRoot(world::World &world, const std::set<core::Uuid> &before) {
  std::vector<flecs::entity> added;
  for (const flecs::entity root : world.roots()) {
    if (!before.contains(world.uuidOf(root))) {
      added.push_back(root);
    }
  }
  REQUIRE(added.size() == 1);
  return added.front();
}

// The grid tile of the current folder's row that `matches`, in rows() order, which is the order the tiles
// are drawn in. It must be on screen to be dragged.
editor::ScreenRect tileOf(editor::Editor &editor, const std::function<bool(const editor::AssetRow &)> &matches) {
  const std::vector<editor::AssetRow> rows = editor.assetBrowser().rows();
  const std::span<const editor::ScreenRect> tiles = editor.assetBrowser().tiles();
  REQUIRE(tiles.size() == rows.size());
  const auto row = std::ranges::find_if(rows, matches);
  REQUIRE(row != rows.end());
  const editor::ScreenRect tile = tiles[static_cast<std::size_t>(row - rows.begin())];
  INFO("tile " << tile.min.x << ", " << tile.min.y << " to " << tile.max.x << ", " << tile.max.y);
  REQUIRE(tile.max.x > tile.min.x);
  REQUIRE(tile.min.y >= 0.0f);
  REQUIRE(tile.max.y <= ImGui::GetIO().DisplaySize.y);
  return tile;
}

editor::ScreenRect tileOf(editor::Editor &editor, const core::Uuid &uuid) {
  return tileOf(editor, [&](const editor::AssetRow &row) { return row.uuid == uuid; });
}

editor::ScreenRect tileOfFile(editor::Editor &editor, std::string_view filename) {
  return tileOf(editor, [&](const editor::AssetRow &row) { return row.file.filename() == filename; });
}

// Where a world point is drawn in the viewport image, in ImGui's screen space; inside the image.
glm::vec2 screenOf(editor::Editor &editor, glm::vec3 point) {
  const editor::ViewportInput &input = editor.viewport().input();
  const renderer::Camera &camera = editor.viewport().camera().renderView().camera;
  const glm::uvec2 size = editor.viewport().target().size();
  const editor::GizmoView view{.view = camera.view(),
                               .projection = camera.projection(static_cast<float>(size.x) / static_cast<float>(size.y)),
                               .cameraPosition = camera.position,
                               .origin = input.origin,
                               .size = input.size};
  const auto projected = editor::Gizmo::project(view, point);
  REQUIRE(projected.has_value());
  INFO("point at " << projected->x << ", " << projected->y);
  REQUIRE(projected->x > input.origin.x);
  REQUIRE(projected->y > input.origin.y);
  REQUIRE(projected->x < input.origin.x + input.size.x);
  REQUIRE(projected->y < input.origin.y + input.size.y);
  // Dear ImGui floors the mouse position each frame, so the cursor can only be on a whole pixel.
  return glm::floor(*projected);
}

// Where the drawn camera's ray through `mouse` meets the ground, y = 0: the point a drop there lands on
// before snapping, since the cursor is on a whole pixel rather than on the point it was aimed at.
glm::vec3 groundUnder(editor::Editor &editor, glm::vec2 mouse) {
  const editor::ViewportInput &input = editor.viewport().input();
  const glm::uvec2 size = editor.viewport().target().size();
  const renderer::Ray ray = editor.viewport().camera().renderView().camera.ray(
      (mouse - input.origin) / input.size, static_cast<float>(size.x) / static_cast<float>(size.y));
  REQUIRE(ray.direction.y < 0.0f);
  return ray.origin - ray.direction * (ray.origin.y / ray.direction.y);
}

// Whether the window with the keyboard focus is the viewport's.
bool viewportFocused() {
  const ImGuiWindow *viewport = ImGui::FindWindowByName("Viewport");
  const ImGuiWindow *focused = ImGui::GetCurrentContext()->NavWindow;
  return viewport != nullptr && focused != nullptr && focused->RootWindow == viewport;
}

// The scratch project every case drags from: the starter scene with the box saved as a prefab, a material, a
// script and the Basic sample's sky, the Assets panel in grid view at its smallest tiles, the Game view
// split beside the viewport, and the editor's camera looking down at the ground from the front.
struct DropProject {
  std::filesystem::path root;
  core::Uuid prefab;
  core::Uuid material;
  core::Uuid script;
  core::Uuid sky;
};

DropProject makeProject(Fixture &fixture, editor::Editor &editor, const std::filesystem::path &directory) {
  REQUIRE(editor.createProject(directory, "Drop").has_value());
  DropProject project;
  project.root = editor.project()->root;
  const auto prefabFile = editor.saveAsPrefab(editor.world().uuidOf(byName(editor.world(), "Box")));
  REQUIRE(prefabFile.has_value());
  assets::MaterialSource painted;
  painted.baseColor = {0.2f, 0.4f, 0.9f, 1.0f};
  const auto material = editor.assets().createMaterial(project.root / "assets" / "painted.material.json", painted);
  REQUIRE(material.has_value());
  project.material = *material;
  const auto script = editor.assets().createScript(project.root / "assets" / "spin.lua", "return {}");
  REQUIRE(script.has_value());
  project.script = *script;
  const auto sky = editor::locateSource("apps/samples/basic/assets/sky.hdr", {}, fixture.platform.basePath());
  REQUIRE(sky);
  std::filesystem::copy_file(*sky, project.root / "assets" / "sky.hdr");
  static_cast<void>(editor.assets().rescan());
  const assets::AssetInfo *skyInfo = editor.assets().findByPath(project.root / "assets" / "sky.hdr");
  REQUIRE(skyInfo != nullptr);
  project.sky = skyInfo->uuid;

  editor.assetBrowser().setView({.grid = true, .tileSize = editor::AssetsView::MinTileSize});
  fixture.showGame(editor);
  editor.viewport().camera().lookAt({0.0f, 7.0f, 9.0f}, {0.0f, 0.0f, 0.0f});
  editor.selection().clear();
  fixture.settle(editor);
  // The prefab is listed under its file's folder.
  bool listed = false;
  editor.assetBrowser().navigate(prefabFile->parent_path());
  fixture.frame(editor);
  for (const editor::AssetRow &row : editor.assetBrowser().rows()) {
    if (row.file.filename() == prefabFile->filename()) {
      project.prefab = row.uuid;
      listed = true;
    }
  }
  REQUIRE(listed);
  return project;
}

} // namespace

TEST_CASE("a prefab dropped on the viewport previews there, snaps with Ctrl and lands as one step",
          "[editor][gpu][drop]") {
  Fixture fixture;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "drop_prefab";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    const DropProject project = makeProject(fixture, editor, directory);
    // ImGui swaps Ctrl and Super on macOS: Cmd is sent as Super and read as KeyCtrl.
    SECTION("Ctrl shortcuts") {
      ImGui::GetIO().ConfigMacOSXBehaviors = false;
    }
    SECTION("Cmd shortcuts") {
      ImGui::GetIO().ConfigMacOSXBehaviors = true;
    }
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiKey snapModifier = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    world::World &world = editor.world();
    const glm::vec2 cursor = screenOf(editor, {2.3f, 0.0f, 1.6f});
    const glm::vec3 ground = groundUnder(editor, cursor);
    REQUIRE(glm::distance(ground, glm::vec3{2.3f, 0.0f, 1.6f}) < 0.1f);
    const float step = editor.gizmo().snap().translate;
    REQUIRE(step == Approx(1.0f));
    const glm::vec3 snapped{2.0f, 0.0f, 2.0f};

    fixture.frame(editor);
    const std::uint32_t sceneDraws = editor.renderer().statistics(0).drawCount;
    const std::uint32_t gameDraws = editor.renderer().statistics(1).drawCount;
    REQUIRE(sceneDraws > 0);
    REQUIRE(gameDraws == sceneDraws);
    const std::set<core::Uuid> roots = rootIds(world);
    const std::size_t history = editor.commands().size();

    for (const bool snap : {false, true}) {
      INFO((snap ? "snapped" : "unsnapped"));
      fixture.hold(editor, tileOf(editor, project.prefab), cursor, project.prefab);
      // Held, the viewport draws the prefab's box once more; the Game view, the world and the history do not
      // change.
      CHECK(editor.renderer().statistics(0).drawCount == sceneDraws + 1);
      CHECK(editor.renderer().statistics(1).drawCount == gameDraws);
      CHECK(rootIds(world) == roots);
      CHECK(editor.commands().size() == history);
      if (snap) {
        // Pressed mid-drag, the modifier switches snapping on at once.
        io.AddKeyEvent(snapModifier, true);
        fixture.frames(editor, 2);
        REQUIRE(io.KeyCtrl);
        CHECK(editor.renderer().statistics(0).drawCount == sceneDraws + 1);
      }
      fixture.release(editor);
      if (snap) {
        io.AddKeyEvent(snapModifier, false);
        fixture.frame(editor);
      }

      // One step, the new instance selected, standing on the ground, and the viewport focused.
      REQUIRE(editor.commands().size() == history + 1);
      CHECK(editor.commands().undoDescription() == "instantiate Box");
      const flecs::entity instance = newRoot(world, roots);
      CHECK(editor.selection().primary() == world.uuidOf(instance));
      CHECK(viewportFocused());
      const glm::vec3 expected = snap ? snapped : ground;
      const glm::vec3 position = instance.get<world::Transform>().position;
      CHECK(position.x == Approx(expected.x).margin(0.005f));
      CHECK(position.y == Approx(0.5f).margin(0.005f));
      CHECK(position.z == Approx(expected.z).margin(0.005f));
      fixture.frame(editor);
      CHECK(editor.renderer().statistics(0).drawCount == sceneDraws + 1);

      // Undo removes it.
      REQUIRE(editor.commands().undo(world));
      fixture.frames(editor, 2);
      CHECK(rootIds(world) == roots);
      CHECK(editor.commands().size() == history);
      CHECK(editor.renderer().statistics(0).drawCount == sceneDraws);
    }
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  sonnet::test::removeScratch(directory);
}

TEST_CASE("a built-in mesh dropped on the viewport is created standing on the ground", "[editor][gpu][drop]") {
  Fixture fixture;
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "drop_mesh";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    const DropProject project = makeProject(fixture, editor, directory);
    world::World &world = editor.world();
    // Built-in assets are listed at the project root.
    editor.assetBrowser().navigate(project.root);
    fixture.frames(editor, 2);
    const glm::vec2 cursor = screenOf(editor, {-2.2f, 0.0f, 1.4f});
    const glm::vec3 ground = groundUnder(editor, cursor);
    const std::set<core::Uuid> roots = rootIds(world);
    const std::size_t history = editor.commands().size();
    const std::uint32_t sceneDraws = editor.renderer().statistics(0).drawCount;

    fixture.hold(editor, tileOf(editor, assets::builtin::box()), cursor, assets::builtin::box());
    CHECK(editor.renderer().statistics(0).drawCount == sceneDraws + 1);
    CHECK(rootIds(world) == roots);
    CHECK(editor.commands().size() == history);
    fixture.release(editor);

    REQUIRE(editor.commands().size() == history + 1);
    CHECK(std::string{editor.commands().undoDescription()}.starts_with("create "));
    const flecs::entity created = newRoot(world, roots);
    REQUIRE(created.has<world::MeshRenderer>());
    CHECK(created.get<world::MeshRenderer>().mesh == assets::builtin::box());
    CHECK(editor.selection().primary() == world.uuidOf(created));
    CHECK(viewportFocused());
    const glm::vec3 position = created.get<world::Transform>().position;
    CHECK(position.x == Approx(ground.x).margin(0.005f));
    CHECK(position.y == Approx(0.5f).margin(0.005f));
    CHECK(position.z == Approx(ground.z).margin(0.005f));
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  sonnet::test::removeScratch(directory);
}

TEST_CASE("a material dropped on the box outlines it while held and is assigned in one step", "[editor][gpu][drop]") {
  Fixture fixture;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "drop_material";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    const DropProject project = makeProject(fixture, editor, directory);
    world::World &world = editor.world();
    editor.assetBrowser().navigate(project.root / "assets");
    fixture.frames(editor, 2);
    const flecs::entity box = byName(world, "Box");
    const core::Uuid before = box.get<world::MeshRenderer>().material;
    REQUIRE(before != project.material);
    REQUIRE(editor.outlineIds().empty());
    const std::size_t history = editor.commands().size();

    fixture.hold(editor, tileOf(editor, project.material), screenOf(editor, {0.0f, 0.5f, 0.0f}), project.material);
    // Held, the box is the outlined target, and nothing changed yet.
    const std::vector<std::uint32_t> outlined{editor.outlineIds().begin(), editor.outlineIds().end()};
    CHECK(outlined == std::vector<std::uint32_t>{world::World::pickId(box)});
    CHECK(box.get<world::MeshRenderer>().material == before);
    CHECK(editor.commands().size() == history);
    fixture.release(editor);

    REQUIRE(editor.commands().size() == history + 1);
    CHECK(editor.commands().undoDescription() == "set material of Box");
    CHECK(box.get<world::MeshRenderer>().material == project.material);
    CHECK(editor.selection().primary() == world.uuidOf(box));
    CHECK(viewportFocused());
    REQUIRE(editor.commands().undo(world));
    CHECK(box.get<world::MeshRenderer>().material == before);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  sonnet::test::removeScratch(directory);
}

TEST_CASE("a script dropped on the box adds a slot, and an environment becomes the sky", "[editor][gpu][drop]") {
  Fixture fixture;
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "drop_script";
  std::filesystem::remove_all(directory);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    const DropProject project = makeProject(fixture, editor, directory);
    world::World &world = editor.world();
    editor.assetBrowser().navigate(project.root / "assets");
    fixture.frames(editor, 2);
    const flecs::entity box = byName(world, "Box");
    REQUIRE_FALSE(box.has<scripting::Scripts>());
    std::size_t history = editor.commands().size();

    fixture.hold(editor, tileOf(editor, project.script), screenOf(editor, {0.0f, 0.5f, 0.0f}), project.script);
    const std::vector<std::uint32_t> outlined{editor.outlineIds().begin(), editor.outlineIds().end()};
    CHECK(outlined == std::vector<std::uint32_t>{world::World::pickId(box)});
    CHECK_FALSE(box.has<scripting::Scripts>());
    CHECK(editor.commands().size() == history);
    fixture.release(editor);

    REQUIRE(editor.commands().size() == history + 1);
    REQUIRE(box.has<scripting::Scripts>());
    REQUIRE(box.get<scripting::Scripts>().slots.size() == 1);
    CHECK(box.get<scripting::Scripts>().slots[0].script == project.script);
    CHECK(editor.selection().primary() == world.uuidOf(box));
    CHECK(viewportFocused());

    // The starter scene has no sky: the drop makes one, as a drop on the hierarchy does.
    REQUIRE_FALSE(world::environmentEntity(world));
    history = editor.commands().size();
    const editor::ViewportInput &input = editor.viewport().input();
    fixture.hold(editor, tileOf(editor, project.sky), input.origin + input.size * glm::vec2{0.5f, 0.2f}, project.sky);
    CHECK_FALSE(world::environmentEntity(world));
    CHECK(editor.commands().size() == history);
    fixture.release(editor);

    REQUIRE(editor.commands().size() == history + 1);
    const flecs::entity sky = world::environmentEntity(world);
    REQUIRE(sky);
    CHECK(sky.get<world::Environment>().map == project.sky);
    CHECK(viewportFocused());
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  sonnet::test::removeScratch(directory);
}

// docs/editor.md, "Dropping assets on the viewport": window captures, taken mid-drag on a copy of the Basic
// sample, of the crate prefab snapped on the ground and of the ground material held over the sphere.
TEST_CASE("capture drops on the viewport for visual review", "[.drop-captures][editor][gpu]") {
  Fixture fixture;
  const auto manifest = editor::locateSource("apps/samples/basic/project.json", {}, fixture.platform.basePath());
  REQUIRE(manifest);
  const auto directory = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / "drop_captures";
  const auto output = std::filesystem::temp_directory_path() / "sonnet-drop-captures";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory.parent_path()); // copy makes only the last directory
  std::filesystem::create_directories(output);
  std::filesystem::copy(manifest->parent_path(), directory, std::filesystem::copy_options::recursive);
  {
    editor::Editor editor{fixture.platform, *fixture.window, *fixture.device, *fixture.swapchain};
    REQUIRE(editor.openProject(directory));
    REQUIRE(editor.openScene(directory / "scenes/main.scene.json"));
    // Without the statistics overlay, which would cover the left of the viewport.
    ImGui::LoadIniSettingsFromMemory("[SonnetView][View]\nOverlay=0\n");
    editor.assetBrowser().setView({.grid = true, .tileSize = editor::AssetsView::MinTileSize});
    fixture.showGame(editor);
    editor.viewport().camera().lookAt({0.0f, 6.0f, 11.0f}, {0.0f, 0.0f, 2.0f});
    editor.selection().clear();
    fixture.settle(editor);
    ImGuiIO &io = ImGui::GetIO();
    const auto capture = [&](const char *name) {
      editor.requestScreenshots({}, output / name);
      std::optional<core::Result<void>> result;
      for (int i = 0; i < 5 && !result; ++i) {
        fixture.frame(editor);
        result = editor.takeScreenshotResult();
      }
      REQUIRE(result);
      REQUIRE(*result);
    };

    // The crate prefab, snapped on the ground with Ctrl held.
    editor.assetBrowser().navigate(directory / "prefabs");
    fixture.frames(editor, 2);
    fixture.hold(editor, tileOfFile(editor, "crate.prefab.json"), screenOf(editor, {2.3f, 0.0f, 4.6f}));
    io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
    fixture.settle(editor);
    capture("crate-snapped.png");
    fixture.release(editor);
    io.AddKeyEvent(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, false);
    fixture.frame(editor);
    REQUIRE(editor.commands().undoDescription().starts_with("instantiate "));
    // Undone and deselected, so the next picture shows neither the crate nor its gizmo.
    REQUIRE(editor.commands().undo(editor.world()));
    editor.selection().clear();

    // The ground material held over the sphere.
    editor.assetBrowser().navigate(directory / "assets");
    fixture.frames(editor, 2);
    const flecs::entity sphere = byName(editor.world(), "Sphere");
    fixture.hold(editor, tileOfFile(editor, "ground.material.json"),
                 screenOf(editor, glm::vec3{sphere.get<world::WorldTransform>().matrix[3]}));
    fixture.settle(editor);
    capture("ground-material.png");
    fixture.release(editor);
  }
  fixture.device->waitIdle();
  REQUIRE(fixture.device->validationMessageCount() == 0);
  sonnet::test::removeScratch(directory);
}
