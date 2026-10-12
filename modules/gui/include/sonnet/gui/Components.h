#pragma once

#include <sonnet/core/Math.h>
#include <sonnet/core/Uuid.h>

#include <cstdint>
#include <string>

namespace sonnet::world {
class World;
}

namespace sonnet::gui {

// The game UI components (ADR-0026, docs/gui.md): plain structs registered with reflection in the
// world, so the inspector, scene files and scripts reach them like the core components. A UI
// element is an entity under a GuiCanvas; every length is in reference pixels, which the canvas
// scales to the target.

// 32-bit, the width reflection reads enum values with.
enum class GuiAlignment : std::int32_t {
  Start, // left or top
  Center,
  End, // right or bottom
};

enum class GuiAxis : std::int32_t {
  Row,    // children left to right
  Column, // children top to bottom
};

// The root of a UI tree. Its rectangle is the target, or the part of it inside the platform's safe
// area, and its scale comes from the target's size against `referenceSize`.
struct GuiCanvas {
  glm::vec2 referenceSize{1920.0f, 1080.0f};
  // 0 scales by width, 1 by height, between blends the two logarithmically.
  float match{0.0f};
  bool safeArea{true};
  // Canvases draw and take input in this order, then by entity.
  std::int32_t order{0};
};

// An element's place inside its parent's rectangle. The rectangle runs from the parent's point at
// `anchorMin` plus `offsetMin` to its point at `anchorMax` plus `offsetMax`. Under a parent with a
// GuiLayout only the size, offsetMax - offsetMin, is read. `pivot` (0 to 1 over the element) is the
// point that the layout reports as the element's own; it moves nothing yet.
struct GuiRect {
  glm::vec2 anchorMin{0.5f, 0.5f};
  glm::vec2 anchorMax{0.5f, 0.5f};
  glm::vec2 pivot{0.5f, 0.5f};
  glm::vec2 offsetMin{-50.0f, -50.0f};
  glm::vec2 offsetMax{50.0f, 50.0f};
};

// A texture, or a coloured rectangle without one. `slice` is the nine-slice borders (left, top,
// right, bottom) in texture pixels; zero draws the texture whole.
struct GuiImage {
  core::Uuid texture{};
  glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f}; // sRGB, with alpha
  glm::vec4 slice{0.0f, 0.0f, 0.0f, 0.0f};
  bool blocksInput{false}; // takes pointer events even without a button
};

struct GuiText {
  core::Uuid font{};
  std::string text;
  float size{32.0f};
  glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // sRGB, with alpha
  GuiAlignment horizontal{GuiAlignment::Start};
  GuiAlignment vertical{GuiAlignment::Start};
  bool wrap{false};
};

// Makes the element take pointer events. Its tints multiply the `tint` of the GuiImage on the same
// entity while it is normal, under the mouse, pressed or not interactable. It carries no script:
// the Scripts on the same entity hear its onClick, onPress and onRelease.
struct GuiButton {
  bool interactable{true};
  glm::vec4 normal{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 hovered{0.9f, 0.9f, 0.9f, 1.0f};
  glm::vec4 pressed{0.8f, 0.8f, 0.8f, 1.0f};
  glm::vec4 disabled{0.5f, 0.5f, 0.5f, 0.5f};
};

// Sizes and places the children that have a GuiRect, in sibling order, from their sizes. The
// children, as a block, and each child across the axis follow `alignment`. `padding` is left, top,
// right and bottom.
struct GuiLayout {
  GuiAxis axis{GuiAxis::Column};
  float spacing{0.0f};
  glm::vec4 padding{0.0f, 0.0f, 0.0f, 0.0f};
  GuiAlignment alignment{GuiAlignment::Start};
};

// Sizes the element to its text plus `padding` (left, top, right, bottom): the largest of the
// GuiText on the element and on its visible children, each as one unwrapped block. The size takes
// the place of the GuiRect's, around its pivot or in a parent's GuiLayout. Without text, or while a
// font is not loaded, the GuiRect's own size holds.
struct GuiFit {
  glm::vec4 padding{0.0f, 0.0f, 0.0f, 0.0f};
};

// Registers the components above with the world's reflection under their scene-file names. Done
// by GuiSystem; separate for tools that read scenes without running a UI. Idempotent.
void registerComponents(world::World &world);

} // namespace sonnet::gui
