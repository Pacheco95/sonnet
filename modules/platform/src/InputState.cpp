#include <sonnet/platform/InputState.h>

#include <algorithm>
#include <variant>

namespace sonnet::platform {

namespace {

std::size_t index(Key key) noexcept {
  return static_cast<std::size_t>(key);
}

std::size_t index(MouseButton button) noexcept {
  return static_cast<std::size_t>(button);
}

std::size_t index(GamepadButton button) noexcept {
  return static_cast<std::size_t>(button);
}

std::size_t index(GamepadAxis axis) noexcept {
  return static_cast<std::size_t>(axis);
}

// A radial dead zone: inside it the stick is centred; outside, the length is rescaled from the dead
// zone's edge to `full` and kept within 1, the direction as it is.
glm::vec2 radial(glm::vec2 raw, float deadZone, float full) noexcept {
  const float length = glm::length(raw);
  if (length <= deadZone) {
    return {0.0f, 0.0f};
  }
  const float scaled = std::min((length - deadZone) / (full - deadZone), 1.0f);
  return raw * (scaled / length);
}

float trigger(float raw, float deadZone, float full) noexcept {
  return std::clamp((raw - deadZone) / (full - deadZone), 0.0f, 1.0f);
}

} // namespace

void InputState::beginFrame() noexcept {
  m_keysPressed.reset();
  m_keysReleased.reset();
  m_buttonsPressed.reset();
  m_buttonsReleased.reset();
  m_mouseDelta = {0.0f, 0.0f};
  m_wheel = {0.0f, 0.0f};
  for (Touch &touch : std::span{m_touches}.first(m_touchCount)) {
    touch.delta = {0.0f, 0.0f};
  }
  m_padPressed.reset();
  m_padReleased.reset();
}

void InputState::handle(const Event &event) noexcept {
  if (const auto *pressed = std::get_if<KeyPressed>(&event)) {
    // A held key's repeats are not new presses.
    if (pressed->key != Key::Unknown && !pressed->repeat && !m_keysDown.test(index(pressed->key))) {
      m_keysDown.set(index(pressed->key));
      m_keysPressed.set(index(pressed->key));
    }
  } else if (const auto *released = std::get_if<KeyReleased>(&event)) {
    if (released->key != Key::Unknown && m_keysDown.test(index(released->key))) {
      m_keysDown.reset(index(released->key));
      m_keysReleased.set(index(released->key));
    }
  } else if (const auto *down = std::get_if<MouseButtonPressed>(&event)) {
    m_buttonsDown.set(index(down->button));
    m_buttonsPressed.set(index(down->button));
    m_mousePosition = down->position;
  } else if (const auto *up = std::get_if<MouseButtonReleased>(&event)) {
    if (m_buttonsDown.test(index(up->button))) {
      m_buttonsDown.reset(index(up->button));
      m_buttonsReleased.set(index(up->button));
    }
    m_mousePosition = up->position;
  } else if (const auto *moved = std::get_if<MouseMoved>(&event)) {
    m_mousePosition = moved->position;
    m_mouseDelta += moved->delta;
  } else if (const auto *wheel = std::get_if<MouseWheel>(&event)) {
    m_wheel += wheel->delta;
  } else if (const auto *touchDown = std::get_if<TouchDown>(&event)) {
    if (findTouch(touchDown->id) == nullptr && m_touchCount < MaxTouches) {
      m_touches[m_touchCount++] = Touch{.id = touchDown->id, .position = touchDown->position, .delta = {0.0f, 0.0f}};
    }
  } else if (const auto *touchMotion = std::get_if<TouchMotion>(&event)) {
    // A finger that went down before the game was listening is not the game's.
    if (Touch *touch = findTouch(touchMotion->id)) {
      touch->position = touchMotion->position;
      touch->delta += touchMotion->delta;
    }
  } else if (const auto *touchUp = std::get_if<TouchUp>(&event)) {
    if (const Touch *touch = findTouch(touchUp->id)) {
      // Keeps the order the others went down in.
      const auto at = static_cast<std::ptrdiff_t>(touch - m_touches.data());
      std::shift_left(m_touches.begin() + at, m_touches.begin() + static_cast<std::ptrdiff_t>(m_touchCount), 1);
      --m_touchCount;
    }
  } else if (const auto *padDown = std::get_if<GamepadButtonPressed>(&event)) {
    // As with keys, a button already held is not pressed again.
    if (padDown->button != GamepadButton::Count && !m_padDown.test(index(padDown->button))) {
      m_padDown.set(index(padDown->button));
      m_padPressed.set(index(padDown->button));
    }
  } else if (const auto *padUp = std::get_if<GamepadButtonReleased>(&event)) {
    // A button held before the state saw it go down has nothing to release.
    if (padUp->button != GamepadButton::Count && m_padDown.test(index(padUp->button))) {
      m_padDown.reset(index(padUp->button));
      m_padReleased.set(index(padUp->button));
    }
  } else if (const auto *axis = std::get_if<GamepadAxisMoved>(&event)) {
    if (axis->axis != GamepadAxis::Count) {
      m_padAxes[index(axis->axis)] = axis->value;
    }
  } else if (std::holds_alternative<GamepadConnected>(event)) {
    // A gamepad replacing another starts with nothing held: the old one's buttons are let go.
    releaseGamepad();
    m_gamepadConnected = true;
  } else if (std::holds_alternative<GamepadDisconnected>(event)) {
    releaseGamepad();
    m_gamepadConnected = false;
  } else if (const auto *focus = std::get_if<WindowFocusChanged>(&event)) {
    // The release of a key held while the focus leaves never arrives.
    if (!focus->focused) {
      releaseAll();
    }
  }
}

void InputState::releaseAll() noexcept {
  m_keysReleased |= m_keysDown;
  m_keysDown.reset();
  m_buttonsReleased |= m_buttonsDown;
  m_buttonsDown.reset();
  m_touchCount = 0;
  releaseGamepad();
}

void InputState::reset() noexcept {
  const bool connected = m_gamepadConnected;
  *this = InputState{};
  m_gamepadConnected = connected;
}

void InputState::releaseGamepad() noexcept {
  m_padReleased |= m_padDown;
  m_padDown.reset();
  m_padAxes.fill(0.0f);
}

Touch *InputState::findTouch(std::uint64_t id) noexcept {
  const auto active = std::span{m_touches}.first(m_touchCount);
  const auto it = std::ranges::find(active, id, &Touch::id);
  return it != active.end() ? &*it : nullptr;
}

bool InputState::keyDown(Key key) const noexcept {
  return key != Key::Unknown && key != Key::Count && m_keysDown.test(index(key));
}

bool InputState::keyPressed(Key key) const noexcept {
  return key != Key::Unknown && key != Key::Count && m_keysPressed.test(index(key));
}

bool InputState::keyReleased(Key key) const noexcept {
  return key != Key::Unknown && key != Key::Count && m_keysReleased.test(index(key));
}

bool InputState::mouseDown(MouseButton button) const noexcept {
  return button != MouseButton::Count && m_buttonsDown.test(index(button));
}

bool InputState::mousePressed(MouseButton button) const noexcept {
  return button != MouseButton::Count && m_buttonsPressed.test(index(button));
}

bool InputState::mouseReleased(MouseButton button) const noexcept {
  return button != MouseButton::Count && m_buttonsReleased.test(index(button));
}

bool InputState::gamepadDown(GamepadButton button) const noexcept {
  return button != GamepadButton::Count && m_padDown.test(index(button));
}

bool InputState::gamepadPressed(GamepadButton button) const noexcept {
  return button != GamepadButton::Count && m_padPressed.test(index(button));
}

bool InputState::gamepadReleased(GamepadButton button) const noexcept {
  return button != GamepadButton::Count && m_padReleased.test(index(button));
}

glm::vec2 InputState::leftStick() const noexcept {
  return radial({m_padAxes[index(GamepadAxis::LeftX)], m_padAxes[index(GamepadAxis::LeftY)]}, StickDeadZone, StickFull);
}

glm::vec2 InputState::rightStick() const noexcept {
  return radial({m_padAxes[index(GamepadAxis::RightX)], m_padAxes[index(GamepadAxis::RightY)]}, StickDeadZone,
                StickFull);
}

float InputState::leftTrigger() const noexcept {
  return trigger(m_padAxes[index(GamepadAxis::LeftTrigger)], TriggerDeadZone, TriggerFull);
}

float InputState::rightTrigger() const noexcept {
  return trigger(m_padAxes[index(GamepadAxis::RightTrigger)], TriggerDeadZone, TriggerFull);
}

float InputState::gamepadAxis(GamepadAxis axis) const noexcept {
  return axis != GamepadAxis::Count ? m_padAxes[index(axis)] : 0.0f;
}

} // namespace sonnet::platform
