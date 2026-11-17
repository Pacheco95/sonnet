#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/InspectorPanel.h>
#include <sonnet/editor/Selection.h>
#include <sonnet/editor/SlotMaterials.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/World.h>

#include <TestSupport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
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
    // On macOS Dear ImGui swaps Ctrl and Cmd, so the Ctrl+A of retype would not select the text.
    io.ConfigMacOSXBehaviors = false;
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
  // Moves the mouse down `window`, `x` px from its left edge, until the item `id` is hovered.
  [[nodiscard]] bool hover(const ImGuiWindow *window, ImGuiID id, float x = 60.0f) const {
    const ImVec2 pos = window->Pos;
    const ImVec2 size = window->Size;
    // One frame at the top first: the hovered id read below is the previous frame's, which would
    // otherwise still be the item under the mouse's last position.
    ImGui::GetIO().AddMousePosEvent(pos.x + x, pos.y);
    frame();
    for (float y = pos.y; y < pos.y + size.y; y += 2.0f) {
      ImGui::GetIO().AddMousePosEvent(pos.x + x, y);
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
  // Presses where the mouse is, moves it `dx` px to the right over three frames and releases it.
  void drag(float dx) const {
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 start = io.MousePos;
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    for (int step = 1; step <= 3; ++step) {
      io.AddMousePosEvent(start.x + dx * static_cast<float>(step) / 3.0f, start.y);
      frame();
    }
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
  }
  // Selects the text of the active field, types `text` over it and presses Enter.
  void retype(std::string_view text) const {
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    for (const char character : text) {
      io.AddInputCharacter(static_cast<unsigned int>(character));
      frame();
    }
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
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
  // The id of the material view's top-level widget `label`, and of a component of a colour row.
  [[nodiscard]] static ImGuiID widget(const char *label) {
    return ImHashStr(label, 0, inspector()->ID);
  }
  [[nodiscard]] static ImGuiID colourComponent(const char *label, const char *component) {
    return ImHashStr(component, 0, widget(label));
  }
  [[nodiscard]] static ImGuiID uvWidget(const char *slot, const char *label) {
    return ImHashStr(label, 0, uvNode(slot));
  }
  // The id of a tab of a model's import settings, and of a widget inside it.
  [[nodiscard]] static ImGuiID modelTab(const char *tab) {
    return ImHashStr(tab, 0, ImHashStr("##model", 0, inspector()->ID));
  }
  [[nodiscard]] static ImGuiID modelWidget(const char *tab, const char *label) {
    return ImHashStr(label, 0, modelTab(tab));
  }
  // The id of clip `clip`'s header in the Animations tab, and of event `event`'s field `label` in it.
  [[nodiscard]] static ImGuiID clipId(int clip) {
    return ImHashData(&clip, sizeof(clip), modelTab("Animations"));
  }
  [[nodiscard]] static ImGuiID clipHeader(int clip, const char *name) {
    return ImHashStr(name, 0, clipId(clip));
  }
  [[nodiscard]] static ImGuiID clipWidget(int clip, const char *label) {
    return ImHashStr(label, 0, clipId(clip));
  }
  [[nodiscard]] static ImGuiID eventField(int clip, int event, const char *label) {
    return ImHashStr(label, 0, ImHashData(&event, sizeof(event), clipId(clip)));
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
  REQUIRE(widget(ecs.id<world::Camera>(), "exposure") == Widget::Float);
  REQUIRE(widget(ecs.id<world::Camera>(), "bloomStrength") == Widget::Float);
  REQUIRE(widget(ecs.id<world::DirectionalLight>(), "contactShadows") == Widget::Checkbox);
  REQUIRE(widget(ecs.id<world::DirectionalLight>(), "contactShadowLength") == Widget::Float);
  REQUIRE(widget(ecs.id<world::DirectionalLight>(), "contactShadowThickness") == Widget::Float);
  REQUIRE(widget(ecs.id<world::Camera>(), "ambientOcclusion") == Widget::Checkbox);
  REQUIRE(widget(ecs.id<world::Camera>(), "ambientOcclusionRadius") == Widget::Float);
  REQUIRE(widget(ecs.id<world::Camera>(), "ambientOcclusionStrength") == Widget::Float);
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
  assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

TEST_CASE("the material view edits emissive as a colour and a strength, each edit one command",
          "[editor][inspector][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_emissive";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  const auto material = assets.createMaterial(root / "assets" / "lamp.material.json", assets::MaterialSource{});
  REQUIRE(material.has_value());
  const auto emissive = [&] { return assets.materialSource(*material)->emissive; };

  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, *material};
    driver.frame();
    const ImGuiWindow *window = InspectorDriver::inspector();

    // No emission: black at strength 0. Giving it a colour sets the strength to 1, so it shows.
    REQUIRE(driver.hover(window, InspectorDriver::colourComponent("Emissive", "##X")));
    driver.drag(30.0f);
    REQUIRE(commands.size() == 1);
    const float red = emissive().r;
    REQUIRE(red > 0.0f);
    REQUIRE(red < 1.0f); // the colour times 1
    REQUIRE(emissive().g == 0.0f);
    REQUIRE(emissive().b == 0.0f);

    // Seen afresh, [2, 1, 0] is the colour [1, 0.5, 0] at strength 2; the strength scales it.
    assets::MaterialSource lit = *assets.materialSource(*material);
    lit.emissive = {2.0f, 1.0f, 0.0f};
    assets.setMaterialSource(*material, lit);
    REQUIRE(driver.hover(window, InspectorDriver::widget("Strength")));
    driver.drag(20.0f);
    REQUIRE(commands.size() == 2);
    const glm::vec3 scaled = emissive();
    REQUIRE(scaled.r > 2.0f);
    REQUIRE(scaled.g == Catch::Approx(scaled.r / 2.0f));
    REQUIRE(scaled.b == 0.0f);

    // Dimming the colour keeps the strength: the other channels stay where they were.
    REQUIRE(driver.hover(window, InspectorDriver::colourComponent("Emissive", "##X")));
    driver.drag(-30.0f);
    REQUIRE(commands.size() == 3);
    REQUIRE(emissive().r < scaled.r);
    REQUIRE(emissive().r > 0.0f);
    REQUIRE(emissive().g == Catch::Approx(scaled.g));

    REQUIRE(commands.undo(world));
    REQUIRE(emissive() == scaled);
    REQUIRE(commands.undo(world));
    REQUIRE(emissive() == glm::vec3{2.0f, 1.0f, 0.0f});
    REQUIRE(commands.redo(world));
    REQUIRE(emissive() == scaled);
  }
  assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

TEST_CASE("the model view edits the light scale, rewriting the sidecar and the lights, with undo",
          "[editor][inspector][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_light_scale";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  // Blender's 1000 W point light as its exporter writes it.
  const nlohmann::json document{
      {"asset", {{"version", "2.0"}}},
      {"extensionsUsed", {"KHR_lights_punctual"}},
      {"extensions",
       {{"KHR_lights_punctual", {{"lights", nlohmann::json::array({{{"type", "point"}, {"intensity", 683.0}}})}}}}},
      {"scene", 0},
      {"scenes", nlohmann::json::array({{{"nodes", {0}}}})},
      {"nodes", nlohmann::json::array({{{"name", "Bulb"}, {"extensions", {{"KHR_lights_punctual", {{"light", 0}}}}}}})},
  };
  const std::filesystem::path file = root / "assets" / "lamp.gltf";
  REQUIRE(core::writeFile(file, document.dump()).has_value());
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  const assets::AssetInfo *lamp = assets.findByPath(file);
  REQUIRE(lamp != nullptr);
  const core::Uuid uuid = lamp->uuid;
  const auto intensity = [&] { return assets.model(uuid)->nodes[0].light->intensity; };
  const auto sidecarScale = [&] {
    const auto bytes = core::readFile(file.string() + ".meta");
    REQUIRE(bytes.has_value());
    return nlohmann::json::parse(*bytes)["settings"]["lightScale"].get<float>();
  };
  REQUIRE(intensity() == Catch::Approx(1.0f));

  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, uuid};
    driver.frame();
    const ImGuiWindow *window = InspectorDriver::inspector();

    // Typing a number into the field and leaving it is one command.
    REQUIRE(driver.hover(window, InspectorDriver::modelWidget("Model", "Light scale")));
    driver.click();
    driver.retype("0.5");
    REQUIRE(commands.size() == 1);
    REQUIRE(sidecarScale() == Catch::Approx(0.5f));
    REQUIRE(intensity() == Catch::Approx(341.5f));

    // A negative number is refused: nothing is pushed and the sidecar keeps the last value.
    REQUIRE(driver.hover(window, InspectorDriver::modelWidget("Model", "Light scale")));
    driver.click();
    driver.retype("-2");
    REQUIRE(commands.size() == 1);
    REQUIRE(sidecarScale() == Catch::Approx(0.5f));

    // The quick choices set the two known conventions.
    REQUIRE(driver.hover(window, InspectorDriver::modelWidget("Model", "Photometric"), 120.0f));
    driver.click();
    REQUIRE(commands.size() == 2);
    REQUIRE(sidecarScale() == 1.0f);
    REQUIRE(intensity() == Catch::Approx(683.0f));
    REQUIRE(driver.hover(window, InspectorDriver::modelWidget("Model", "Blender")));
    driver.click();
    REQUIRE(commands.size() == 3);
    REQUIRE(intensity() == Catch::Approx(1.0f));

    REQUIRE(commands.undo(world));
    REQUIRE(commands.undo(world));
    REQUIRE(sidecarScale() == Catch::Approx(0.5f));
    REQUIRE(intensity() == Catch::Approx(341.5f));
    REQUIRE(commands.undo(world));
    REQUIRE(sidecarScale() == Catch::Approx(1.0f / 683.0f));
    REQUIRE(intensity() == Catch::Approx(1.0f));
  }
  assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

TEST_CASE("the model view's Animations tab edits a clip's sidecar events, with undo", "[editor][inspector][assets]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_animation_events";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets");
  // One node moved over a second by the clip "Walk", whose extras carry one event.
  const std::vector<float> keys{0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
  REQUIRE(core::writeFile(root / "assets" / "walk.bin",
                          std::span{reinterpret_cast<const std::byte *>(keys.data()), keys.size() * sizeof(float)})
              .has_value());
  using nlohmann::json;
  const json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({{{"nodes", {0}}}})},
      {"nodes", json::array({{{"name", "Body"}}})},
      {"buffers", json::array({{{"uri", "walk.bin"}, {"byteLength", 32}}})},
      {"bufferViews",
       json::array({{{"buffer", 0}, {"byteLength", 8}}, {{"buffer", 0}, {"byteOffset", 8}, {"byteLength", 24}}})},
      {"accessors", json::array({{{"bufferView", 0},
                                  {"componentType", 5126},
                                  {"count", 2},
                                  {"type", "SCALAR"},
                                  {"min", {0.0}},
                                  {"max", {1.0}}},
                                 {{"bufferView", 1}, {"componentType", 5126}, {"count", 2}, {"type", "VEC3"}}})},
      {"animations",
       json::array(
           {{{"name", "Walk"},
             {"samplers", json::array({{{"input", 0}, {"output", 1}}})},
             {"channels", json::array({{{"sampler", 0}, {"target", {{"node", 0}, {"path", "translation"}}}}})},
             {"extras", {{"events", json::array({{{"time", 0.25}, {"name", "step"}, {"argument", "left"}}})}}}}})},
  };
  const std::filesystem::path file = root / "assets" / "walker.gltf";
  REQUIRE(core::writeFile(file, document.dump()).has_value());
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  const assets::AssetInfo *walker = assets.findByPath(file);
  REQUIRE(walker != nullptr);
  const core::Uuid uuid = walker->uuid;
  const core::Uuid clip = core::Uuid::derive(uuid, "animation/0");
  using Events = std::vector<assets::AnimationEvent>;
  const Events own{{.time = 0.25f, .name = "step", .argument = "left"}};
  // The clip's events once loaded, and the sidecar's list for it.
  const auto clipEvents = [&] {
    static_cast<void>(assets.requestAnimation(clip));
    assets.waitForLoads();
    const assets::AnimationClip *loaded = assets.animation(clip);
    REQUIRE(loaded != nullptr);
    return loaded->events;
  };
  const auto sidecarEvents = [&] {
    const auto settings = assets.modelSettings(uuid);
    const auto found = settings.animationEvents.find("Walk");
    return found != settings.animationEvents.end() ? found->second : Events{};
  };
  REQUIRE(clipEvents() == own);

  world::World world;
  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, uuid};
    driver.frame();
    const ImGuiWindow *window = InspectorDriver::inspector();

    // The Model tab is the first; the Animations tab lists the clip under a closed header.
    REQUIRE(driver.hover(window, InspectorDriver::modelWidget("Model", "Light scale")));
    REQUIRE(driver.hover(window, InspectorDriver::modelTab("Animations"), 100.0f));
    driver.click();
    REQUIRE_FALSE(driver.hover(window, InspectorDriver::clipWidget(0, "Add"), 20.0f));
    REQUIRE(driver.hover(window, InspectorDriver::clipHeader(0, "Walk")));
    driver.click();

    // Adding an event is one command, written to the sidecar and merged into the clip.
    REQUIRE(driver.hover(window, InspectorDriver::clipWidget(0, "Add"), 20.0f));
    driver.click();
    REQUIRE(commands.size() == 1);
    REQUIRE(sidecarEvents() == Events{{.time = 0.0f, .name = "event", .argument = {}}});
    REQUIRE(clipEvents() == Events{{.time = 0.0f, .name = "event", .argument = {}}, own[0]});

    // Each field left is one command; with the glTF's time and name the event replaces the glTF's.
    REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 0, "##argument"), 240.0f));
    driver.click();
    driver.retype("right");
    REQUIRE(commands.size() == 2);
    REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 0, "##name"), 120.0f));
    driver.click();
    driver.retype("step");
    REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 0, "##time"), 20.0f));
    driver.click();
    driver.retype("0.25");
    REQUIRE(commands.size() == 4);
    const Events replaced{{.time = 0.25f, .name = "step", .argument = "right"}};
    REQUIRE(sidecarEvents() == replaced);
    REQUIRE(clipEvents() == replaced);

    // A negative time or an empty name would not survive the sidecar, so nothing is pushed.
    REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 0, "##time"), 20.0f));
    driver.click();
    driver.retype("-1");
    REQUIRE(commands.size() == 4);

    // A second event goes after the first, and the list stays sorted when a time moves it ahead.
    REQUIRE(driver.hover(window, InspectorDriver::clipWidget(0, "Add"), 20.0f));
    driver.click();
    REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 1, "##time"), 20.0f));
    driver.click();
    driver.retype("0.1");
    REQUIRE(commands.size() == 6);
    REQUIRE(sidecarEvents() == Events{{.time = 0.1f, .name = "event", .argument = {}}, replaced[0]});

    // Removing both leaves the sidecar without the clip, and the glTF's event comes back.
    for (int i = 0; i < 2; ++i) {
      REQUIRE(driver.hover(window, InspectorDriver::eventField(0, 0, "Remove"), 350.0f));
      driver.click();
    }
    REQUIRE(commands.size() == 8);
    REQUIRE(assets.modelSettings(uuid).animationEvents.empty());
    REQUIRE(clipEvents() == own);

    // Undo and redo restore the list and the clip.
    REQUIRE(commands.undo(world));
    REQUIRE(commands.undo(world));
    REQUIRE(sidecarEvents() == Events{{.time = 0.1f, .name = "event", .argument = {}}, replaced[0]});
    REQUIRE(clipEvents() == Events{{.time = 0.1f, .name = "event", .argument = {}}, replaced[0]});
    REQUIRE(commands.redo(world));
    REQUIRE(sidecarEvents() == replaced);
  }
  assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

