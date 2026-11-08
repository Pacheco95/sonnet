#pragma once

#include <sonnet/platform/Event.h>
#include <sonnet/platform/InputState.h>

#include <functional>

namespace sonnet::platform {

// Applies a game's wish to lock the pointer (docs/platform.md, "Mouse lock"): the player's Game and
// the editor's play mode both own one, so the rules are the same in both. A game asks; the lock
// holds the pointer only while the game has the input, lets it go on Escape, a lost focus or
// release(), takes it back on the next click into the game, and reports what it got to the
// InputState the scripts read.
class MouseLock {
public:
  // Asks the platform for relative mouse mode or out of it (IWindow::setRelativeMouseMode), and
  // says whether it got it. Leaving the mode is expected to succeed.
  using Apply = std::function<bool(bool enabled)>;

  MouseLock(InputState &input, Apply apply);
  ~MouseLock();
  MouseLock(const MouseLock &) = delete;
  MouseLock &operator=(const MouseLock &) = delete;

  // Offered each event before the game sees it; true when the event was the lock's and the game
  // must not see it: the Escape that let the pointer go, the click that takes it back, and the
  // motion that arrives between a change of mode and the next update, whose delta is the jump the
  // change made rather than the mouse's motion. A lost focus lets go and is still the game's.
  // The caller offers only the presses that land in the game's view.
  [[nodiscard]] bool event(const Event &event);
  // Once a frame, before the game reads its input: `requested` is the game's wish
  // (scripting::IScriptRuntime::mouseLockRequested), `focused` whether the game has the input now
  // (the window has the focus and the game runs). Locks or unlocks to match and reports the result
  // through InputState::setMouseLocked. A request the platform refuses is not asked again until the
  // request or the focus goes away and comes back, so the game falls back to an unlocked pointer
  // without a warning every frame.
  void update(bool requested, bool focused);
  // Lets the pointer go and forgets the Escape or lost focus waiting for a click: play stopping or
  // pausing, a scene being replaced, the game going away.
  void release();

  // Whether the pointer is locked now.
  [[nodiscard]] bool locked() const noexcept {
    return m_locked;
  }
  // The pointer was let go by Escape or a lost focus while the game still asks for it, and waits
  // for a click into the game.
  [[nodiscard]] bool suspended() const noexcept {
    return m_suspended;
  }

private:
  void setLocked(bool locked);

  InputState &m_input;
  Apply m_apply;
  bool m_locked{false};
  bool m_requested{false};
  bool m_focused{false};
  bool m_suspended{false};
  bool m_resumeClicked{false};
  bool m_refused{false};
  bool m_skipMotion{false};
};

} // namespace sonnet::platform
