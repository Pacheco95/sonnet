#include "Atlas.h"
#include "Draw.h"
#include "FontCache.h"
#include "GuiTestSupport.h"
#include "Text.h"

#include <sonnet/gui/GuiSystem.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/File.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>
#include <sonnet/world/World.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>

using namespace sonnet;
using Catch::Approx;

namespace {

// TestFont.ttf: 1000 units to the em; ascent 800, descent -200, line gap 100; advances A and V 600,
// i 300, space 250, notdef 500; kern A-V -100 and V-A -50. Glyphs in cmap order: notdef, space, A, V, i.
const gui::PixelRect Box{{100.0f, 200.0f}, {500.0f, 400.0f}};

[[nodiscard]] gui::FontFace testFace() {
  const platform::Platform platform{{.headless = true}};
  auto face = gui::FontFace::parse(gui::test::testFontBytes(platform.basePath()));
  REQUIRE(face.has_value());
  return std::move(*face);
}

[[nodiscard]] gui::TextLayout lay(const gui::FontFace &face, const std::string &text, float size = 100.0f,
                                  gui::PixelRect rect = Box, bool wrap = false,
                                  gui::GuiAlignment horizontal = gui::GuiAlignment::Start,
                                  gui::GuiAlignment vertical = gui::GuiAlignment::Start) {
  return gui::layoutText(face, text, size, rect, horizontal, vertical, wrap);
}

} // namespace

