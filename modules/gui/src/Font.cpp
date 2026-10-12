#include "Font.h"

// stb_truetype is header-only; this is its one implementation in the engine.
#define STB_TRUETYPE_IMPLEMENTATION // NOLINT(readability-identifier-naming): stb's switch, not ours
#include <stb_truetype.h>

namespace sonnet::gui {

std::vector<char32_t> decodeUtf8(std::string_view text) {
  std::vector<char32_t> out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    const auto lead = static_cast<unsigned char>(text[at]);
    const int extra = lead < 0x80 ? 0 : (lead >> 5) == 0x6 ? 1 : (lead >> 4) == 0xE ? 2 : (lead >> 3) == 0x1E ? 3 : -1;
    if (extra < 0 || at + static_cast<std::size_t>(extra) >= text.size()) {
      out.push_back(U'�');
      ++at;
      continue;
    }
    char32_t codepoint = extra == 0 ? lead : lead & (0x3Fu >> extra);
    bool valid = true;
    for (int i = 1; i <= extra; ++i) {
      const auto next = static_cast<unsigned char>(text[at + static_cast<std::size_t>(i)]);
      valid = valid && (next & 0xC0) == 0x80;
      codepoint = (codepoint << 6) | (next & 0x3Fu);
    }
    if (!valid) {
      out.push_back(U'�');
      ++at;
      continue;
    }
    out.push_back(codepoint);
    at += static_cast<std::size_t>(extra) + 1;
  }
  return out;
}

FontFace::FontFace(FontFace &&) noexcept = default;
FontFace &FontFace::operator=(FontFace &&) noexcept = default;
FontFace::~FontFace() = default;

std::optional<FontFace> FontFace::parse(std::span<const std::byte> file) {
  FontFace face;
  face.m_file.assign(file.begin(), file.end());
  face.m_info = std::make_unique<stbtt_fontinfo>();
  const auto *data = reinterpret_cast<const unsigned char *>(face.m_file.data());
  const int offset = stbtt_GetFontOffsetForIndex(data, 0);
  if (offset < 0 || stbtt_InitFont(face.m_info.get(), data, offset) == 0) {
    return std::nullopt;
  }
  face.m_unit = stbtt_ScaleForMappingEmToPixels(face.m_info.get(), 1.0f);
  int ascent = 0;
  int descent = 0;
  int gap = 0;
  stbtt_GetFontVMetrics(face.m_info.get(), &ascent, &descent, &gap);
  face.m_ascent = static_cast<float>(ascent) * face.m_unit;
  face.m_descent = static_cast<float>(descent) * face.m_unit;
  face.m_lineGap = static_cast<float>(gap) * face.m_unit;
  return face;
}

std::uint32_t FontFace::glyphIndex(char32_t codepoint) const {
  return static_cast<std::uint32_t>(stbtt_FindGlyphIndex(m_info.get(), static_cast<int>(codepoint)));
}

float FontFace::advance(std::uint32_t glyph) const {
  int advance = 0;
  int bearing = 0;
  stbtt_GetGlyphHMetrics(m_info.get(), static_cast<int>(glyph), &advance, &bearing);
  return static_cast<float>(advance) * m_unit;
}

float FontFace::leftBearing(std::uint32_t glyph) const {
  int advance = 0;
  int bearing = 0;
  stbtt_GetGlyphHMetrics(m_info.get(), static_cast<int>(glyph), &advance, &bearing);
  return static_cast<float>(bearing) * m_unit;
}

float FontFace::kern(std::uint32_t left, std::uint32_t right) const {
  return static_cast<float>(stbtt_GetGlyphKernAdvance(m_info.get(), static_cast<int>(left), static_cast<int>(right))) *
         m_unit;
}

SdfBitmap FontFace::rasterise(std::uint32_t glyph) const {
  // 128 is the outline; the distance of SdfBorder pixels spans the whole byte range up to it.
  constexpr unsigned char onEdge = 128;
  constexpr float distanceScale = static_cast<float>(onEdge) / static_cast<float>(SdfBorder);
  int width = 0;
  int height = 0;
  int xOffset = 0;
  int yOffset = 0;
  unsigned char *texels = stbtt_GetGlyphSDF(m_info.get(), m_unit * static_cast<float>(SdfSize), static_cast<int>(glyph),
                                            SdfBorder, onEdge, distanceScale, &width, &height, &xOffset, &yOffset);
  SdfBitmap bitmap;
  if (texels != nullptr) {
    bitmap.size = {width, height};
    bitmap.offset = {static_cast<float>(xOffset), static_cast<float>(yOffset)};
    bitmap.texels.assign(texels, texels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    stbtt_FreeSDF(texels, nullptr);
  }
  return bitmap;
}

} // namespace sonnet::gui
