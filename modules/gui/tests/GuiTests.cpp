#include "Draw.h"

#include <sonnet/gui/GuiSystem.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/InputState.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/Scene.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace sonnet;
using Catch::Approx;

namespace {

constexpr gui::PixelRect Reference{{0.0f, 0.0f}, {1920.0f, 1080.0f}};

struct Fixture {
  world::World world;
  gui::GuiSystem system{world};

  flecs::entity canvas(const char *name = "Canvas", gui::GuiCanvas spec = {}) {
    const flecs::entity entity = world.createEntity(name);
    entity.set<gui::GuiCanvas>(spec);
    return entity;
  }

  // An element whose rectangle is `offsetMin` to `offsetMax` from the point `anchor` of its parent.
  flecs::entity element(const char *name, flecs::entity parent, glm::vec2 anchor, glm::vec2 offsetMin,
                        glm::vec2 offsetMax) {
    return stretched(name, parent, anchor, anchor, offsetMin, offsetMax);
  }
  flecs::entity stretched(const char *name, flecs::entity parent, glm::vec2 anchorMin, glm::vec2 anchorMax,
                          glm::vec2 offsetMin, glm::vec2 offsetMax) {
    const flecs::entity entity = world.createEntity(name, parent);
    entity.set<gui::GuiRect>({.anchorMin = anchorMin,
                              .anchorMax = anchorMax,
                              .pivot = {0.5f, 0.5f},
                              .offsetMin = offsetMin,
                              .offsetMax = offsetMax});
    return entity;
  }
  flecs::entity button(const char *name, flecs::entity parent, glm::vec2 min, glm::vec2 max) {
    const flecs::entity entity = element(name, parent, {0.0f, 0.0f}, min, max);
    entity.set<gui::GuiButton>({});
    return entity;
  }

  [[nodiscard]] std::vector<gui::GuiElement> layout(glm::vec2 target = {1920.0f, 1080.0f},
                                                    gui::PixelRect safeArea = Reference) const {
    return gui::layoutGui(world, target, {safeArea.min, safeArea.max});
  }
  [[nodiscard]] gui::PixelRect rectOf(const std::vector<gui::GuiElement> &elements, flecs::entity entity) const {
    const gui::GuiElement *found = gui::findElement(elements, entity.id());
    REQUIRE(found != nullptr);
    return found->rect;
  }
  void update(glm::vec2 target = {1920.0f, 1080.0f}) {
    system.update(target, target, {{0, 0}, {static_cast<unsigned>(target.x), static_cast<unsigned>(target.y)}});
  }

  bool press(glm::vec2 at) {
    return system.event(
        platform::MouseButtonPressed{.button = platform::MouseButton::Left, .position = at, .clicks = 1});
  }
  bool release(glm::vec2 at) {
    return system.event(platform::MouseButtonReleased{.button = platform::MouseButton::Left, .position = at});
  }
  [[nodiscard]] std::vector<gui::GuiEventKind> kinds() const {
    std::vector<gui::GuiEventKind> out;
    for (const gui::GuiEvent &event : system.events()) {
      out.push_back(event.kind);
    }
    return out;
  }
};

// A renderer on the null device and an asset database over it, for the draw tests.
struct DrawFixture : Fixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 1}};
  assets::AssetDatabase assets{renderer, jobs};

  // An image over `rect` of a canvas that needs no scaling.
  flecs::entity image(const char *name, flecs::entity canvas, glm::vec2 min, glm::vec2 max, gui::GuiImage spec) {
    const flecs::entity entity = element(name, canvas, {0.0f, 0.0f}, min, max);
    entity.set<gui::GuiImage>(spec);
    return entity;
  }
  void build() {
    update();
    system.buildQuads(renderer, assets);
  }
};

[[nodiscard]] bool near(const gui::PixelRect &rect, glm::vec2 min, glm::vec2 max) {
  return glm::all(glm::lessThan(glm::abs(rect.min - min), glm::vec2{0.01f})) &&
         glm::all(glm::lessThan(glm::abs(rect.max - max), glm::vec2{0.01f}));
}

} // namespace