TEST_CASE("UTF-8 decodes to codepoints and a malformed byte becomes the replacement character", "[gui][text]") {
  using Codepoints = std::vector<char32_t>;
  REQUIRE(gui::decodeUtf8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80") == Codepoints{U'A', U'é', U'€', U'\U0001F600'});
  REQUIRE(gui::decodeUtf8("\xFF") == Codepoints{U'�'});
  REQUIRE(gui::decodeUtf8("A\xE2\x82") == Codepoints{U'A', U'�', U'�'}); // cut off
  REQUIRE(gui::decodeUtf8("\xC3(") == Codepoints{U'�', U'('});           // not a continuation
  REQUIRE(gui::decodeUtf8("").empty());
}

TEST_CASE("a font reports the metrics it was built with", "[gui][text]") {
  const gui::FontFace face = testFace();
  const std::uint32_t a = face.glyphIndex(U'A');
  const std::uint32_t v = face.glyphIndex(U'V');
  const std::uint32_t i = face.glyphIndex(U'i');
  REQUIRE(a == 2);
  REQUIRE(v == 3);
  REQUIRE(i == 4);
  REQUIRE(face.glyphIndex(U' ') == 1);
  REQUIRE(face.glyphIndex(U'Z') == 0); // the font has no Z: its notdef

  REQUIRE(face.advance(a) == Approx(0.6f));
  REQUIRE(face.advance(i) == Approx(0.3f));
  REQUIRE(face.advance(1) == Approx(0.25f));
  REQUIRE(face.advance(0) == Approx(0.5f));
  REQUIRE(face.leftBearing(a) == Approx(0.05f));
  REQUIRE(face.leftBearing(i) == Approx(0.1f));
  REQUIRE(face.kern(a, v) == Approx(-0.1f));
  REQUIRE(face.kern(v, a) == Approx(-0.05f));
  REQUIRE(face.kern(a, a) == Approx(0.0f));
  REQUIRE(face.ascent() == Approx(0.8f));
  REQUIRE(face.descent() == Approx(-0.2f));
  REQUIRE(face.lineGap() == Approx(0.1f));
}

TEST_CASE("a glyph is rasterised at 48 pixels with a 6 pixel border around its outline", "[gui][text]") {
  const gui::FontFace face = testFace();
  // A is the box 50..550 by 0..700 units: 2.4..26.4 by 0..33.6 pixels at 48 to the em. Its bitmap
  // reaches the pixels around that, floor and ceil, and the border beyond: x from 2 - 6 to 27 + 6
  // and y from -34 - 6 to 0 + 6, with y down from the baseline.
  const gui::SdfBitmap a = face.rasterise(face.glyphIndex(U'A'));
  REQUIRE(a.size == glm::ivec2{37, 46});
  REQUIRE(a.offset == glm::vec2{-4.0f, -40.0f});
  REQUIRE(a.texels.size() == std::size_t{37} * 46);
  const auto at = [&](int x, int y) { return a.texels[static_cast<std::size_t>(y * a.size.x + x)]; };
  REQUIRE(at(0, 0) == 0);    // the corner is farther than the border from the outline
  REQUIRE(at(18, 20) > 200); // the middle of the box is deep inside
  REQUIRE(at(18, 45) < 64);  // five pixels below the baseline, outside the box and nearly out of the field
  REQUIRE(face.rasterise(face.glyphIndex(U' ')).texels.empty()); // a space has no outline
}

TEST_CASE("one glyph sits at the pen, the next after its advance and the kern", "[gui][text]") {
  const gui::FontFace face = testFace();
  const gui::TextLayout one = lay(face, "A");
  REQUIRE(one.glyphs.size() == 1);
  REQUIRE(one.glyphs[0].glyph == 2);
  REQUIRE(one.glyphs[0].pen.x == Approx(100.0f));
  REQUIRE(one.glyphs[0].pen.y == Approx(200.0f + 80.0f)); // the top plus the ascent
  REQUIRE(one.size.x == Approx(60.0f));
  REQUIRE(one.size.y == Approx(100.0f));

  REQUIRE(lay(face, "AA").glyphs[1].pen.x == Approx(160.0f));
  REQUIRE(lay(face, "AV").glyphs[1].pen.x == Approx(150.0f)); // 0.6 - 0.1
  REQUIRE(lay(face, "VA").glyphs[1].pen.x == Approx(155.0f)); // 0.6 - 0.05
  REQUIRE(lay(face, "Ai").glyphs[1].pen.x == Approx(160.0f));
  const gui::TextLayout spaced = lay(face, "A A");
  REQUIRE(spaced.glyphs.size() == 2); // a space places nothing
  REQUIRE(spaced.glyphs[1].pen.x == Approx(100.0f + 60.0f + 25.0f));
  REQUIRE(lay(face, "AA", 50.0f).glyphs[1].pen.x == Approx(130.0f)); // the size scales the advance
}

TEST_CASE("text aligns in its rectangle by line and block, in all nine ways", "[gui][text]") {
  const gui::FontFace face = testFace();
  // "AA" is 120 pixels wide and one line is 100 tall at size 100, in a rectangle 400 by 200 at (100, 200).
  using gui::GuiAlignment;
  const std::pair<GuiAlignment, float> columns[] = {
      {GuiAlignment::Start, 100.0f}, {GuiAlignment::Center, 240.0f}, {GuiAlignment::End, 380.0f}};
  const std::pair<GuiAlignment, float> rows[] = {
      {GuiAlignment::Start, 280.0f}, {GuiAlignment::Center, 330.0f}, {GuiAlignment::End, 380.0f}}; // baselines
  for (const auto &[horizontal, left] : columns) {
    for (const auto &[vertical, baseline] : rows) {
      CAPTURE(static_cast<int>(horizontal), static_cast<int>(vertical));
      const gui::TextLayout layout = lay(face, "AA", 100.0f, Box, false, horizontal, vertical);
      REQUIRE(layout.glyphs.size() == 2);
      REQUIRE(layout.glyphs[0].pen.x == Approx(left));
      REQUIRE(layout.glyphs[1].pen.x == Approx(left + 60.0f));
      REQUIRE(layout.glyphs[0].pen.y == Approx(baseline));
    }
  }
}

TEST_CASE("each line aligns on its own and a block of lines on its height", "[gui][text]") {
  const gui::FontFace face = testFace();
  // Two lines: the line pitch is ascent - descent + gap = 1.1 em, the block 1.1 + 1.0 em.
  const gui::TextLayout layout =
      lay(face, "AAA\nA", 100.0f, Box, false, gui::GuiAlignment::Center, gui::GuiAlignment::Center);
  REQUIRE(layout.glyphs.size() == 4);
  REQUIRE(layout.size.y == Approx(210.0f));
  // Top = 200 + (200 - 210) / 2 = 195; baselines at 195 + 80 and 195 + 80 + 110.
  REQUIRE(layout.glyphs[0].pen.y == Approx(275.0f));
  REQUIRE(layout.glyphs[3].pen.y == Approx(385.0f));
  REQUIRE(layout.glyphs[0].pen.x == Approx(100.0f + (400.0f - 180.0f) / 2.0f)); // 180 wide
  REQUIRE(layout.glyphs[3].pen.x == Approx(100.0f + (400.0f - 60.0f) / 2.0f));  // 60 wide
}

TEST_CASE("a line break in the text starts a line, an empty one too", "[gui][text]") {
  const gui::FontFace face = testFace();
  const gui::TextLayout layout = lay(face, "A\n\nA");
  REQUIRE(layout.glyphs.size() == 2);
  REQUIRE(layout.glyphs[0].pen.y == Approx(280.0f));
  REQUIRE(layout.glyphs[1].pen.y == Approx(280.0f + 220.0f));
  REQUIRE(layout.glyphs[1].pen.x == Approx(100.0f));
}

TEST_CASE("wrapping breaks at spaces and drops the spaces at the break", "[gui][text]") {
  const gui::FontFace face = testFace();
  // "AA" is 1.2 em; with a space 2.65 em for two words. A line of 250 pixels at size 100 is 2.5 em.
  const gui::PixelRect narrow{{0.0f, 0.0f}, {250.0f, 400.0f}};
  const gui::TextLayout wrapped = lay(face, "AA AA AA", 100.0f, narrow, true);
  REQUIRE(wrapped.glyphs.size() == 6);
  REQUIRE(wrapped.glyphs[0].pen.y == Approx(80.0f));
  REQUIRE(wrapped.glyphs[2].pen.y == Approx(190.0f));
  REQUIRE(wrapped.glyphs[4].pen.y == Approx(300.0f));
  REQUIRE(wrapped.glyphs[2].pen.x == Approx(0.0f)); // no leading space on the new line

  const gui::PixelRect wide{{0.0f, 0.0f}, {300.0f, 400.0f}};
  const gui::TextLayout two = lay(face, "AA AA AA", 100.0f, wide, true);
  REQUIRE(two.glyphs[3].pen.y == Approx(80.0f)); // 2.65 em fits in 3
  REQUIRE(two.glyphs[4].pen.y == Approx(190.0f));

  // Without wrap the text keeps to one line however long.
  const gui::TextLayout single = lay(face, "AA AA AA", 100.0f, narrow, false);
  REQUIRE(single.glyphs[5].pen.y == Approx(80.0f));
  // A line that fits exactly stays.
  REQUIRE(lay(face, "AA", 100.0f, {{0.0f, 0.0f}, {120.0f, 100.0f}}, true).glyphs[1].pen.y == Approx(80.0f));
}

TEST_CASE("a word longer than the line breaks between characters", "[gui][text]") {
  const gui::FontFace face = testFace();
  const gui::PixelRect line{{0.0f, 0.0f}, {150.0f, 400.0f}};
  const gui::TextLayout layout = lay(face, "AAAAAA", 100.0f, line, true);
  REQUIRE(layout.glyphs.size() == 6);
  REQUIRE(layout.glyphs[1].pen.y == Approx(80.0f)); // two fit in 1.5 em
  REQUIRE(layout.glyphs[2].pen.y == Approx(190.0f));
  REQUIRE(layout.glyphs[4].pen.y == Approx(300.0f));
  REQUIRE(layout.glyphs[2].pen.x == Approx(0.0f));

  // A word after a short one that does not fit the rest of the line starts a line, then breaks.
  const gui::TextLayout after = lay(face, "A AAAAAA", 100.0f, line, true);
  REQUIRE(after.glyphs[1].pen.y == Approx(190.0f));

  // A line narrower than one character still takes one per line, and ends.
  const gui::TextLayout thin = lay(face, "AAA", 100.0f, {{0.0f, 0.0f}, {20.0f, 400.0f}}, true);
  REQUIRE(thin.glyphs.size() == 3);
  REQUIRE(thin.glyphs[2].pen.y == Approx(80.0f + 220.0f));
}

TEST_CASE("a character the font lacks draws its notdef, and odd text does not crash", "[gui][text]") {
  const gui::FontFace face = testFace();
  const gui::TextLayout layout = lay(face, "ZA");
  REQUIRE(layout.glyphs.size() == 2);
  REQUIRE(layout.glyphs[0].glyph == 0);
  REQUIRE(layout.glyphs[1].pen.x == Approx(100.0f + 50.0f)); // notdef advances 0.5 em
  REQUIRE(!face.rasterise(0).texels.empty());                // and has an outline to draw

  REQUIRE(lay(face, "").glyphs.empty());
  REQUIRE(lay(face, "   ").glyphs.empty());
  REQUIRE(lay(face, "\xFF\xFE").glyphs.size() == 2);
  REQUIRE(lay(face, "A", 0.0f, Box, true).glyphs.size() == 1);
  REQUIRE(lay(face, "AA", 100.0f, {{0.0f, 0.0f}, {0.0f, 0.0f}}, true).glyphs.size() == 2);
}

TEST_CASE("the atlas packs glyphs without overlap and finds them again", "[gui][atlas]") {
  gui::GlyphAtlas atlas;
  std::mt19937 random{7};
  std::uniform_int_distribution<int> side{4, 60};
  std::vector<gui::AtlasGlyph> placed;
  for (std::uint32_t glyph = 1; glyph <= 400; ++glyph) {
    gui::SdfBitmap bitmap;
    bitmap.size = {side(random), side(random)};
    bitmap.texels.assign(static_cast<std::size_t>(bitmap.size.x * bitmap.size.y), static_cast<std::uint8_t>(glyph));
    placed.push_back(atlas.insert(glyph, bitmap));
  }
  for (std::size_t i = 0; i < placed.size(); ++i) {
    const gui::AtlasGlyph &a = placed[i];
    REQUIRE(a.position.x >= 0);
    REQUIRE(a.position.y >= 0);
    REQUIRE(a.position.x + a.size.x <= gui::AtlasPageSize);
    REQUIRE(a.position.y + a.size.y <= gui::AtlasPageSize);
    for (std::size_t j = i + 1; j < placed.size(); ++j) {
      const gui::AtlasGlyph &b = placed[j];
      const bool apart = a.page != b.page || a.position.x + a.size.x <= b.position.x ||
                         b.position.x + b.size.x <= a.position.x || a.position.y + a.size.y <= b.position.y ||
                         b.position.y + b.size.y <= a.position.y;
      REQUIRE(apart);
    }
    // The texels are the bitmap's, in place.
    const gui::GlyphAtlas::Page &page = atlas.pages()[a.page];
    REQUIRE(page.texels[static_cast<std::size_t>(a.position.y * gui::AtlasPageSize + a.position.x)] ==
            static_cast<std::uint8_t>(i + 1));
    REQUIRE(page.dirty);
  }
}

TEST_CASE("a glyph is placed once, and a full page opens the next", "[gui][atlas]") {
  gui::GlyphAtlas atlas;
  gui::SdfBitmap big;
  big.size = {500, 500};
  big.texels.assign(std::size_t{500} * 500, 9);
  const gui::AtlasGlyph first = atlas.insert(1, big);
  REQUIRE(atlas.find(1) != nullptr);
  REQUIRE(atlas.insert(1, big).position == first.position); // found, not packed again
  REQUIRE(atlas.pages().size() == 1);
  // 501 by 501 cells: four fill a page of 1024.
  for (std::uint32_t glyph = 2; glyph <= 4; ++glyph) {
    REQUIRE(atlas.insert(glyph, big).page == 0);
  }
  REQUIRE(atlas.pages().size() == 1);
  REQUIRE(atlas.insert(5, big).page == 1);
  REQUIRE(atlas.pages().size() == 2);
  REQUIRE(atlas.find(6) == nullptr);

  // A space has no texels: it is known, and takes no room.
  const gui::AtlasGlyph &space = atlas.insert(6, {});
  REQUIRE(space.size == glm::ivec2{0, 0});
  REQUIRE(atlas.find(6) != nullptr);
  REQUIRE(atlas.pages().size() == 2);
}

namespace {

// A project with the test font in it, a renderer on the null device and the game UI over them.
struct FontFixture {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::NullDevice> device = rhi::createNullDevice();
  renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
  core::JobSystem jobs{{.workerCount = 1}};
  assets::AssetDatabase assets{renderer, jobs};
  world::World world;
  gui::GuiSystem system{world};
  std::filesystem::path root = std::filesystem::temp_directory_path() / "sonnet_gui_text";
  std::vector<std::string> roots{"assets"};
  core::Uuid font;

  // `preload` is the sidecar's list; the test font has four glyphs outside the notdef.
  explicit FontFixture(const char *preload = "[[32, 126]]") {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets");
    REQUIRE(core::writeFile(root / "assets" / "test.ttf", gui::test::testFontBytes(platform.basePath())).has_value());
    const core::Uuid uuid = core::Uuid::generate();
    REQUIRE(core::writeFile(root / "assets" / "test.ttf.meta", std::string{"{\"version\": 2, \"uuid\": \""} +
                                                                   uuid.toString() +
                                                                   "\", \"settings\": {\"preload\": " + preload + "}}")
                .has_value());
    assets.open(root, roots);
    font = uuid;
  }
  ~FontFixture() {
    std::filesystem::remove_all(root);
  }

  flecs::entity canvas() {
    const flecs::entity entity = world.createEntity("Canvas");
    entity.set<gui::GuiCanvas>({});
    return entity;
  }
  flecs::entity label(const char *name, flecs::entity parent, std::string text, bool wrap = false) {
    const flecs::entity entity = world.createEntity(name, parent);
    entity.set<gui::GuiRect>({.anchorMin = {0.0f, 0.0f},
                              .anchorMax = {0.0f, 0.0f},
                              .pivot = {0.5f, 0.5f},
                              .offsetMin = {100.0f, 100.0f},
                              .offsetMax = {900.0f, 400.0f}});
    entity.set<gui::GuiText>({.font = font, .text = std::move(text), .size = 100.0f, .wrap = wrap});
    return entity;
  }
  void build(glm::vec2 target = {1920.0f, 1080.0f}) {
    system.update(target, target, {{0, 0}, {static_cast<unsigned>(target.x), static_cast<unsigned>(target.y)}});
    system.buildQuads(renderer, assets);
  }
  [[nodiscard]] std::size_t glyphQuads() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        system.quads(), [](const renderer::OverlayQuad &quad) { return quad.kind == renderer::OverlayKind::Glyph; }));
  }
};

} // namespace

