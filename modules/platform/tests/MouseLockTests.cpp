#include <sonnet/platform/MouseLock.h>

#include <sonnet/platform/Platform.h>

#include <sonnet/core/Error.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace sonnet::platform;

namespace {

// Stands in for the window's relative mouse mode: whether the platform has it, what it is now,
// and how often it was asked.
struct FakeMode {
  bool supported{true};
  bool enabled{false};
  int requests{0};

  [[nodiscard]] MouseLock::Apply apply() {
    return [this](bool on) {
      ++requests;
      if (on && !supported) {
        return false;
      }
      enabled = on;
      return true;
    };
  }
};

const Event Escape = KeyPressed{.key = Key::Escape, .modifiers = Modifiers::None, .repeat = false};
const Event Click = MouseButtonPressed{.button = MouseButton::Left, .position = {5.0f, 5.0f}, .clicks = 1};
const Event Move = MouseMoved{.position = {300.0f, 200.0f}, .delta = {250.0f, -40.0f}};

} // namespace

TEST_CASE("the pointer is locked while the game asks and has the input, and the state is reported",
          "[platform][input][mouselock]") {
  InputState input;
  FakeMode mode;
  MouseLock lock{input, mode.apply()};

  // Asked, but the game does not have the input: nothing happens yet.
  lock.update(true, false);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(input.mouseLocked());
  REQUIRE(mode.requests == 0);

  lock.update(true, true);
  REQUIRE(lock.locked());
  REQUIRE(mode.enabled);
  REQUIRE(input.mouseLocked());
  // Held without asking the platform again every frame.
  lock.update(true, true);
  REQUIRE(mode.requests == 1);

  // The game losing the input (a pause, another panel focused) lets go, and having it back takes
  // the pointer again without a click.
  lock.update(true, false);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(mode.enabled);
  REQUIRE_FALSE(input.mouseLocked());
  lock.update(true, true);
  REQUIRE(lock.locked());

  // The game no longer asking lets go.
  lock.update(false, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(mode.enabled);
  REQUIRE_FALSE(input.mouseLocked());
}

TEST_CASE("Escape and a lost focus let the pointer go until a click into the game", "[platform][input][mouselock]") {
  InputState input;
  FakeMode mode;
  MouseLock lock{input, mode.apply()};
  lock.update(true, true);
  REQUIRE(lock.locked());

  // Escape is the lock's, not the game's.
  REQUIRE(lock.event(Escape));
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(mode.enabled);
  REQUIRE(lock.suspended());
  lock.update(true, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(input.mouseLocked());
  // Escape with nothing locked is the game's.
  REQUIRE_FALSE(lock.event(Escape));

  // The click that takes it back is the lock's too.
  REQUIRE(lock.event(Click));
  lock.update(true, true);
  REQUIRE(lock.locked());
  REQUIRE(input.mouseLocked());
  // Clicks while locked are the game's.
  REQUIRE_FALSE(lock.event(Click));

  // A lost focus lets go and stays the game's event, so its input releases what was held; the
  // focus coming back does not take the pointer until a click.
  REQUIRE_FALSE(lock.event(WindowFocusChanged{.focused = false}));
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(mode.enabled);
  REQUIRE_FALSE(lock.event(WindowFocusChanged{.focused = true}));
  lock.update(true, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE(lock.event(Click));
  lock.update(true, true);
  REQUIRE(lock.locked());

  // The click counts when the game has the input at the update that follows it: in the editor the
  // click into the Game panel is what focuses it. One that did not give the game the input is
  // forgotten.
  REQUIRE(lock.event(Escape));
  REQUIRE(lock.event(Click));
  lock.update(true, false);
  lock.update(true, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE(lock.event(Click));
  lock.update(true, true);
  REQUIRE(lock.locked());

  // A request made afresh after Escape locks at once.
  REQUIRE(lock.event(Escape));
  lock.update(false, true);
  REQUIRE_FALSE(lock.suspended());
  lock.update(true, true);
  REQUIRE(lock.locked());
}

TEST_CASE("release lets the pointer go and forgets a pending click", "[platform][input][mouselock]") {
  InputState input;
  FakeMode mode;
  MouseLock lock{input, mode.apply()};
  lock.update(true, true);
  REQUIRE(lock.event(Escape));
  REQUIRE(lock.suspended());

  // Stop, pause, a scene replaced: whatever was locked or waiting goes.
  lock.release();
  REQUIRE_FALSE(lock.suspended());
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(input.mouseLocked());
  REQUIRE_FALSE(lock.event(Click));

  lock.update(true, true);
  REQUIRE(lock.locked());
  lock.release();
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(mode.enabled);
  REQUIRE_FALSE(input.mouseLocked());

  // The lock going away gives the pointer back too.
  {
    MouseLock scoped{input, mode.apply()};
    scoped.update(true, true);
    REQUIRE(mode.enabled);
  }
  REQUIRE_FALSE(mode.enabled);
}

TEST_CASE("a platform without relative mode leaves the pointer unlocked and is not asked every frame",
          "[platform][input][mouselock]") {
  InputState input;
  FakeMode mode{.supported = false};
  MouseLock lock{input, mode.apply()};
  lock.update(true, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(input.mouseLocked());
  REQUIRE(mode.requests == 1);
  for (int frame = 0; frame < 10; ++frame) {
    lock.update(true, true);
  }
  REQUIRE(mode.requests == 1);
  // Clicks and Escape stay the game's, which falls back to dragging.
  REQUIRE_FALSE(lock.event(Click));
  REQUIRE_FALSE(lock.event(Escape));
  REQUIRE_FALSE(lock.suspended());

  // Asked afresh once the focus comes back.
  lock.update(true, false);
  lock.update(true, true);
  REQUIRE(mode.requests == 2);
  REQUIRE_FALSE(input.mouseLocked());
}

TEST_CASE("the motion around a change of mode does not reach the game", "[platform][input][mouselock]") {
  InputState input;
  FakeMode mode;
  MouseLock lock{input, mode.apply()};
  input.handle(std::get<MouseMoved>(Move));
  REQUIRE(input.mouseDelta() != glm::vec2{0.0f});

  // The free pointer's motion this frame is gone once it locks, and so is the platform's warp
  // that arrives before the next update.
  lock.update(true, true);
  REQUIRE(input.mouseDelta() == glm::vec2{0.0f});
  REQUIRE(lock.event(Move));
  lock.update(true, true);
  REQUIRE_FALSE(lock.event(Move));

  // Likewise when Escape lets it go.
  REQUIRE(lock.event(Escape));
  REQUIRE(lock.event(Move));
  lock.update(true, true);
  REQUIRE_FALSE(lock.event(Move));

  // A refused request changes nothing, so it hides no motion.
  FakeMode unsupported{.supported = false};
  MouseLock refused{input, unsupported.apply()};
  input.handle(std::get<MouseMoved>(Move));
  refused.update(true, true);
  REQUIRE(input.mouseDelta() != glm::vec2{0.0f});
  REQUIRE_FALSE(refused.event(Move));
}

TEST_CASE("the headless driver refuses relative mode, and the lock reports it unlocked",
          "[platform][input][mouselock][window]") {
  // What a test can check of the real mode: that a refusal is reported. A headless success would
  // say nothing about a pointer held on a desktop, which needs a real window.
  Platform platform{{.headless = true}};
  // Windows are created Vulkan-capable, which needs the Vulkan library (WindowTests.cpp).
  try {
    static_cast<void>(platform.vulkanGetInstanceProcAddr());
  } catch (const sonnet::core::Exception &e) {
    SKIP("no Vulkan loader on this machine: " << e.what());
  }
  const std::unique_ptr<IWindow> window = platform.createWindow({.title = "mouselock", .size = {320, 240}});
  InputState input;
  MouseLock lock{input, [&](bool on) { return window->setRelativeMouseMode(on); }};
  lock.update(true, true);
  REQUIRE_FALSE(lock.locked());
  REQUIRE_FALSE(input.mouseLocked());
  REQUIRE_FALSE(window->relativeMouseMode());
}