TEST_CASE("the inspector draws the members an instance changed without changing anything",
          "[editor][inspector][prefab]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Lamp");
  prefab.set<world::PointLight>({});
  world.createEntity("Bulb", prefab).set<world::MeshRenderer>({});
  world.makePrefab(prefab);
  const flecs::entity lamp = world.instantiate(prefab, "Lamp 1");
  lamp.ensure<world::PointLight>().intensity = 4.0f;
  const flecs::entity bulb = world.children(lamp)[0];
  bulb.ensure<world::MeshRenderer>().color.r = 0.25f;
  const nlohmann::json lampBefore = world.componentToJson(lamp, world.findComponent("PointLight")->id);
  const nlohmann::json bulbBefore = world.componentToJson(bulb, world.findComponent("MeshRenderer")->id);

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  const InspectorDriver driver{panel, {}};
  for (const flecs::entity entity : {lamp, bulb}) {
    selection.select(world.uuidOf(entity));
    for (int frame = 0; frame < 3; ++frame) {
      driver.frame();
    }
  }
  REQUIRE(!commands.canUndo());
  REQUIRE(world.componentToJson(lamp, world.findComponent("PointLight")->id) == lampBefore);
  REQUIRE(world.componentToJson(bulb, world.findComponent("MeshRenderer")->id) == bulbBefore);
  REQUIRE(!world.find(world.uuidOf(lamp)).owns<world::MeshRenderer>());
}

