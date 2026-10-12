#include <sonnet/gui/Layout.h>

#include <sonnet/gui/Components.h>
#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <algorithm>
#include <cmath>

namespace sonnet::gui {

namespace {

// The canvas's scale: the target against the reference size, by width at match 0 and by height
// at 1, blended on a logarithmic scale so that halving one is the inverse of doubling it.
[[nodiscard]] float canvasScale(const GuiCanvas &canvas, glm::vec2 target) {
  if (canvas.referenceSize.x <= 0.0f || canvas.referenceSize.y <= 0.0f || target.x <= 0.0f || target.y <= 0.0f) {
    return 1.0f;
  }
  const float match = std::clamp(canvas.match, 0.0f, 1.0f);
  const float byWidth = std::log2(target.x / canvas.referenceSize.x);
  const float byHeight = std::log2(target.y / canvas.referenceSize.y);
  return std::exp2(byWidth + (byHeight - byWidth) * match);
}

// Where `alignment` puts something of `extent` inside [start, start + available].
[[nodiscard]] float align(GuiAlignment alignment, float start, float available, float extent) {
  switch (alignment) {
  case GuiAlignment::Center:
    return start + (available - extent) * 0.5f;
  case GuiAlignment::End:
    return start + available - extent;
  case GuiAlignment::Start:
    break;
  }
  return start;
}

class Layouter {
public:
  Layouter(const world::World &world, std::vector<GuiElement> &out) : m_world(world), m_out(out) {
  }

  void place(flecs::entity entity, PixelRect rect, glm::vec2 pivot, float scale) {
    m_out.push_back({entity.id(), rect, rect.min + rect.size() * pivot, scale});
    std::vector<flecs::entity> children;
    for (const flecs::entity child : m_world.children(entity)) {
      if (!child.has<world::Disabled>() && child.has<GuiRect>()) {
        children.push_back(child);
      }
    }
    const std::vector<PixelRect> rects =
        entity.has<GuiLayout>() ? stack(entity.get<GuiLayout>(), rect, scale, children) : anchor(rect, scale, children);
    for (std::size_t index = 0; index < children.size(); ++index) {
      place(children[index], rects[index], children[index].get<GuiRect>().pivot, scale);
    }
  }

private:
  static std::vector<PixelRect> anchor(PixelRect parent, float scale, const std::vector<flecs::entity> &children) {
    std::vector<PixelRect> rects;
    for (const flecs::entity child : children) {
      const GuiRect &spec = child.get<GuiRect>();
      rects.push_back({parent.min + spec.anchorMin * parent.size() + spec.offsetMin * scale,
                       parent.min + spec.anchorMax * parent.size() + spec.offsetMax * scale});
    }
    return rects;
  }

  // The children in a row or a column inside the padding, each of the size its GuiRect gives.
  static std::vector<PixelRect> stack(const GuiLayout &layout, PixelRect parent, float scale,
                                      const std::vector<flecs::entity> &children) {
    const glm::vec2 start = parent.min + glm::vec2{layout.padding.x, layout.padding.y} * scale;
    const glm::vec2 available = parent.max - glm::vec2{layout.padding.z, layout.padding.w} * scale - start;
    const glm::length_t main = layout.axis == GuiAxis::Row ? 0 : 1;
    const glm::length_t cross = 1 - main;
    const float gap = layout.spacing * scale;
    std::vector<glm::vec2> sizes;
    float total = children.empty() ? 0.0f : gap * static_cast<float>(children.size() - 1);
    for (const flecs::entity child : children) {
      const GuiRect &spec = child.get<GuiRect>();
      sizes.push_back((spec.offsetMax - spec.offsetMin) * scale);
      total += sizes.back()[main];
    }
    float cursor = align(layout.alignment, start[main], available[main], total);
    std::vector<PixelRect> rects;
    for (const glm::vec2 size : sizes) {
      glm::vec2 min;
      min[main] = cursor;
      min[cross] = align(layout.alignment, start[cross], available[cross], size[cross]);
      rects.push_back({min, min + size});
      cursor += size[main] + gap;
    }
    return rects;
  }

  const world::World &m_world;
  std::vector<GuiElement> &m_out;
};

[[nodiscard]] bool hiddenByAncestor(const world::World &world, flecs::entity entity) {
  for (flecs::entity at = entity; at; at = world.parentOf(at)) {
    if (at.has<world::Disabled>()) {
      return true;
    }
  }
  return false;
}

} // namespace

std::vector<GuiElement> layoutGui(const world::World &world, glm::vec2 targetSize, PixelRect safeArea) {
  struct Canvas {
    flecs::entity entity;
    std::int32_t order;
  };
  std::vector<Canvas> canvases;
  world.ecs().each([&](flecs::entity entity, const GuiCanvas &canvas) {
    if (!hiddenByAncestor(world, entity)) {
      canvases.push_back({entity, canvas.order});
    }
  });
  std::ranges::sort(canvases, [](const Canvas &a, const Canvas &b) {
    return a.order != b.order ? a.order < b.order : a.entity.id() < b.entity.id();
  });

  const PixelRect full{{0.0f, 0.0f}, targetSize};
  PixelRect inset{glm::max(safeArea.min, full.min), glm::min(safeArea.max, full.max)};
  if (inset.size().x <= 0.0f || inset.size().y <= 0.0f) {
    inset = full;
  }
  std::vector<GuiElement> elements;
  Layouter layouter(world, elements);
  for (const Canvas &canvas : canvases) {
    const GuiCanvas &spec = canvas.entity.get<GuiCanvas>();
    layouter.place(canvas.entity, spec.safeArea ? inset : full, {0.5f, 0.5f}, canvasScale(spec, targetSize));
  }
  return elements;
}

const GuiElement *findElement(std::span<const GuiElement> layout, flecs::entity_t entity) noexcept {
  const auto found = std::ranges::find(layout, entity, &GuiElement::entity);
  return found == layout.end() ? nullptr : &*found;
}

} // namespace sonnet::gui
