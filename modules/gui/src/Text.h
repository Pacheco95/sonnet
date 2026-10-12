#pragma once

#include "Font.h"

#include <sonnet/gui/Components.h>
#include <sonnet/gui/Layout.h>

#include <string_view>
#include <vector>

namespace sonnet::gui {

// A glyph placed by layoutText: where its pen sits, on the baseline, in target pixels.
struct PlacedGlyph {
  std::uint32_t glyph{0};
  glm::vec2 pen{0.0f, 0.0f};
};

// What layoutText measured: the placed glyphs of the visible characters (a space places none) and
// the size of the text block.
struct TextLayout {
  std::vector<PlacedGlyph> glyphs;
  glm::vec2 size{0.0f, 0.0f};
};

// Lays `text` out in `rect` at `pixelSize` pixels to the em, left to right, with each glyph's
// advance and the font's kerning and no shaping (docs/gui.md, "Text"). `\n` ends a line. With
// `wrap`, a line also ends before a word that would pass the rectangle's right edge, and a word
// longer than the rectangle breaks between characters; the spaces at a break are dropped. The
// lines follow `horizontal` inside the rectangle and the block follows `vertical`. Pure: the same
// arguments give the same glyphs, with no GPU.
[[nodiscard]] TextLayout layoutText(const FontFace &font, std::string_view text, float pixelSize, const PixelRect &rect,
                                    GuiAlignment horizontal, GuiAlignment vertical, bool wrap);

} // namespace sonnet::gui