TEST_CASE("editing an inherited component gives the instance its own strings and lists",
          "[editor][inspector][prefab]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Walker");
  prefab.set<world::Animator>({.rootBone = "Root", .layers = {world::AnimationLayer{}}});
  world.makePrefab(prefab);
  const flecs::entity walker = world.instantiate(prefab, "Walker 1");
  REQUIRE_FALSE(walker.owns<world::Animator>());

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, {}};
    selection.select(world.uuidOf(walker));
    driver.frame();
    // Dragging the inherited Animator's speed makes the value the instance's own.
    const flecs::entity animator = world.ecs().component<world::Animator>();
    const ecs_member_t *members = ecs_vec_first_t(&animator.get<flecs::Struct>().members, ecs_member_t);
    const int speedIndex = static_cast<int>(&memberOf(world, animator, "speed") - members);
    // Each member is a row of a property table of its own, which pushes the table's id.
    const ImGuiID row =
        ImHashData(&speedIndex, sizeof(speedIndex), ImHashStr("Animator", 0, InspectorDriver::inspector()->ID));
    const ImGuiID speed = ImHashStr("##value", 0, ImHashStr("member", 0, row));
    REQUIRE(driver.hover(InspectorDriver::inspector(), speed, 300.0f));
    driver.drag(30.0f);
  }
  REQUIRE(walker.owns<world::Animator>());
  REQUIRE(commands.canUndo());
  const world::Animator &own = walker.get<world::Animator>();
  const world::Animator &shared = prefab.get<world::Animator>();
  REQUIRE(own.speed != shared.speed);
  // Its string and list are copies, not the prefab's buffers.
  REQUIRE(own.rootBone == "Root");
  REQUIRE(own.rootBone.data() != shared.rootBone.data());
  REQUIRE(own.layers.size() == 1);
  REQUIRE(own.layers.data() != shared.layers.data());
}