TEST_CASE("a font is an asset with a sidecar, preloaded by default over Basic Latin and Latin-1", "[gui][font]") {
  FontFixture fixture;
  const assets::FontSource *source = fixture.assets.font(fixture.font);
  REQUIRE(source != nullptr);
  REQUIRE(source->preload == std::vector<assets::GlyphRange>{{32, 126}});
  REQUIRE(assets::FontSettings{}.preload == std::vector<assets::GlyphRange>{{0x20, 0x7E}, {0xA0, 0xFF}});
  REQUIRE(assets::assetTypeFromString("Font") == assets::AssetType::Font);
}

TEST_CASE("text draws a quad a visible glyph, in the glyph's place, from the atlas page", "[gui][font][draw]") {
  FontFixture fixture;
  const flecs::entity canvas = fixture.canvas();
  fixture.label("Label", canvas, "AV");
  fixture.build();
  REQUIRE(fixture.system.quads().size() == 2);
  const renderer::OverlayQuad &a = fixture.system.quads()[0];
  REQUIRE(a.kind == renderer::OverlayKind::Glyph);
  REQUIRE(fixture.renderer.isValid(a.texture));
  REQUIRE(fixture.renderer.textureSize(a.texture) == glm::uvec2{gui::AtlasPageSize, gui::AtlasPageSize});
  REQUIRE(a.color == glm::vec4{1.0f});
  // The pen is at (100, 100 + 80); A's bitmap starts (-4, -40) from it at 48 px, drawn at 100: x2.0833.
  const float scale = 100.0f / 48.0f;
  REQUIRE(a.min.x == Approx(100.0f - 4.0f * scale));
  REQUIRE(a.min.y == Approx(180.0f - 40.0f * scale));
  REQUIRE(a.max.x - a.min.x == Approx(37.0f * scale));
  REQUIRE(a.max.y - a.min.y == Approx(46.0f * scale));
  const glm::vec2 texels = (a.uvMax - a.uvMin) * static_cast<float>(gui::AtlasPageSize);
  REQUIRE(texels.x == Approx(37.0f));
  REQUIRE(texels.y == Approx(46.0f));
  // V follows A by 0.6 - 0.1 em.
  REQUIRE(fixture.system.quads()[1].texture == a.texture);
}

