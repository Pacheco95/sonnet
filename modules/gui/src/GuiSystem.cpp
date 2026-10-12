#include <sonnet/gui/GuiSystem.h>

#include <sonnet/world/World.h>

#include <algorithm>
#include <ranges>

namespace sonnet::gui {

namespace {

// Only a finger and the left button click; the others are held, so the game does not see them
// through the UI, and say nothing.
[[nodiscard]] bool clicks(GuiPointer pointer) noexcept {
  return pointer.touch || pointer.id == static_cast<std::uint64_t>(platform::MouseButton::Left);
}

[[nodiscard]] GuiPointer mouse(platform::MouseButton button) noexcept {
  return {false, static_cast<std::uint64_t>(button)};
}

} // namespace

GuiSystem::GuiSystem(world::World &world) : m_world(world) {
  registerComponents(world);
}

void GuiSystem::update(glm::vec2 targetSize, glm::vec2 windowSize, platform::WindowRect safeArea) {
  m_pointerScale = windowSize.x > 0.0f && windowSize.y > 0.0f ? targetSize / windowSize : glm::vec2{1.0f};
  const glm::vec2 min = glm::vec2{safeArea.position} * m_pointerScale;
  m_elements = layoutGui(m_world, targetSize, {min, min + glm::vec2{safeArea.size} * m_pointerScale});
}

bool GuiSystem::pressed(flecs::entity_t entity) const noexcept {
  return std::ranges::any_of(m_captures,
                             [&](const Capture &capture) { return capture.entity == entity && capture.notify; });
}

bool GuiSystem::event(const platform::Event &event) {
  using namespace platform;
  if (const auto *down = std::get_if<MouseButtonPressed>(&event)) {
    return press(mouse(down->button), down->position);
  }
  if (const auto *up = std::get_if<MouseButtonReleased>(&event)) {
    return release(mouse(up->button), up->position);
  }
  if (std::holds_alternative<MouseMoved>(event)) {
    // The mouse's motion belongs to the UI while any of its buttons is held on it.
    return std::ranges::any_of(m_captures, [](const Capture &capture) { return !capture.pointer.touch; });
  }
  if (const auto *down = std::get_if<TouchDown>(&event)) {
    return press({true, down->id}, down->position);
  }
  if (const auto *up = std::get_if<TouchUp>(&event)) {
    return release({true, up->id}, up->position);
  }
  if (const auto *moved = std::get_if<TouchMotion>(&event)) {
    return std::ranges::any_of(m_captures,
                               [&](const Capture &capture) { return capture.pointer == GuiPointer{true, moved->id}; });
  }
  if (const auto *focus = std::get_if<WindowFocusChanged>(&event); focus != nullptr && !focus->focused) {
    releaseAll();
  }
  return false;
}

bool GuiSystem::press(GuiPointer pointer, glm::vec2 position) {
  const flecs::entity_t target = hit(position * m_pointerScale);
  if (target == 0) {
    return false;
  }
  // A pointer that never reported its release (a window that lost the event) lets go first.
  if (const auto stale = std::ranges::find(m_captures, pointer, &Capture::pointer); stale != m_captures.end()) {
    const Capture lost = *stale;
    m_captures.erase(stale);
    drop(lost, false);
  }
  const GuiButton *button = flecs::entity{m_world.ecs(), target}.try_get<GuiButton>();
  const bool notify = clicks(pointer) && button != nullptr && button->interactable;
  if (notify && !pressed(target)) {
    m_events.push_back({GuiEventKind::Press, target, pointer});
  }
  m_captures.push_back({pointer, target, notify});
  return true;
}

bool GuiSystem::release(GuiPointer pointer, glm::vec2 position) {
  const auto held = std::ranges::find(m_captures, pointer, &Capture::pointer);
  if (held == m_captures.end()) {
    return false;
  }
  const Capture capture = *held;
  m_captures.erase(held);
  const GuiElement *element = findElement(m_elements, capture.entity);
  drop(capture, element != nullptr && element->rect.contains(position * m_pointerScale));
  return true;
}

void GuiSystem::releaseAll() {
  while (!m_captures.empty()) {
    const Capture capture = m_captures.front();
    m_captures.erase(m_captures.begin());
    drop(capture, false);
  }
}

void GuiSystem::drop(const Capture &capture, bool inside) {
  if (!capture.notify || pressed(capture.entity)) {
    return;
  }
  // A button that went away while it was held has nobody left to tell.
  if (!m_world.ecs().is_alive(capture.entity) || findElement(m_elements, capture.entity) == nullptr) {
    return;
  }
  m_events.push_back({GuiEventKind::Release, capture.entity, capture.pointer});
  if (inside) {
    m_events.push_back({GuiEventKind::Click, capture.entity, capture.pointer});
  }
}

flecs::entity_t GuiSystem::hit(glm::vec2 position) const {
  for (const GuiElement &element : m_elements | std::views::reverse) {
    if (!element.rect.contains(position)) {
      continue;
    }
    if (!m_world.ecs().is_alive(element.entity)) {
      continue;
    }
    const flecs::entity entity{m_world.ecs(), element.entity};
    const GuiButton *button = entity.try_get<GuiButton>();
    const GuiImage *image = entity.try_get<GuiImage>();
    if ((button != nullptr && button->interactable) || (image != nullptr && image->blocksInput)) {
      return element.entity;
    }
  }
  return 0;
}

} // namespace sonnet::gui