TEST_CASE("anchors, offsets and pivots give hand-computed rectangles", "[gui][layout]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  // A 200 x 100 element at the top-left corner, 10 in from each edge.
  const flecs::entity corner = f.element("Corner", canvas, {0.0f, 0.0f}, {10.0f, 10.0f}, {210.0f, 110.0f});
  // Centred, the default anchors: 100 x 100 around the middle.
  const flecs::entity centre = f.element("Centre", canvas, {0.5f, 0.5f}, {-50.0f, -50.0f}, {50.0f, 50.0f});
  // Stretched over the whole canvas with a 20 pixel margin.
  const flecs::entity panel =
      f.stretched("Panel", canvas, {0.0f, 0.0f}, {1.0f, 1.0f}, {20.0f, 20.0f}, {-20.0f, -20.0f});
  // Anchored to a quarter of its parent, inside the panel.
  const flecs::entity quarter = f.stretched("Quarter", panel, {0.0f, 0.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
  panel.get_mut<gui::GuiRect>().pivot = {0.0f, 1.0f};
  const std::vector<gui::GuiElement> elements = f.layout();

  REQUIRE(f.rectOf(elements, canvas) == Reference);
  REQUIRE(f.rectOf(elements, corner) == gui::PixelRect{{10.0f, 10.0f}, {210.0f, 110.0f}});
  REQUIRE(f.rectOf(elements, centre) == gui::PixelRect{{910.0f, 490.0f}, {1010.0f, 590.0f}});
  REQUIRE(f.rectOf(elements, panel) == gui::PixelRect{{20.0f, 20.0f}, {1900.0f, 1060.0f}});
  REQUIRE(f.rectOf(elements, quarter) == gui::PixelRect{{20.0f, 20.0f}, {960.0f, 540.0f}});
  // The pivot is reported in target pixels: the panel's bottom-left corner.
  REQUIRE(gui::findElement(elements, panel.id())->pivot == glm::vec2{20.0f, 1060.0f});
  // The tree is in draw order: a parent, then its children in sibling order.
  REQUIRE(elements.size() == 5);
  REQUIRE(elements[0].entity == canvas.id());
  REQUIRE(elements[3].entity == panel.id());
  REQUIRE(elements[4].entity == quarter.id());
}

TEST_CASE("row and column layouts place children from their sizes, spacing and padding", "[gui][layout]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity column =
      f.stretched("Column", canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {400.0f, 300.0f});
  column.set<gui::GuiLayout>({.axis = gui::GuiAxis::Column,
                              .spacing = 10.0f,
                              .padding = {5.0f, 6.0f, 7.0f, 8.0f},
                              .alignment = gui::GuiAlignment::Start});
  const flecs::entity a = f.element("A", column, {0.5f, 0.5f}, {0.0f, 0.0f}, {100.0f, 40.0f});
  const flecs::entity b = f.element("B", column, {0.5f, 0.5f}, {0.0f, 0.0f}, {60.0f, 20.0f});
  const flecs::entity hidden = f.element("Hidden", column, {0.5f, 0.5f}, {0.0f, 0.0f}, {60.0f, 20.0f});
  hidden.add<world::Disabled>();
  const flecs::entity c = f.element("C", column, {0.5f, 0.5f}, {0.0f, 0.0f}, {80.0f, 30.0f});

  std::vector<gui::GuiElement> elements = f.layout();
  REQUIRE(f.rectOf(elements, a) == gui::PixelRect{{5.0f, 6.0f}, {105.0f, 46.0f}});
  REQUIRE(f.rectOf(elements, b) == gui::PixelRect{{5.0f, 56.0f}, {65.0f, 76.0f}}); // 46 + 10
  REQUIRE(f.rectOf(elements, c) ==
          gui::PixelRect{{5.0f, 86.0f}, {85.0f, 116.0f}}); // 76 + 10, no gap for the hidden one
  REQUIRE(gui::findElement(elements, hidden.id()) == nullptr);

  // Centred: the block of heights 40 + 20 + 30 + 2 * 10 = 110 in the 286 tall content area, each
  // child across the 388 wide one.
  column.get_mut<gui::GuiLayout>().alignment = gui::GuiAlignment::Center;
  elements = f.layout();
  REQUIRE(near(f.rectOf(elements, a), {149.0f, 94.0f}, {249.0f, 134.0f}));
  REQUIRE(near(f.rectOf(elements, c), {159.0f, 174.0f}, {239.0f, 204.0f}));

  // A row, to the end: widths 100 + 60 + 80 + 2 * 10 = 260, right-aligned in 388, bottom-aligned in 286.
  column.get_mut<gui::GuiLayout>() = {.axis = gui::GuiAxis::Row,
                                      .spacing = 10.0f,
                                      .padding = {5.0f, 6.0f, 7.0f, 8.0f},
                                      .alignment = gui::GuiAlignment::End};
  elements = f.layout();
  REQUIRE(near(f.rectOf(elements, a), {133.0f, 252.0f}, {233.0f, 292.0f}));
  REQUIRE(near(f.rectOf(elements, b), {243.0f, 272.0f}, {303.0f, 292.0f}));
  REQUIRE(near(f.rectOf(elements, c), {313.0f, 262.0f}, {393.0f, 292.0f}));
}

TEST_CASE("the canvas scale follows the reference size by width, height or between", "[gui][layout]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity box = f.element("Box", canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f});
  const auto scaleOf = [&](glm::vec2 target) {
    return gui::findElement(f.layout(target, {{0, 0}, target}), box.id())->scale;
  };

  // Twice as wide and 1.5 times as tall as the 1920 x 1080 reference.
  const glm::vec2 target{3840.0f, 1620.0f};
  canvas.get_mut<gui::GuiCanvas>().match = 0.0f;
  REQUIRE(scaleOf(target) == Approx(2.0f));
  canvas.get_mut<gui::GuiCanvas>().match = 1.0f;
  REQUIRE(scaleOf(target) == Approx(1.5f));
  canvas.get_mut<gui::GuiCanvas>().match = 0.5f;
  REQUIRE(scaleOf(target) == Approx(std::sqrt(3.0f))); // the geometric mean
  // The size of the element follows the scale.
  canvas.get_mut<gui::GuiCanvas>().match = 0.0f;
  const std::vector<gui::GuiElement> elements = f.layout(target, {{0, 0}, target});
  REQUIRE(f.rectOf(elements, box) == gui::PixelRect{{0.0f, 0.0f}, {200.0f, 200.0f}});
}

TEST_CASE("a canvas that asks for it is inset by the safe area, scaled from the whole target", "[gui][layout]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity corner = f.element("Corner", canvas, {1.0f, 1.0f}, {-100.0f, -100.0f}, {0.0f, 0.0f});
  const gui::PixelRect notch{{0.0f, 60.0f}, {1920.0f, 1000.0f}};

  std::vector<gui::GuiElement> elements = f.layout({1920.0f, 1080.0f}, notch);
  REQUIRE(f.rectOf(elements, canvas) == notch);
  REQUIRE(f.rectOf(elements, corner) == gui::PixelRect{{1820.0f, 900.0f}, {1920.0f, 1000.0f}});
  REQUIRE(gui::findElement(elements, corner.id())->scale == Approx(1.0f));

  canvas.get_mut<gui::GuiCanvas>().safeArea = false;
  elements = f.layout({1920.0f, 1080.0f}, notch);
  REQUIRE(f.rectOf(elements, canvas) == Reference);
  // An empty safe area is no inset.
  canvas.get_mut<gui::GuiCanvas>().safeArea = true;
  REQUIRE(f.rectOf(f.layout({1920.0f, 1080.0f}, {}), canvas) == Reference);
}

TEST_CASE("canvases lay out in order, and a disabled subtree is hidden and takes no input", "[gui][layout]") {
  Fixture f;
  const flecs::entity late = f.canvas("Late", {.order = 2});
  const flecs::entity early = f.canvas("Early", {.order = 1});
  const flecs::entity tie = f.canvas("Tie", {.order = 1});
  const flecs::entity panel = f.element("Panel", early, {0.0f, 0.0f}, {0.0f, 0.0f}, {100.0f, 100.0f});
  const flecs::entity inside = f.button("Inside", panel, {0.0f, 0.0f}, {50.0f, 50.0f});

  std::vector<gui::GuiElement> elements = f.layout();
  REQUIRE(elements.size() == 5);
  REQUIRE(elements[0].entity == early.id());
  REQUIRE(elements[1].entity == panel.id());
  REQUIRE(elements[2].entity == inside.id());
  REQUIRE(elements[3].entity == tie.id());
  REQUIRE(elements[4].entity == late.id());

  f.update();
  REQUIRE(f.press({10.0f, 10.0f}));
  REQUIRE(f.release({10.0f, 10.0f}));

  panel.add<world::Disabled>();
  elements = f.layout();
  REQUIRE(gui::findElement(elements, panel.id()) == nullptr);
  REQUIRE(gui::findElement(elements, inside.id()) == nullptr); // the tag is on its parent
  f.update();
  REQUIRE_FALSE(f.press({10.0f, 10.0f}));

  // A canvas under a disabled entity goes with it.
  early.add<world::Disabled>();
  REQUIRE(gui::findElement(f.layout(), early.id()) == nullptr);
}

TEST_CASE("the front-most interactive element takes the press", "[gui][input]") {
  Fixture f;
  const flecs::entity back = f.canvas("Back", {.order = 0});
  const flecs::entity front = f.canvas("Front", {.order = 1});
  const flecs::entity below = f.button("Below", back, {0.0f, 0.0f}, {200.0f, 200.0f});
  const flecs::entity above = f.button("Above", front, {100.0f, 100.0f}, {300.0f, 300.0f});
  const flecs::entity first = f.button("First", back, {400.0f, 0.0f}, {600.0f, 200.0f});
  const flecs::entity second = f.button("Second", back, {500.0f, 100.0f}, {700.0f, 300.0f});
  // A label over a button, which is not interactive, and a disabled button.
  const flecs::entity label = f.element("Label", back, {0.0f, 0.0f}, {0.0f, 400.0f}, {100.0f, 500.0f});
  label.set<gui::GuiText>({});
  const flecs::entity off = f.button("Off", back, {0.0f, 400.0f}, {100.0f, 500.0f});
  off.get_mut<gui::GuiButton>().interactable = false;
  f.update();

  const auto pressedAt = [&](glm::vec2 at) {
    REQUIRE(f.press(at));
    const flecs::entity_t target = f.system.events().back().entity;
    REQUIRE(f.release({-1.0f, -1.0f})); // let go, away from everything
    return target;
  };
  REQUIRE(pressedAt({50.0f, 50.0f}) == below.id());
  REQUIRE(pressedAt({150.0f, 150.0f}) == above.id());  // overlapping canvases: the later one is in front
  REQUIRE(pressedAt({550.0f, 150.0f}) == second.id()); // overlapping siblings: the later one is in front
  REQUIRE(pressedAt({450.0f, 50.0f}) == first.id());
  REQUIRE_FALSE(f.press({50.0f, 450.0f})); // the label and the disabled button pass it through
  REQUIRE_FALSE(f.press({1000.0f, 1000.0f}));
}

TEST_CASE("an image that blocks input consumes a press without a button", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity shade = f.stretched("Shade", canvas, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f});
  shade.set<gui::GuiImage>({});
  f.update();
  REQUIRE_FALSE(f.press({100.0f, 100.0f}));
  shade.get_mut<gui::GuiImage>().blocksInput = true;
  REQUIRE(f.press({100.0f, 100.0f}));
  REQUIRE(f.release({100.0f, 100.0f}));
  REQUIRE(f.system.events().empty()); // it is not a button: no events
}

TEST_CASE("a pointer is captured until its release, and a click needs the release over the button", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity play = f.button("Play", canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
  f.update();

  // Press and release inside: press, release, one click.
  REQUIRE(f.press({50.0f, 50.0f}));
  REQUIRE(f.system.pressed(play.id()));
  // The motion of the held mouse is the UI's, wherever it goes; the release too.
  REQUIRE(f.system.event(platform::MouseMoved{.position = {500.0f, 500.0f}, .delta = {450.0f, 450.0f}}));
  REQUIRE(f.release({60.0f, 60.0f}));
  REQUIRE(f.kinds() == std::vector{gui::GuiEventKind::Press, gui::GuiEventKind::Release, gui::GuiEventKind::Click});
  REQUIRE_FALSE(f.system.pressed(play.id()));
  f.system.endFrame();
  REQUIRE(f.system.events().empty());
  // Nothing is held now: motion and a stray release belong to the game.
  REQUIRE_FALSE(f.system.event(platform::MouseMoved{.position = {5.0f, 5.0f}, .delta = {1.0f, 1.0f}}));
  REQUIRE_FALSE(f.release({50.0f, 50.0f}));

  // Released outside: press and release, no click, and the release is still the UI's.
  REQUIRE(f.press({50.0f, 50.0f}));
  REQUIRE(f.release({900.0f, 900.0f}));
  REQUIRE(f.kinds() == std::vector{gui::GuiEventKind::Press, gui::GuiEventKind::Release});
  f.system.endFrame();

  // A press that starts away from the button is the game's, even when it ends over it.
  REQUIRE_FALSE(f.press({900.0f, 900.0f}));
  REQUIRE_FALSE(f.release({50.0f, 50.0f}));
  REQUIRE(f.system.events().empty());

  // Only the left button makes events; another is consumed over a button all the same.
  REQUIRE(f.system.event(
      platform::MouseButtonPressed{.button = platform::MouseButton::Right, .position = {50, 50}, .clicks = 1}));
  REQUIRE(f.system.event(platform::MouseButtonReleased{.button = platform::MouseButton::Right, .position = {50, 50}}));
  REQUIRE(f.system.events().empty());
}

TEST_CASE("a finger and the mouse SDL synthesises from it are one press", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity play = f.button("Play", canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
  f.update();

  REQUIRE(f.system.event(platform::TouchDown{.id = 7, .position = {50.0f, 50.0f}}));
  REQUIRE(f.press({50.0f, 50.0f}));
  REQUIRE(f.system.event(platform::TouchMotion{.id = 7, .position = {52.0f, 50.0f}, .delta = {2.0f, 0.0f}}));
  REQUIRE(f.system.event(platform::TouchUp{.id = 7, .position = {52.0f, 50.0f}}));
  REQUIRE(f.system.pressed(play.id())); // the mouse still holds it
  REQUIRE(f.release({52.0f, 50.0f}));
  REQUIRE(f.kinds() == std::vector{gui::GuiEventKind::Press, gui::GuiEventKind::Release, gui::GuiEventKind::Click});
  // A second finger elsewhere is the game's.
  REQUIRE_FALSE(f.system.event(platform::TouchDown{.id = 8, .position = {900.0f, 900.0f}}));
}

TEST_CASE("a consumed touch never reaches the input state", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  f.button("Play", canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
  f.update();
  platform::InputState input;
  const auto offer = [&](const platform::Event &event) {
    if (!f.system.event(event)) {
      input.handle(event);
    }
  };

  offer(platform::TouchDown{.id = 1, .position = {50.0f, 50.0f}});   // on the button
  offer(platform::TouchDown{.id = 2, .position = {800.0f, 800.0f}}); // in the world
  REQUIRE(input.touches().size() == 1);
  REQUIRE(input.touches()[0].id == 2);
  offer(platform::TouchMotion{.id = 1, .position = {60.0f, 60.0f}, .delta = {10.0f, 10.0f}});
  offer(platform::TouchUp{.id = 1, .position = {60.0f, 60.0f}});
  REQUIRE(input.touches().size() == 1);
  offer(platform::TouchUp{.id = 2, .position = {800.0f, 800.0f}});
  REQUIRE(input.touches().empty());
}

TEST_CASE("pointer positions are mapped from the window to the target", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  f.button("Play", canvas, {0.0f, 0.0f}, {200.0f, 200.0f});
  // A 2x display: the window is 960 x 540 points, the target 1920 x 1080 pixels.
  f.system.update({1920.0f, 1080.0f}, {960.0f, 540.0f}, {{0, 0}, {960, 540}});
  REQUIRE(f.press({90.0f, 90.0f})); // 180, 180 in the target
  REQUIRE(f.release({90.0f, 90.0f}));
  REQUIRE_FALSE(f.press({110.0f, 110.0f})); // 220, 220
}

TEST_CASE("losing the window's focus releases a held button without a click", "[gui][input]") {
  Fixture f;
  const flecs::entity canvas = f.canvas();
  f.button("Play", canvas, {0.0f, 0.0f}, {100.0f, 100.0f});
  f.update();
  REQUIRE(f.press({50.0f, 50.0f}));
  REQUIRE_FALSE(f.system.event(platform::WindowFocusChanged{.focused = false}));
  REQUIRE(f.kinds() == std::vector{gui::GuiEventKind::Press, gui::GuiEventKind::Release});
  REQUIRE_FALSE(f.release({50.0f, 50.0f})); // nothing is held any more
}

TEST_CASE("every component round-trips through the scene file", "[gui][scene]") {
  Fixture f;
  const flecs::entity canvas =
      f.canvas("Canvas", {.referenceSize = {1280.0f, 720.0f}, .match = 0.25f, .safeArea = false, .order = 3});
  const flecs::entity panel = f.world.createEntity("Panel", canvas);
  panel.set<gui::GuiRect>({.anchorMin = {0.1f, 0.2f},
                           .anchorMax = {0.9f, 0.8f},
                           .pivot = {0.0f, 1.0f},
                           .offsetMin = {-1.0f, -2.0f},
                           .offsetMax = {3.0f, 4.0f}});
  panel.set<gui::GuiImage>({.texture = core::Uuid::generate(),
                            .tint = {0.1f, 0.2f, 0.3f, 0.4f},
                            .slice = {1.0f, 2.0f, 3.0f, 4.0f},
                            .blocksInput = true});
  panel.set<gui::GuiText>({.font = core::Uuid::generate(),
                           .text = "Play \"now\"",
                           .size = 24.0f,
                           .color = {1.0f, 0.5f, 0.25f, 1.0f},
                           .horizontal = gui::GuiAlignment::Center,
                           .vertical = gui::GuiAlignment::End,
                           .wrap = true});
  panel.set<gui::GuiButton>({.interactable = false,
                             .normal = {0.1f, 0.1f, 0.1f, 1.0f},
                             .pressed = {0.2f, 0.2f, 0.2f, 1.0f},
                             .disabled = {0.3f, 0.3f, 0.3f, 1.0f}});
  panel.set<gui::GuiLayout>({.axis = gui::GuiAxis::Row,
                             .spacing = 12.0f,
                             .padding = {1.0f, 2.0f, 3.0f, 4.0f},
                             .alignment = gui::GuiAlignment::End});

  const nlohmann::json saved = world::saveScene(f.world);
  REQUIRE(saved["version"] == 3);

  Fixture other;
  REQUIRE(world::loadScene(other.world, saved).has_value());
  REQUIRE(world::saveScene(other.world) == saved);
  const flecs::entity loaded = other.world.findByPath(other.world.roots().front(), "Panel");
  REQUIRE(loaded.is_valid());
  const gui::GuiText &text = loaded.get<gui::GuiText>();
  REQUIRE(text.text == "Play \"now\"");
  REQUIRE(text.horizontal == gui::GuiAlignment::Center);
  REQUIRE(loaded.get<gui::GuiImage>().texture == panel.get<gui::GuiImage>().texture);
  REQUIRE(loaded.get<gui::GuiLayout>().axis == gui::GuiAxis::Row);
  REQUIRE(other.world.roots().front().get<gui::GuiCanvas>().order == 3);
}

TEST_CASE("an image draws one quad in its rectangle and tint, in draw order", "[gui][draw]") {
  DrawFixture f;
  const flecs::entity canvas = f.canvas();
  f.image("Back", canvas, {10.0f, 20.0f}, {110.0f, 70.0f}, {.tint = {1.0f, 0.5f, 0.25f, 1.0f}});
  f.image("Front", canvas, {50.0f, 50.0f}, {60.0f, 60.0f}, {.tint = {0.0f, 1.0f, 0.0f, 0.5f}});
  const flecs::entity clear = f.image("Clear", canvas, {0.0f, 0.0f}, {5.0f, 5.0f}, {.tint = {1.0f, 1.0f, 1.0f, 0.0f}});
  clear.set<gui::GuiText>({}); // a label draws nothing yet
  f.element("Empty", canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {9.0f, 9.0f});
  f.build();

  const std::span<const renderer::OverlayQuad> quads = f.system.quads();
  REQUIRE(quads.size() == 2); // the transparent one and the element without an image are skipped
  REQUIRE(quads[0].min == glm::vec2{10.0f, 20.0f});
  REQUIRE(quads[0].max == glm::vec2{110.0f, 70.0f});
  REQUIRE(quads[0].color == glm::vec4{1.0f, 0.5f, 0.25f, 1.0f});
  REQUIRE_FALSE(quads[0].texture.isValid());
  REQUIRE(quads[1].min == glm::vec2{50.0f, 50.0f});
  REQUIRE(quads[1].color.a == Approx(0.5f));
}

TEST_CASE("a button tints its image by state", "[gui][draw]") {
  DrawFixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity play =
      f.image("Play", canvas, {0.0f, 0.0f}, {100.0f, 100.0f}, {.tint = {1.0f, 1.0f, 1.0f, 1.0f}});
  play.set<gui::GuiButton>({.interactable = true,
                            .normal = {1.0f, 1.0f, 1.0f, 1.0f},
                            .pressed = {0.5f, 0.5f, 0.5f, 1.0f},
                            .disabled = {0.25f, 0.25f, 0.25f, 0.5f}});
  f.build();
  REQUIRE(f.system.quads()[0].color == glm::vec4{1.0f});

  REQUIRE(f.press({50.0f, 50.0f}));
  f.build();
  REQUIRE(f.system.quads()[0].color == glm::vec4{0.5f, 0.5f, 0.5f, 1.0f});
  REQUIRE(f.release({50.0f, 50.0f}));

  play.get_mut<gui::GuiButton>().interactable = false;
  f.build();
  REQUIRE(f.system.quads()[0].color == glm::vec4{0.25f, 0.25f, 0.25f, 0.5f});

  // The button's tint multiplies the image's own.
  play.get_mut<gui::GuiImage>().tint = {1.0f, 0.5f, 1.0f, 1.0f};
  play.get_mut<gui::GuiButton>().interactable = true;
  f.build();
  REQUIRE(f.system.quads()[0].color == glm::vec4{1.0f, 0.5f, 1.0f, 1.0f});
}

TEST_CASE("a disabled subtree draws nothing, and an image still loading waits", "[gui][draw]") {
  DrawFixture f;
  const flecs::entity canvas = f.canvas();
  const flecs::entity panel = f.image("Panel", canvas, {0.0f, 0.0f}, {100.0f, 100.0f}, {});
  f.image("Inside", panel, {0.0f, 0.0f}, {10.0f, 10.0f}, {});
  f.image("Missing", canvas, {0.0f, 0.0f}, {10.0f, 10.0f}, {.texture = core::Uuid::generate()});
  f.build();
  REQUIRE(f.system.quads().size() == 2); // the unknown texture is skipped, not drawn white

  panel.add<world::Disabled>();
  f.build();
  REQUIRE(f.system.quads().empty());
}

TEST_CASE("the pass is declared only when something draws", "[gui][draw]") {
  DrawFixture f;
  renderer::RenderGraph graph{*f.device};
  renderer::RenderTarget target{*f.device, "game"};
  target.resize({64, 64});
  const auto frame = [&] {
    rhi::ICommandList &commands = f.device->beginFrame();
    graph.reset();
    f.system.addPass(f.renderer, f.assets, graph, graph.importImage(target.color()));
    graph.execute(commands);
    f.device->endFrame();
  };
  const auto declared = [&] {
    return std::ranges::any_of(graph.statistics().passes, [](const auto &pass) { return pass.name == "overlay"; });
  };

  f.update({64.0f, 64.0f});
  frame();
  REQUIRE_FALSE(declared()); // no canvas
  const flecs::entity canvas = f.canvas();
  f.image("Box", canvas, {0.0f, 0.0f}, {10.0f, 10.0f}, {});
  f.update({64.0f, 64.0f});
  frame();
  REQUIRE(declared());
}

TEST_CASE("a nine-slice keeps its corners at the border size and stretches the rest", "[gui][draw]") {
  DrawFixture f;
  gui::GuiElement element;
  element.rect = {{100.0f, 200.0f}, {200.0f, 260.0f}}; // 100 x 60
  element.scale = 2.0f;
  renderer::TextureData pixels;
  pixels.size = {16, 16};
  pixels.data.assign(16 * 16 * 4, std::byte{255});
  const renderer::TextureHandle texture = f.renderer.createTexture(pixels, "slice");
  // Left 4, top 2, right 4, bottom 2 texels of a 16 x 16 texture, at twice the size.
  const gui::GuiImage image{.texture = {}, .tint = {}, .slice = {4.0f, 2.0f, 4.0f, 2.0f}, .blocksInput = false};
  std::vector<renderer::OverlayQuad> quads;
  gui::appendImage(quads, element, image, {1.0f, 1.0f, 1.0f, 1.0f}, texture, {16, 16});
  REQUIRE(quads.size() == 9);
  // Row by row. The top-left corner: 8 x 4 pixels, the texture's top-left 4 x 2 texels.
  REQUIRE(quads[0].min == glm::vec2{100.0f, 200.0f});
  REQUIRE(quads[0].max == glm::vec2{108.0f, 204.0f});
  REQUIRE(quads[0].uvMin == glm::vec2{0.0f, 0.0f});
  REQUIRE(quads[0].uvMax == glm::vec2{0.25f, 0.125f});
  // The middle of the top row stretches between the corners.
  REQUIRE(quads[1].min == glm::vec2{108.0f, 200.0f});
  REQUIRE(quads[1].max == glm::vec2{192.0f, 204.0f});
  REQUIRE(quads[1].uvMin == glm::vec2{0.25f, 0.0f});
  REQUIRE(quads[1].uvMax == glm::vec2{0.75f, 0.125f});
  // The centre, and the bottom-right corner ending at the rectangle's corner.
  REQUIRE(quads[4].min == glm::vec2{108.0f, 204.0f});
  REQUIRE(quads[4].max == glm::vec2{192.0f, 256.0f});
  REQUIRE(quads[8].min == glm::vec2{192.0f, 256.0f});
  REQUIRE(quads[8].max == glm::vec2{200.0f, 260.0f});
  REQUIRE(quads[8].uvMax == glm::vec2{1.0f, 1.0f});

  // Borders wider than the rectangle shrink together, and the empty centre is not drawn.
  quads.clear();
  element.rect = {{0.0f, 0.0f}, {8.0f, 60.0f}};
  gui::appendImage(quads, element, image, {1.0f, 1.0f, 1.0f, 1.0f}, texture, {16, 16});
  REQUIRE(quads.size() == 6);
  REQUIRE(quads[0].max.x == Approx(4.0f));
  REQUIRE(quads[1].min.x == Approx(4.0f));

  // Without borders, or without a texture, one quad.
  quads.clear();
  gui::appendImage(quads, element, gui::GuiImage{}, {1.0f, 1.0f, 1.0f, 1.0f}, texture, {16, 16});
  gui::appendImage(quads, element, image, {1.0f, 1.0f, 1.0f, 1.0f}, renderer::TextureHandle{}, {0, 0});
  REQUIRE(quads.size() == 2);
  f.renderer.destroyTexture(texture);
}