TEST_CASE("images and text come in the draw order of their elements", "[gui][font][draw]") {
  FontFixture fixture;
  const flecs::entity canvas = fixture.canvas();
  const flecs::entity panel = fixture.world.createEntity("Panel", canvas);
  panel.set<gui::GuiRect>(
      {.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0, 0}, .offsetMax = {0, 0}});
  panel.set<gui::GuiImage>({});
  const flecs::entity label = fixture.label("Label", panel, "A");
  label.set<gui::GuiImage>({.tint = {0.0f, 0.0f, 0.0f, 1.0f}}); // an element with both: image first
  fixture.label("Second", panel, "A");
  fixture.build();
  using Kind = renderer::OverlayKind;
  std::vector<Kind> kinds;
  for (const renderer::OverlayQuad &quad : fixture.system.quads()) {
    kinds.push_back(quad.kind);
  }
  REQUIRE(kinds == std::vector<Kind>{Kind::Image, Kind::Image, Kind::Glyph, Kind::Glyph});
}

TEST_CASE("text without a font, with a missing one or with nothing to show draws nothing", "[gui][font][draw]") {
  FontFixture fixture;
  const flecs::entity canvas = fixture.canvas();
  const flecs::entity none = fixture.label("None", canvas, "A");
  none.get_mut<gui::GuiText>().font = {};
  const flecs::entity missing = fixture.label("Missing", canvas, "A");
  missing.get_mut<gui::GuiText>().font = core::Uuid::generate();
  fixture.label("Empty", canvas, "");
  const flecs::entity clear = fixture.label("Clear", canvas, "A");
  clear.get_mut<gui::GuiText>().color.a = 0.0f;
  fixture.label("Spaces", canvas, "   ");
  fixture.build();
  REQUIRE(fixture.system.quads().empty());
}

