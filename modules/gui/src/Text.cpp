#include "Text.h"

#include <algorithm>
#include <limits>

namespace sonnet::gui {

namespace {

// One line as laid out in em: each visible glyph with its pen x, and the width without the spaces
// at its end, which alignment ignores.
struct Line {
  std::vector<std::pair<std::uint32_t, float>> glyphs;
  float width{0.0f};
};

struct Item {
  std::uint32_t glyph;
  bool space;
  float advance;
};

class Builder {
public:
  Builder(const FontFace &font, float maxWidth) : m_font(font), m_maxWidth(maxWidth) {
    m_lines.emplace_back();
  }

  void newline() {
    m_lines.emplace_back();
    m_pen = 0.0f;
    m_previous = 0;
    m_hasPrevious = false;
    m_softBreak = false;
  }

  // A word is the run of non-space characters between two spaces.
  void word(const std::vector<Item> &items) {
    const float width = measure(items);
    if (m_maxWidth < std::numeric_limits<float>::infinity() && lineHasGlyphs() && m_pen + width > m_maxWidth) {
      softBreak();
    }
    for (const Item &item : items) {
      if (m_maxWidth < std::numeric_limits<float>::infinity() && lineHasGlyphs() &&
          m_pen + advanceOf(item) > m_maxWidth) {
        softBreak(); // a word longer than the line
      }
      put(item);
    }
  }

  void space(const Item &item) {
    if (m_softBreak && !lineHasGlyphs()) {
      return; // spaces that follow a soft break open no line
    }
    put(item);
  }

  [[nodiscard]] std::vector<Line> finish() {
    return std::move(m_lines);
  }

private:
  [[nodiscard]] bool lineHasGlyphs() const {
    return m_hasPrevious;
  }
  [[nodiscard]] float advanceOf(const Item &item) const {
    return item.advance + (m_hasPrevious ? m_font.kern(m_previous, item.glyph) : 0.0f);
  }
  [[nodiscard]] float measure(const std::vector<Item> &items) const {
    float width = 0.0f;
    std::uint32_t previous = m_previous;
    bool has = m_hasPrevious;
    for (const Item &item : items) {
      width += item.advance + (has ? m_font.kern(previous, item.glyph) : 0.0f);
      previous = item.glyph;
      has = true;
    }
    return width;
  }
  void softBreak() {
    newline();
    m_softBreak = true;
  }
  void put(const Item &item) {
    m_pen += advanceOf(item);
    Line &line = m_lines.back();
    if (!item.space) {
      line.glyphs.emplace_back(item.glyph, m_pen - item.advance);
      line.width = m_pen;
    }
    m_previous = item.glyph;
    m_hasPrevious = true;
  }

  const FontFace &m_font;
  float m_maxWidth;
  std::vector<Line> m_lines;
  float m_pen{0.0f};
  std::uint32_t m_previous{0};
  bool m_hasPrevious{false};
  bool m_softBreak{false};
};

[[nodiscard]] float aligned(GuiAlignment alignment, float from, float room, float used) {
  switch (alignment) {
  case GuiAlignment::Start:
    return from;
  case GuiAlignment::Center:
    return from + (room - used) * 0.5f;
  case GuiAlignment::End:
    return from + room - used;
  }
  return from;
}

} // namespace

TextLayout layoutText(const FontFace &font, std::string_view text, float pixelSize, const PixelRect &rect,
                      GuiAlignment horizontal, GuiAlignment vertical, bool wrap) {
  const float maxWidth = wrap && pixelSize > 0.0f ? rect.size().x / pixelSize : std::numeric_limits<float>::infinity();
  Builder builder{font, maxWidth};
  std::vector<Item> word;
  const auto flush = [&] {
    if (!word.empty()) {
      builder.word(word);
      word.clear();
    }
  };
  for (const char32_t codepoint : decodeUtf8(text)) {
    if (codepoint == U'\n') {
      flush();
      builder.newline();
      continue;
    }
    const std::uint32_t glyph = font.glyphIndex(codepoint);
    const Item item{glyph, codepoint == U' ', font.advance(glyph)};
    if (item.space) {
      flush();
      builder.space(item);
    } else {
      word.push_back(item);
    }
  }
  flush();
  const std::vector<Line> lines = builder.finish();

  const float pitch = font.ascent() - font.descent() + font.lineGap();
  const float block = static_cast<float>(lines.size() - 1) * pitch + font.ascent() - font.descent();
  TextLayout out;
  out.size = {0.0f, block * pixelSize};
  const float top = aligned(vertical, rect.min.y, rect.size().y, block * pixelSize);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const Line &line = lines[index];
    out.size.x = std::max(out.size.x, line.width * pixelSize);
    const float left = aligned(horizontal, rect.min.x, rect.size().x, line.width * pixelSize);
    const float baseline = top + (font.ascent() + static_cast<float>(index) * pitch) * pixelSize;
    for (const auto &[glyph, x] : line.glyphs) {
      out.glyphs.push_back({glyph, {left + x * pixelSize, baseline}});
    }
  }
  return out;
}

} // namespace sonnet::gui
