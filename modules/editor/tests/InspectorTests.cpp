#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/InspectorPanel.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet;
using Widget = editor::InspectorPanel::ScalarWidget;

namespace {

struct Scalars {
  double precise{0.0};
  std::int32_t count{0};
  std::uint32_t mask{0};
  std::int64_t big{0};
  std::uint64_t bigger{0};
  std::int16_t small{0};
};

const ecs_member_t &memberOf(const world::World &world, flecs::entity_t type, std::string_view name) {
  const flecs::entity entity{world.ecs(), type};
  const flecs::Struct &layout = entity.get<flecs::Struct>();
  const auto *members = ecs_vec_first_t(&layout.members, ecs_member_t);
  for (std::int32_t i = 0; i < ecs_vec_count(&layout.members); ++i) {
    if (name == members[i].name) {
      return members[i];
    }
  }
  FAIL("no member " << name);
}

// A Dear ImGui context without a renderer, drawing the inspector on an asset and moving the mouse over it.
struct InspectorDriver {
  editor::InspectorPanel &panel;
  core::Uuid asset;

  InspectorDriver(editor::InspectorPanel &inspector, core::Uuid inspected) : panel(inspector), asset(inspected) {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ~InspectorDriver() {
    ImGui::DestroyContext();
  }
  InspectorDriver(const InspectorDriver &) = delete;
  InspectorDriver &operator=(const InspectorDriver &) = delete;

  void frame() const {
    ImGui::NewFrame();
    // Placed and sized, or the first frames auto-fit a tiny window.
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
    ImGui::SetNextWindowSize(ImVec2{500.0f, 600.0f});
    bool open = true;
    panel.draw(open, asset);
    ImGui::Render();
  }
  // Moves the mouse down `window`, 60 px from its left edge, until the item `id` is hovered.
  [[nodiscard]] bool hover(const ImGuiWindow *window, ImGuiID id) const {
    const ImVec2 pos = window->Pos;
    const ImVec2 size = window->Size;
    for (float y = pos.y; y < pos.y + size.y; y += 2.0f) {
      ImGui::GetIO().AddMousePosEvent(pos.x + 60.0f, y);
      frame();
      if (ImGui::GetCurrentContext()->HoveredIdPreviousFrame == id) {
        return true;
      }
    }
    return false;
  }
  void click() const {
    ImGuiIO &io = ImGui::GetIO();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
  }
  [[nodiscard]] static ImGuiWindow *inspector() {
    ImGuiWindow *window = ImGui::FindWindowByName("Inspector");
    REQUIRE(window != nullptr);
    return window;
  }
  // The id of a texture slot's UV node in the material view, and of a widget inside it.
  [[nodiscard]] static ImGuiID uvNode(const char *slot) {
    return ImHashStr("UV", 0, ImHashStr(slot, 0, inspector()->ID));
  }
  [[nodiscard]] static ImGuiID uvWidget(const char *slot, const char *label) {
    return ImHashStr(label, 0, uvNode(slot));
  }
};

} // namespace

TEST_CASE("scalar members get the widget of their reflected kind", "[editor][inspector]") {
  world::World world;
  const flecs::world &ecs = world.ecs();
  const auto widget = [&](flecs::entity_t type, std::string_view member) {
    return editor::InspectorPanel::scalarWidget(ecs, memberOf(world, type, member));
  };
  REQUIRE(widget(ecs.id<world::MeshRenderer>(), "visible") == Widget::Checkbox);
  REQUIRE(widget(ecs.id<world::PointLight>(), "intensity") == Widget::Float);
  REQUIRE(widget(ecs.id<world::Camera>(), "fovY") == Widget::Degrees);
  REQUIRE(widget(ecs.id<world::Camera>(), "nearPlane") == Widget::Float);
  // Not primitives: drawn by the struct, vector and asset branches.
  REQUIRE(widget(ecs.id<world::Transform>(), "position") == Widget::Unsupported);
  REQUIRE(widget(ecs.id<world::MeshRenderer>(), "mesh") == Widget::Unsupported);

  world.ecs()
      .component<Scalars>("Scalars")
      .member<double>("precise")
      .member<std::int32_t>("count")
      .member<std::uint32_t>("mask")
      .member<std::int64_t>("big")
      .member<std::uint64_t>("bigger")
      .member<std::int16_t>("small");
  const flecs::entity_t scalars = ecs.id<Scalars>();
  REQUIRE(widget(scalars, "precise") == Widget::Double);
  REQUIRE(widget(scalars, "count") == Widget::Int);
  REQUIRE(widget(scalars, "mask") == Widget::UInt);
  REQUIRE(widget(scalars, "big") == Widget::Int64);
  REQUIRE(widget(scalars, "bigger") == Widget::UInt64);
  REQUIRE(widget(scalars, "small") == Widget::Unsupported);
}

TEST_CASE("the material view edits a texture slot's UV transform and set, with undo and save",
          "[editor][inspector][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_uv_settings";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  // Not a real image: the import fails and is logged, but the slot still names the texture.
  REQUIRE(core::writeFile(root / "assets" / "noise.png", std::string_view{"not a png"}).has_value());
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  const assets::AssetInfo *noise = assets.findByPath(root / "assets" / "noise.png");
  REQUIRE(noise != nullptr);
  assets::MaterialSource source;
  source.baseColorTexture = noise->uuid;
  const auto material = assets.createMaterial(root / "assets" / "tiles.material.json", source);
  REQUIRE(material.has_value());

  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, *material};
    driver.frame();
    const ImGuiWindow *window = InspectorDriver::inspector();