TEST_CASE("a changed text lays out again and a glyph seen first draws the same frame", "[gui][font][draw]") {
  FontFixture fixture("[]"); // nothing preloaded: every glyph is rasterised when it is first drawn
  const flecs::entity canvas = fixture.canvas();
  const flecs::entity label = fixture.label("Label", canvas, "A");
  fixture.build();
  REQUIRE(fixture.glyphQuads() == 1);

  label.get_mut<gui::GuiText>().text = "Aii";
  fixture.build();
  REQUIRE(fixture.glyphQuads() == 3);
  const renderer::OverlayQuad second = fixture.system.quads()[1];
  const renderer::OverlayQuad third = fixture.system.quads()[2];
  REQUIRE(third.min.x - second.min.x == Approx(0.3f * 100.0f)); // the same glyph, one advance on

  label.get_mut<gui::GuiText>().text = "V";
  fixture.build();
  REQUIRE(fixture.glyphQuads() == 1);
  REQUIRE(fixture.renderer.isValid(fixture.system.quads()[0].texture));
}

TEST_CASE("text wraps at the element's width and follows the canvas scale", "[gui][font][draw]") {
  FontFixture fixture;
  const flecs::entity canvas = fixture.canvas();
  fixture.label("Label", canvas, "AA AA AA AA AA AA", true);
  fixture.build({960.0f, 540.0f}); // half the reference size: the element is 400 px wide, the text 50 px
  const auto &quads = fixture.system.quads();
  REQUIRE(quads.size() == 12);
  // Six words of 1.2 em and five spaces make 8.45 em, 422 px: the last word goes to a second line.
  REQUIRE(quads[10].min.y - quads[0].min.y == Approx(1.1f * 50.0f));
  REQUIRE(quads[0].max.x - quads[0].min.x == Approx(37.0f * 50.0f / 48.0f));
}