TEST_CASE("a changed member of an instance offers to apply it to the prefab", "[editor][inspector][prefab][apply]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Lamp");
  prefab.set<world::PointLight>({});
  world.makePrefab(prefab);
  const flecs::entity lamp = world.instantiate(prefab, "Lamp 1");
  lamp.ensure<world::PointLight>().intensity = 4.0f;

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  std::vector<editor::ApplyRequest> asked;
  std::vector<editor::ApplyRequest> applied;
  const core::Uuid lampPrefab = world.uuidOf(prefab);
  panel.setApplyHandlers(
      [&](const editor::ApplyRequest &request) {
        asked.push_back(request);
        return std::vector<editor::ApplyChoice>{{.prefab = lampPrefab, .name = "Lamp"}};
      },
      [&](editor::ApplyRequest request) { applied.push_back(std::move(request)); });
  const InspectorDriver driver{panel, {}};
  selection.select(world.uuidOf(lamp));
  driver.frame();
  // Asked for the changed component and for its changed member, never for the others.
  const auto askedFor = [&](std::string_view member) {
    return std::ranges::any_of(asked, [&](const editor::ApplyRequest &request) {
      return request.component == "PointLight" && request.member.to_string() == member;
    });
  };
  CHECK(askedFor(""));
  CHECK(askedFor("/intensity"));
  CHECK(!askedFor("/range"));

  // The member's row has Apply and Revert side by side, each one click away.
  const flecs::entity type = world.ecs().component<world::PointLight>();
  const ecs_member_t *members = ecs_vec_first_t(&type.get<flecs::Struct>().members, ecs_member_t);
  const int index = static_cast<int>(&memberOf(world, type, "intensity") - members);
  const ImGuiID row = ImHashData(&index, sizeof(index), ImHashStr("PointLight", 0, InspectorDriver::inspector()->ID));
  const auto hoverButton = [&](const char *label) {
    const ImGuiID button = ImHashStr(label, 0, ImHashStr("member", 0, row));
    for (float x = 494.0f; x > 300.0f; x -= 6.0f) {
      if (driver.hover(InspectorDriver::inspector(), button, x)) {
        return true;
      }
    }
    return false;
  };
  REQUIRE(hoverButton("Apply"));
  driver.click();
  CHECK(ImGui::GetCurrentContext()->OpenPopupStack.empty());
  REQUIRE(applied.size() == 1);
  CHECK(applied[0].entity == world.uuidOf(lamp));
  CHECK(applied[0].member.to_string() == "/intensity");
  CHECK(applied[0].prefab == lampPrefab);
  CHECK(!commands.canUndo());

  REQUIRE(hoverButton("Revert"));
  driver.click();
  driver.frame();
  CHECK(applied.size() == 1);
  CHECK(commands.canUndo());
  CHECK(lamp.get<world::PointLight>().intensity == prefab.get<world::PointLight>().intensity);
}

