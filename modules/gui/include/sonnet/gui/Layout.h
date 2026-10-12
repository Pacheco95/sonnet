#pragma once

#include <sonnet/core/Math.h>

#include <flecs.h>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace sonnet::world {
class World;
}

namespace sonnet::gui {

struct GuiText;

// An axis-aligned rectangle in target pixels, `min` inclusive and `max` exclusive.
struct PixelRect {
  glm::vec2 min{0.0f, 0.0f};
  glm::vec2 max{0.0f, 0.0f};

  [[nodiscard]] glm::vec2 size() const noexcept {
    return max - min;
  }
  [[nodiscard]] bool contains(glm::vec2 point) const noexcept {
    return point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
  }
  friend bool operator==(const PixelRect &, const PixelRect &) = default;
};

// One laid-out element: the canvas entities and the entities with a GuiRect under them.
struct GuiElement {
  flecs::entity_t entity{0};
  PixelRect rect;
  glm::vec2 pivot{0.0f, 0.0f}; // the GuiRect's pivot, in target pixels
  float scale{1.0f};           // reference pixels to target pixels, the canvas's
};

// The size of a text as one unwrapped block, in reference pixels at its `size`; none while its font
// is not loaded. GuiSystem measures with its fonts; a layout without a measurer fits nothing.
using TextMeasure = std::function<std::optional<glm::vec2>(const GuiText &)>;

// Lays out every visible canvas of the world for a target of `targetSize` pixels, whose safe area
// is `safeArea` (the whole target where it has none), sizing the GuiFit elements with `measure`.
// The result is in draw order: canvases by `order` then entity, and under each its tree depth first
// in sibling order, so an element comes after its parent and the elements before it, and what is
// later is in front. An entity with the Disabled tag, its subtree, and a canvas under a disabled
// entity are left out.
[[nodiscard]] std::vector<GuiElement> layoutGui(const world::World &world, glm::vec2 targetSize, PixelRect safeArea,
                                                const TextMeasure &measure = {});

// The element of an entity in a layout, or null where it is hidden or not an element.
[[nodiscard]] const GuiElement *findElement(std::span<const GuiElement> layout, flecs::entity_t entity) noexcept;

} // namespace sonnet::gui