TEST_CASE("an atlas page is uploaded again at most once a frame", "[gui][font][atlas]") {
  FontFixture fixture("[]");
  gui::FontCache cache;
  gui::LoadedFont *font = cache.get(fixture.font, fixture.assets);
  REQUIRE(font != nullptr);
  REQUIRE(cache.upload(fixture.renderer, 1) == 0); // nothing yet

  static_cast<void>(cache.glyph(*font, font->face.glyphIndex(U'A')));
  REQUIRE(cache.upload(fixture.renderer, 1) == 1);
  const renderer::TextureHandle first = font->pages[0].texture;
  REQUIRE(fixture.renderer.isValid(first));

  static_cast<void>(cache.glyph(*font, font->face.glyphIndex(U'V')));
  REQUIRE(cache.upload(fixture.renderer, 1) == 0); // the same frame: it waits
  REQUIRE(font->pages[0].texture == first);
  REQUIRE(cache.upload(fixture.renderer, 2) == 1); // the next one takes it
  REQUIRE(font->pages[0].texture != first);
  REQUIRE(!fixture.renderer.isValid(first));       // the old page was released
  REQUIRE(cache.upload(fixture.renderer, 3) == 0); // and nothing changed since
}

TEST_CASE("the preload rasterises the sidecar's ranges on the job system", "[gui][font][atlas]") {
  FontFixture fixture("[[65, 86]]"); // A to V: A and V are in the font, nothing between them
  gui::FontCache cache;
  gui::LoadedFont *font = cache.get(fixture.font, fixture.assets);
  REQUIRE(font != nullptr);
  REQUIRE(font->face.glyphIndex(U'A') != 0);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (font->preloaded && std::chrono::steady_clock::now() < deadline) {
    cache.collect(*font);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  REQUIRE(!font->preloaded);
  REQUIRE(font->atlas.find(font->face.glyphIndex(U'A')) != nullptr);
  REQUIRE(font->atlas.find(font->face.glyphIndex(U'V')) != nullptr);
  REQUIRE(font->atlas.find(font->face.glyphIndex(U'i')) == nullptr); // outside the range
  REQUIRE(cache.upload(fixture.renderer, 1) == 1);                   // one page for all of them
}

TEST_CASE("a font whose file changed is parsed again with an empty atlas", "[gui][font]") {
  FontFixture fixture("[]");
  gui::FontCache cache;
  gui::LoadedFont *font = cache.get(fixture.font, fixture.assets);
  REQUIRE(font != nullptr);
  const std::uint64_t revision = font->revision;
  static_cast<void>(cache.glyph(*font, font->face.glyphIndex(U'A')));
  REQUIRE(cache.get(fixture.font, fixture.assets) == font); // the same revision: the same font

  REQUIRE(fixture.assets.reimport(fixture.font).has_value());
  gui::LoadedFont *again = cache.get(fixture.font, fixture.assets);
  REQUIRE(again != nullptr);
  REQUIRE(again->revision > revision);
  REQUIRE(again->atlas.pages().empty());
}

TEST_CASE("bytes that are not a font draw nothing", "[gui][font]") {
  FontFixture fixture("[]");
  REQUIRE(core::writeFile(fixture.root / "assets" / "test.ttf", std::string_view{"not a font, but long enough"})
              .has_value());
  REQUIRE(fixture.assets.reimport(fixture.font).has_value());
  gui::FontCache cache;
  REQUIRE(cache.get(fixture.font, fixture.assets) == nullptr);
}
