#pragma once

#include <sonnet/gui/Components.h>
#include <sonnet/gui/Layout.h>

#include <sonnet/platform/Event.h>
#include <sonnet/platform/Window.h>

#include <flecs.h>

#include <cstdint>
#include <span>
#include <vector>

namespace sonnet::world {
class World;
}

namespace sonnet::gui {

// A mouse button or a finger. The mouse's pointers are its buttons, told apart by `id`.
struct GuiPointer {
  bool touch{false};
  std::uint64_t id{0};

  friend bool operator==(const GuiPointer &, const GuiPointer &) = default;
};

enum class GuiEventKind : std::uint8_t {
  Press,   // a pointer went down on the button
  Release, // the last pointer holding the button went up, wherever it was
  Click,   // ... and it was over the button
};

// What happened to a button, as data for `scripting` to deliver (ADR-0026). Only the left mouse
// button and the fingers make events.
struct GuiEvent {
  GuiEventKind kind{GuiEventKind::Press};
  flecs::entity_t entity{0};
  GuiPointer pointer;
};

// The game UI of a world (docs/gui.md): lays out its canvases and takes the pointer events that
// land on a button, ahead of the game. Registers the components on construction.
class GuiSystem {
public:
  explicit GuiSystem(world::World &world);

  // Lays the canvases out for a target of `targetSize` pixels shown in a window of `windowSize` in
  // the coordinates of the input events, whose `safeArea` insets the canvases that ask for it.
  // Run once a frame after the world has updated; event() hit-tests against the result.
  void update(glm::vec2 targetSize, glm::vec2 windowSize, platform::WindowRect safeArea);
  // Offers an event to the UI before the game. True when it is consumed: a press on a button or
  // an image that blocks input, and the motion and release of a pointer holding one. The rest are
  // the game's.
  [[nodiscard]] bool event(const platform::Event &event);
  // Forgets the events delivered this frame.
  void endFrame() noexcept {
    m_events.clear();
  }

  // The events since the last endFrame, in the order they happened.
  [[nodiscard]] std::span<const GuiEvent> events() const noexcept {
    return m_events;
  }
  // The laid-out elements of the last update, in draw order.
  [[nodiscard]] std::span<const GuiElement> elements() const noexcept {
    return m_elements;
  }
  // Whether a pointer is holding the entity's button.
  [[nodiscard]] bool pressed(flecs::entity_t entity) const noexcept;

private:
  struct Capture {
    GuiPointer pointer;
    flecs::entity_t entity{0};
    bool notify{false}; // an interactable button held by the left button or a finger: it makes events
  };

  [[nodiscard]] bool press(GuiPointer pointer, glm::vec2 position);
  [[nodiscard]] bool release(GuiPointer pointer, glm::vec2 position);
  void cancelAll();
  // The front-most element that takes pointer events at a position in target pixels.
  [[nodiscard]] flecs::entity_t hit(glm::vec2 position) const;
  // The pointer lets go of the entity: Release and Click once no other holds it.
  void drop(const Capture &capture, bool inside);

  world::World &m_world;
  std::vector<GuiElement> m_elements;
  std::vector<GuiEvent> m_events;
  std::vector<Capture> m_captures;
  glm::vec2 m_pointerScale{1.0f, 1.0f}; // window coordinates to target pixels
};

} // namespace sonnet::gui