TEST_CASE("a change nested prefabs could each take offers them in Apply's menu, the rule's pick first",
          "[editor][inspector][prefab][apply][nested]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Lamp");
  prefab.set<world::PointLight>({});
  world.makePrefab(prefab);
  const flecs::entity lamp = world.instantiate(prefab, "Lamp 1");
  lamp.ensure<world::PointLight>().intensity = 4.0f;

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  // The handler stands in for Editor::applyChoices: an inner and an outer prefab.
  const core::Uuid crate = core::Uuid::generate();
  const core::Uuid pallet = core::Uuid::generate();
  std::vector<editor::ApplyRequest> applied;
  panel.setApplyHandlers(
      [&](const editor::ApplyRequest &) {
        return std::vector<editor::ApplyChoice>{{.prefab = crate, .name = "Crate"},
                                                {.prefab = pallet, .name = "Pallet"}};
      },
      [&](editor::ApplyRequest request) { applied.push_back(std::move(request)); });
  const InspectorDriver driver{panel, {}};
  selection.select(world.uuidOf(lamp));
  driver.frame();

  const flecs::entity type = world.ecs().component<world::PointLight>();
  const ecs_member_t *members = ecs_vec_first_t(&type.get<flecs::Struct>().members, ecs_member_t);
  const int index = static_cast<int>(&memberOf(world, type, "intensity") - members);
  const ImGuiID row = ImHashData(&index, sizeof(index), ImHashStr("PointLight", 0, InspectorDriver::inspector()->ID));
  const ImGuiID cell = ImHashStr("member", 0, row);
  const auto hoverRow = [&](ImGuiID id) {
    for (float x = 494.0f; x > 300.0f; x -= 3.0f) {
      if (driver.hover(InspectorDriver::inspector(), id, x)) {
        return true;
      }
    }
    return false;
  };
  // The button itself applies to the rule's pick, with no menu.
  REQUIRE(hoverRow(ImHashStr("Apply", 0, cell)));
  driver.click();
  CHECK(ImGui::GetCurrentContext()->OpenPopupStack.empty());
  REQUIRE(applied.size() == 1);
  CHECK(applied[0].prefab == crate);
  CHECK(applied[0].member.to_string() == "/intensity");

  // The arrow beside it opens the list, and picking the outer prefab applies there.
  REQUIRE(hoverRow(ImHashStr("menu", 0, ImHashStr("Apply", 0, cell))));
  driver.click();
  CHECK(applied.size() == 1);
  REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
  ImGuiWindow *menu = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
  REQUIRE(menu != nullptr);
  int outer = 1;
  const ImGuiID palletItem = ImHashStr("Pallet", 0, ImHashData(&outer, sizeof(outer), menu->ID));
  bool found = false;
  for (float x = 8.0f; x < menu->Size.x && !found; x += 8.0f) {
    found = driver.hover(menu, palletItem, x);
  }
  REQUIRE(found);
  driver.click();
  REQUIRE(applied.size() == 2);
  CHECK(applied[1].prefab == pallet);
  CHECK(applied[1].member.to_string() == "/intensity");
  CHECK(applied[1].entity == world.uuidOf(lamp));
  CHECK(ImGui::GetCurrentContext()->OpenPopupStack.empty());
  CHECK(!commands.canUndo());
}

