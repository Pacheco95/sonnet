#pragma once

#include <sonnet/core/Math.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct stbtt_fontinfo;

namespace sonnet::gui {

// The size every glyph is rasterised at, and the border around it that holds the distance field's
// fall-off (ADR-0026). A glyph is drawn at any size by scaling its quad.
constexpr int SdfSize = 48;
constexpr int SdfBorder = 6;

// One glyph as a signed distance field, 128 on the outline: a byte per texel, rows top to bottom.
// `offset` is the bitmap's top-left from the pen at the baseline, in pixels at SdfSize, y down. A
// glyph without an outline, a space, has no texels.
struct SdfBitmap {
  glm::ivec2 size{0, 0};
  glm::vec2 offset{0.0f, 0.0f};
  std::vector<std::uint8_t> texels;
};

// Decodes UTF-8 into codepoints; a malformed byte becomes U+FFFD.
[[nodiscard]] std::vector<char32_t> decodeUtf8(std::string_view text);

// A TrueType or OpenType font parsed once with stb_truetype (docs/gui.md, "Text"). Metrics are in
// em, so a size multiplies them. Reads are const and safe from several threads at once, which the
// preload relies on. Owns a copy of the file's bytes: the asset database may re-read the file while
// a worker still rasterises.
class FontFace {
public:
  [[nodiscard]] static std::optional<FontFace> parse(std::span<const std::byte> file);
  FontFace(FontFace &&) noexcept;
  FontFace &operator=(FontFace &&) noexcept;
  ~FontFace();

  // The glyph of a codepoint; 0, the font's notdef, for one the font does not have.
  [[nodiscard]] std::uint32_t glyphIndex(char32_t codepoint) const;
  [[nodiscard]] float advance(std::uint32_t glyph) const;
  [[nodiscard]] float leftBearing(std::uint32_t glyph) const;
  // The adjustment to the pen between two glyphs, usually negative.
  [[nodiscard]] float kern(std::uint32_t left, std::uint32_t right) const;
  [[nodiscard]] float ascent() const noexcept {
    return m_ascent;
  }
  // Negative: below the baseline.
  [[nodiscard]] float descent() const noexcept {
    return m_descent;
  }
  [[nodiscard]] float lineGap() const noexcept {
    return m_lineGap;
  }
  [[nodiscard]] SdfBitmap rasterise(std::uint32_t glyph) const;

private:
  FontFace() = default;

  std::vector<std::byte> m_file;
  std::unique_ptr<stbtt_fontinfo> m_info;
  float m_unit{0.001f}; // font units to em
  float m_ascent{0.0f};
  float m_descent{0.0f};
  float m_lineGap{0.0f};
};

} // namespace sonnet::gui
