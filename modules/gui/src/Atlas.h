#pragma once

#include "Font.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sonnet::gui {

constexpr int AtlasPageSize = 1024;

// Where a glyph's distance field sits: a rectangle of texels on a page, and the offset from the pen
// at which the rectangle is drawn at SdfSize. A glyph without an outline has an empty rectangle.
struct AtlasGlyph {
  std::uint32_t page{0};
  glm::ivec2 position{0, 0};
  glm::ivec2 size{0, 0};
  glm::vec2 offset{0.0f, 0.0f};
};

// The glyphs of one font packed into pages of R8 texels, in rows (shelves) of the height of the
// first glyph that opened them. The CPU half of the atlas: it knows nothing of textures, and says
// which pages changed since they were last uploaded.
class GlyphAtlas {
public:
  struct Page {
    std::vector<std::uint8_t> texels = std::vector<std::uint8_t>(std::size_t{AtlasPageSize} * AtlasPageSize, 0);
    bool dirty{false};
  };

  [[nodiscard]] const AtlasGlyph *find(std::uint32_t glyph) const;
  // Packs the bitmap, or returns the glyph already there. A new page opens when none has room.
  const AtlasGlyph &insert(std::uint32_t glyph, const SdfBitmap &bitmap);

  [[nodiscard]] std::vector<Page> &pages() noexcept {
    return m_pages;
  }
  [[nodiscard]] const std::vector<Page> &pages() const noexcept {
    return m_pages;
  }

private:
  struct Shelf {
    int y{0};
    int height{0};
    int used{0};
  };
  struct PageShelves {
    std::vector<Shelf> shelves;
    int nextY{0};
  };

  // A texel of space on the right and below keeps a glyph's neighbour out of its bilinear taps.
  [[nodiscard]] bool place(PageShelves &page, glm::ivec2 size, glm::ivec2 &at) const;

  std::unordered_map<std::uint32_t, AtlasGlyph> m_glyphs;
  std::vector<Page> m_pages;
  std::vector<PageShelves> m_shelves;
};

} // namespace sonnet::gui