TEST_CASE("an instance root offers to apply all of its overrides", "[editor][inspector][prefab][apply]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Lamp");
  prefab.set<world::PointLight>({});
  world.makePrefab(prefab);
  const flecs::entity lamp = world.instantiate(prefab, "Lamp 1");
  lamp.ensure<world::PointLight>().intensity = 4.0f;
  const flecs::entity plain = world.createEntity("Plain");

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  std::vector<core::Uuid> asked;
  std::vector<core::Uuid> applied;
  panel.setApplyAllHandlers(
      [&](core::Uuid instance) {
        asked.push_back(instance);
        return true;
      },
      [&](core::Uuid instance) { applied.push_back(instance); });
  const InspectorDriver driver{panel, {}};
  // Asked about an instance root only.
  selection.select(world.uuidOf(plain));
  driver.frame();
  CHECK(asked.empty());
  selection.select(world.uuidOf(lamp));
  driver.frame();
  REQUIRE(!asked.empty());
  CHECK(asked.back() == world.uuidOf(lamp));
  REQUIRE(driver.hover(InspectorDriver::inspector(),
                       ImHashStr("Apply all overrides", 0, InspectorDriver::inspector()->ID)));
  driver.click();
  REQUIRE(applied.size() == 1);
  CHECK(applied[0] == world.uuidOf(lamp));
  CHECK(!commands.canUndo());
}

