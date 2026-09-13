#include <sonnet/editor/Editor.h>
#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Event.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/world/Components.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <memory>
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
    // Two frames so the dock layout settles and the viewport reports its rectangle.
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