    // Only a slot with a texture has the row, and it starts closed.
    REQUIRE_FALSE(driver.hover(window, InspectorDriver::uvNode("Normal map")));
    REQUIRE(driver.hover(window, InspectorDriver::uvNode("Base colour map")));
    driver.click();

    // A click on the rotation slider is one command, pushed when the mouse is released.
    REQUIRE(driver.hover(window, InspectorDriver::uvWidget("Base colour map", "Rotation")));
    driver.click();
    REQUIRE(commands.size() == 1);
    const float rotation = assets.materialSource(*material)->baseColorTransform.rotation;
    REQUIRE(rotation != 0.0f);
    REQUIRE(rotation >= -glm::pi<float>());
    REQUIRE(rotation <= glm::pi<float>());

    // The UV set is a combo: picking an entry is one command.
    REQUIRE(driver.hover(window, InspectorDriver::uvWidget("Base colour map", "UV set")));
    driver.click();
    driver.frame(); // the popup opens on the release and measures itself hidden for a frame
    const ImGuiWindow *popup = nullptr;
    for (const ImGuiWindow *candidate : ImGui::GetCurrentContext()->Windows) {
      if (candidate->Active && std::string_view{candidate->Name}.starts_with("##Combo")) {
        popup = candidate;
      }
    }
    REQUIRE(popup != nullptr);
    const int second = 1; // the combo's PushID around each entry
    REQUIRE(driver.hover(popup, ImHashStr("1", 0, ImHashData(&second, sizeof(second), popup->ID))));
    driver.click();
    REQUIRE(commands.size() == 2);
    REQUIRE(assets.materialSource(*material)->baseColorTransform.texCoord == 1);
    REQUIRE(assets.materialSource(*material)->normalTransform == renderer::TextureTransform{});

    REQUIRE(commands.undo(world));
    REQUIRE(commands.undo(world));
    REQUIRE(assets.materialSource(*material)->baseColorTransform == renderer::TextureTransform{});
    REQUIRE(commands.redo(world));
    REQUIRE(commands.redo(world));
    const renderer::TextureTransform edited = assets.materialSource(*material)->baseColorTransform;
    REQUIRE(edited.rotation == Catch::Approx(rotation));
    REQUIRE(edited.texCoord == 1);

    // Save writes the file, and the reopened database reads the same values back.
    REQUIRE(assets.saveMaterial(*material).has_value());
    assets.open(root, roots);
    REQUIRE(assets.materialSource(*material)->baseColorTransform == edited);
  }
  std::filesystem::remove_all(root);
}