TEST_CASE("a slot's material is set by its own value, past slots dropped from the end", "[editor][inspector][slots]") {
  const core::Uuid red = core::Uuid::generate();
  const core::Uuid green = core::Uuid::generate();
  // Nil entries fill the slots before the one set.
  REQUIRE(editor::setSlotMaterial({}, 2, red) == std::vector<core::Uuid>{{}, {}, red});
  REQUIRE(editor::setSlotMaterial({red, green}, 0, green) == std::vector<core::Uuid>{green, green});
  // Clearing the last slot drops the trailing nils, down to the empty list a file leaves out.
  REQUIRE(editor::setSlotMaterial({{}, {}, red}, 2, {}).empty());
  REQUIRE(editor::setSlotMaterial({red, {}, green}, 2, {}) == std::vector<core::Uuid>{red});
}

TEST_CASE("the mesh renderer lists its mesh's slots, with a picker and an editable copy each, with undo",
          "[editor][inspector][slots]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_editor_slot_materials";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "assets" / "figure");
  // One mesh of two primitives, the first with the material "Body", the second with "Skin/Face",
  // whose slash a file name cannot hold.
  constexpr std::string_view model =
      R"json({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"Figure","mesh":0}],)json"
      R"json("meshes":[{"name":"FigureMesh","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0},)json"
      R"json({"attributes":{"POSITION":0},"indices":1,"material":1}]}],)json"
      R"json("materials":[{"name":"Body"},{"name":"Skin/Face","pbrMetallicRoughness":{"roughnessFactor":0.3}}],)json"
      R"json("buffers":[{"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAEAAAACAAAA","byteLength":48}],)json"
      R"json("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12}],)json"
      R"json("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)json"
      R"json({"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}]})json";
  const std::filesystem::path file = root / "assets" / "figure" / "figure.gltf";
  REQUIRE(core::writeFile(file, model).has_value());
  // A copy made earlier holds the plain name, so this one is numbered.
  REQUIRE(core::writeFile(root / "assets" / "figure" / "Skin_Face.material.json", std::string_view{"{}"}).has_value());
  const std::vector<std::string> roots{"assets"};
  assets.open(root, roots);
  core::Uuid mesh;
  for (const assets::AssetInfo *info : assets.assets(assets::AssetType::Mesh)) {
    if (info->name == "FigureMesh") {
      mesh = info->uuid;
    }
  }
  REQUIRE_FALSE(mesh.isNil());
  const core::Uuid skin = assets.find(mesh)->materials[1];
  assets::MaterialSource redSource;
  redSource.baseColor = {1.0f, 0.0f, 0.0f, 1.0f};
  const auto red = assets.createMaterial(root / "assets" / "red.material.json", redSource);
  REQUIRE(red.has_value());

  world::World world;
  const flecs::entity figure = world.createEntity("Figure");
  figure.set<world::MeshRenderer>({.mesh = mesh});
  const core::Uuid figureUuid = world.uuidOf(figure);
  const auto slots = [&] { return world.find(figureUuid).get<world::MeshRenderer>().slotMaterials; };
  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  {
    const InspectorDriver driver{panel, {}};
    selection.select(figureUuid);
    driver.frame();
    ImGuiWindow *window = InspectorDriver::inspector();
    // Each slot is a block in the value cell of the slotMaterials row, inside its property table.
    const flecs::entity type = world.ecs().component<world::MeshRenderer>();
    const ecs_member_t *members = ecs_vec_first_t(&type.get<flecs::Struct>().members, ecs_member_t);
    const int memberIndex = static_cast<int>(&memberOf(world, type, "slotMaterials") - members);
    const ImGuiID row = ImHashData(&memberIndex, sizeof(memberIndex), ImHashStr("MeshRenderer", 0, window->ID));
    const auto slotWidget = [&](int slot, const char *label) {
      return ImHashStr(label, 0, ImHashData(&slot, sizeof(slot), ImHashStr("member", 0, row)));
    };

    // "Make editable copy" on the second slot writes the glTF material beside the model and uses it.
    REQUIRE(driver.hover(window, slotWidget(1, "Make editable copy"), 130.0f));
    driver.click();
    REQUIRE(commands.size() == 1);
    const std::filesystem::path written = root / "assets" / "figure" / "Skin_Face 2.material.json";
    REQUIRE(std::filesystem::is_regular_file(written));
    const assets::AssetInfo *copy = assets.findByPath(written);
    REQUIRE(copy != nullptr);
    const core::Uuid copyUuid = copy->uuid;
    REQUIRE(slots() == std::vector<core::Uuid>{{}, copyUuid});
    REQUIRE(*assets.materialSource(copyUuid) == *assets.materialSource(skin));
    REQUIRE(assets.materialSource(copyUuid)->roughness == Catch::Approx(0.3f));

    // The first slot's picker, then the red material in its list: one more command.
    REQUIRE(driver.hover(window, slotWidget(0, "(none)"), 130.0f));
    driver.click();
    driver.frame(); // the popup measures itself hidden for a frame
    const ImGuiWindow *popup = nullptr;
    for (const ImGuiWindow *candidate : ImGui::GetCurrentContext()->Windows) {
      if (candidate->Active && std::string_view{candidate->Name}.starts_with("##Popup")) {
        popup = candidate;
      }
    }
    REQUIRE(popup != nullptr);
    REQUIRE(driver.hover(popup, ImHashStr("red", 0, ImHashStr(red->toString().c_str(), 0, popup->ID))));
    driver.click();
    REQUIRE(commands.size() == 2);
    REQUIRE(slots() == std::vector<core::Uuid>{*red, copyUuid});

    // Undo takes the pick back, then the copy's assignment; the file stays, as a new material does.
    REQUIRE(commands.undo(world));
    REQUIRE(slots() == std::vector<core::Uuid>{{}, copyUuid});
    REQUIRE(commands.undo(world));
    REQUIRE(slots().empty());
    REQUIRE(std::filesystem::is_regular_file(written));
    REQUIRE(commands.redo(world));
    REQUIRE(commands.redo(world));
    REQUIRE(slots() == std::vector<core::Uuid>{*red, copyUuid});
  }
  assets.close(); // finishes an import on a worker still holding a file open, which Windows will not delete
  sonnet::test::removeScratch(root);
}

