#include <sonnet/gui/Components.h>

#include <sonnet/world/World.h>

namespace sonnet::gui {

void registerComponents(world::World &world) {
  if (world.findComponent("GuiCanvas") != nullptr) {
    return;
  }
  world.ecs().component<GuiAlignment>("GuiAlignment");
  world.ecs().component<GuiAxis>("GuiAxis");
  world.registerComponent<GuiCanvas>("GuiCanvas")
      .member<glm::vec2>("referenceSize")
      .member<float>("match")
      .member<bool>("safeArea")
      .member<std::int32_t>("order");
  world.registerComponent<GuiRect>("GuiRect")
      .member<glm::vec2>("anchorMin")
      .member<glm::vec2>("anchorMax")
      .member<glm::vec2>("pivot")
      .member<glm::vec2>("offsetMin")
      .member<glm::vec2>("offsetMax");
  world.registerComponent<GuiImage>("GuiImage")
      .member<core::Uuid>("texture")
      .member<glm::vec4>("tint")
      .member<glm::vec4>("slice")
      .member<bool>("blocksInput");
  world.registerComponent<GuiText>("GuiText")
      .member<core::Uuid>("font")
      .member<std::string>("text")
      .member<float>("size")
      .member<glm::vec4>("color")
      .member<GuiAlignment>("horizontal")
      .member<GuiAlignment>("vertical")
      .member<bool>("wrap");
  world.registerComponent<GuiButton>("GuiButton")
      .member<bool>("interactable")
      .member<glm::vec4>("normal")
      .member<glm::vec4>("hovered")
      .member<glm::vec4>("pressed")
      .member<glm::vec4>("disabled");
  world.registerComponent<GuiLayout>("GuiLayout")
      .member<GuiAxis>("axis")
      .member<float>("spacing")
      .member<glm::vec4>("padding")
      .member<GuiAlignment>("alignment");
}

} // namespace sonnet::gui
