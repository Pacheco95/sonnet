#include "Atlas.h"

#include <algorithm>

namespace sonnet::gui {

const AtlasGlyph *GlyphAtlas::find(std::uint32_t glyph) const {
  const auto it = m_glyphs.find(glyph);
  return it != m_glyphs.end() ? &it->second : nullptr;
}

bool GlyphAtlas::place(PageShelves &page, glm::ivec2 size, glm::ivec2 &at) const {
  const int width = size.x + 1;
  const int height = size.y + 1;
  if (width > AtlasPageSize || height > AtlasPageSize) {
    return false;
  }
  // The shelf that wastes the least height, among those with room to the right.
  Shelf *best = nullptr;
  for (Shelf &shelf : page.shelves) {
    if (shelf.height >= height && shelf.used + width <= AtlasPageSize &&
        (best == nullptr || shelf.height < best->height)) {
      best = &shelf;
    }
  }
  if (best == nullptr) {
    if (page.nextY + height > AtlasPageSize) {
      return false;
    }
    best = &page.shelves.emplace_back(Shelf{.y = page.nextY, .height = height, .used = 0});
    page.nextY += height;
  }
  at = {best->used, best->y};
  best->used += width;
  return true;
}

const AtlasGlyph &GlyphAtlas::insert(std::uint32_t glyph, const SdfBitmap &bitmap) {
  if (const auto it = m_glyphs.find(glyph); it != m_glyphs.end()) {
    return it->second;
  }
  AtlasGlyph placed{.page = 0, .position = {0, 0}, .size = bitmap.size, .offset = bitmap.offset};
  if (bitmap.size.x >= AtlasPageSize || bitmap.size.y >= AtlasPageSize) {
    placed.size = {0, 0}; // cannot happen at SdfSize; a page that could never hold it would open pages for ever
  } else if (!bitmap.texels.empty()) {
    glm::ivec2 at{0, 0};
    std::size_t page = 0;
    while (true) {
      if (page == m_pages.size()) {
        m_pages.emplace_back();
        m_shelves.emplace_back();
      }
      if (place(m_shelves[page], bitmap.size, at)) {
        break;
      }
      ++page;
    }
    placed.page = static_cast<std::uint32_t>(page);
    placed.position = at;
    Page &target = m_pages[page];
    for (int row = 0; row < bitmap.size.y; ++row) {
      const auto from = bitmap.texels.begin() + static_cast<std::ptrdiff_t>(row) * bitmap.size.x;
      std::copy_n(from, bitmap.size.x,
                  target.texels.begin() + static_cast<std::ptrdiff_t>(at.y + row) * AtlasPageSize + at.x);
    }
    target.dirty = true;
  }
  return m_glyphs.emplace(glyph, placed).first->second;
}

} // namespace sonnet::gui