TEST_CASE("a renamed part of an instance offers to apply its name beside the name field",
          "[editor][inspector][prefab][apply][name]") {
  platform::Platform platform{{.headless = true}};
  const auto device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 2}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  const flecs::entity prefab = world.createEntity("Lamp");
  world.createEntity("Bulb", prefab);
  world.makePrefab(prefab);
  const flecs::entity lamp = world.instantiate(prefab, "Lamp 1");
  const flecs::entity bulb = world.findByPath(lamp, "Bulb");
  REQUIRE(bulb);
  bulb.set<world::Name>({"Globe"});

  editor::Selection selection;
  editor::CommandStack commands;
  editor::InspectorPanel panel{world, assets, selection, commands};
  // The handler stands in for Editor::applyChoices: a name is offered, a component is not.
  const core::Uuid lampPrefab = world.uuidOf(prefab);
  std::vector<editor::ApplyRequest> asked;
  std::vector<editor::ApplyRequest> applied;
  panel.setApplyHandlers(
      [&](const editor::ApplyRequest &request) {
        asked.push_back(request);
        return request.kind == editor::ApplyKind::Name
                   ? std::vector<editor::ApplyChoice>{{.prefab = lampPrefab, .name = "Lamp"}}
                   : std::vector<editor::ApplyChoice>{};
      },
      [&](editor::ApplyRequest request) { applied.push_back(std::move(request)); });
  const InspectorDriver driver{panel, {}};
  selection.select(world.uuidOf(bulb));
  driver.frame();
  CHECK(std::ranges::any_of(asked, [&](const editor::ApplyRequest &request) {
    return request.kind == editor::ApplyKind::Name && request.entity == world.uuidOf(bulb);
  }));

  // Apply sits at the right end of the name's row and asks for the name, pushing no command.
  const ImGuiID button = ImHashStr("Apply", 0, ImHashStr("name", 0, InspectorDriver::inspector()->ID));
  bool found = false;
  for (float x = 494.0f; x > 300.0f && !found; x -= 6.0f) {
    found = driver.hover(InspectorDriver::inspector(), button, x);
  }
  REQUIRE(found);
  driver.click();
  REQUIRE(applied.size() == 1);
  CHECK(applied[0].kind == editor::ApplyKind::Name);
  CHECK(applied[0].entity == world.uuidOf(bulb));
  CHECK(applied[0].prefab == lampPrefab);
  CHECK(!commands.canUndo());
  CHECK(bulb.get<world::Name>().value == "Globe");
}
