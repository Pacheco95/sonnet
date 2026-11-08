#include <sonnet/platform/MouseLock.h>

#include <sonnet/core/Log.h>

#include <utility>
#include <variant>

namespace sonnet::platform {

MouseLock::MouseLock(InputState &input, Apply apply) : m_input(input), m_apply(std::move(apply)) {
}

MouseLock::~MouseLock() {
  // The input state may be gone already; only the window is told.
  if (m_locked) {
    static_cast<void>(m_apply(false));
  }
}

bool MouseLock::event(const Event &event) {
  if (const auto *focus = std::get_if<WindowFocusChanged>(&event)) {
    // SDL would take relative mode back by itself when the focus returns; a click has to.
    if (!focus->focused && m_locked) {
      setLocked(false);
      m_suspended = true;
    }
    return false;
  }
  if (const auto *pressed = std::get_if<KeyPressed>(&event)) {
    if (pressed->key == Key::Escape && m_locked) {
      setLocked(false);
      m_suspended = true;
      return true;
    }
    return false;
  }
  if (std::holds_alternative<MouseButtonPressed>(event)) {
    // The click that takes the pointer back is not a shot or a drag; its release, unmatched,
    // changes nothing in the input state.
    if (m_suspended && m_requested) {
      m_resumeClicked = true;
      return true;
    }
    return false;
  }
  if (std::holds_alternative<MouseMoved>(event)) {
    return m_skipMotion;
  }
  return false;
}

void MouseLock::update(bool requested, bool focused) {
  // The events since the last update have been offered: motion from now on is the mouse's.
  m_skipMotion = false;
  const bool clicked = std::exchange(m_resumeClicked, false);
  if (!requested) {
    // A request made afresh locks at once.
    m_suspended = false;
  } else if (clicked && focused) {
    m_suspended = false;
  }
  if (!requested || !focused) {
    m_refused = false;
  }
  m_requested = requested;
  m_focused = focused;
  const bool wanted = requested && focused && !m_suspended;
  if (wanted && !m_locked && !m_refused) {
    setLocked(true);
    if (!m_locked) {
      m_refused = true;
      SONNET_LOG_INFO("the pointer cannot be locked here; the game gets it unlocked");
    }
  } else if (!wanted && m_locked) {
    setLocked(false);
  }
  m_input.setMouseLocked(m_locked);
}

void MouseLock::release() {
  m_suspended = false;
  m_resumeClicked = false;
  m_refused = false;
  if (m_locked) {
    setLocked(false);
  }
  m_input.setMouseLocked(false);
}

void MouseLock::setLocked(bool locked) {
  // Leaving the mode is taken as done whatever the platform says: a pointer the game no longer
  // asks for is not the game's.
  const bool got = m_apply(locked) && locked;
  if (got != m_locked) {
    m_locked = got;
    // The motion gathered so far this frame was the free pointer's, or the locked one's, and the
    // first motion after the change can be the platform warping the pointer.
    m_input.clearMotion();
    m_skipMotion = true;
  }
  m_input.setMouseLocked(m_locked);
}

} // namespace sonnet::platform
