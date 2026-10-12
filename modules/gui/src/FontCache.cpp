#include "FontCache.h"

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <set>

namespace sonnet::gui {

namespace {

// The glyphs of the ranges, in order and once each, without the notdef: it is drawn for what the
// font lacks, and found on first use.
[[nodiscard]] std::vector<std::uint32_t> preloadGlyphs(const FontFace &face,
                                                       const std::vector<assets::GlyphRange> &ranges) {
  std::set<std::uint32_t> glyphs;
  for (const assets::GlyphRange &range : ranges) {
    for (std::uint32_t codepoint = range.first; codepoint <= range.last && codepoint <= 0x10FFFF; ++codepoint) {
      if (const std::uint32_t glyph = face.glyphIndex(static_cast<char32_t>(codepoint)); glyph != 0) {
        glyphs.insert(glyph);
      }
    }
  }
  return {glyphs.begin(), glyphs.end()};
}

} // namespace

FontCache::~FontCache() {
  for (auto &[uuid, font] : m_fonts) {
    retire(*font);
  }
}

void FontCache::retire(LoadedFont &font) {
  if (m_jobs != nullptr) {
    m_jobs->wait(font.job);
  }
  for (const LoadedFont::PageTexture &page : font.pages) {
    if (page.texture.isValid()) {
      m_renderer->destroyTexture(page.texture);
    }
  }
}

LoadedFont *FontCache::get(const core::Uuid &uuid, assets::AssetDatabase &assets) {
  if (uuid.isNil()) {
    return nullptr;
  }
  const assets::FontSource *source = assets.font(uuid);
  if (source == nullptr) {
    return nullptr;
  }
  m_jobs = &assets.jobs();
  if (const auto it = m_fonts.find(uuid); it != m_fonts.end()) {
    if (it->second->revision == source->revision) {
      return it->second.get();
    }
    retire(*it->second); // the file was re-read: the old face and its glyphs go
    m_fonts.erase(it);
  }
  auto face = FontFace::parse(source->bytes);
  if (!face) {
    SONNET_LOG_ERROR("gui: font {} is not a TrueType or OpenType font with outlines", uuid.toString());
    return nullptr;
  }
  auto font = std::make_unique<LoadedFont>(std::move(*face), source->revision);
  std::vector<std::uint32_t> glyphs = preloadGlyphs(font->face, source->preload);
  if (!glyphs.empty()) {
    LoadedFont *raw = font.get();
    raw->preloaded = std::make_unique<LoadedFont::Preloaded>();
    raw->job = m_jobs->schedule("gui preload", [raw, glyphs = std::move(glyphs)] {
      SONNET_ZONE();
      raw->preloaded->glyphs.reserve(glyphs.size());
      for (const std::uint32_t glyph : glyphs) {
        raw->preloaded->glyphs.emplace_back(glyph, raw->face.rasterise(glyph));
      }
    });
  }
  return m_fonts.emplace(uuid, std::move(font)).first->second.get();
}

const LoadedFont *FontCache::find(const core::Uuid &uuid) const {
  const auto it = m_fonts.find(uuid);
  return it == m_fonts.end() ? nullptr : it->second.get();
}

const AtlasGlyph &FontCache::glyph(LoadedFont &font, std::uint32_t glyph) {
  if (const AtlasGlyph *found = font.atlas.find(glyph)) {
    return *found;
  }
  return font.atlas.insert(glyph, font.face.rasterise(glyph));
}

void FontCache::collect(LoadedFont &font) {
  if (!font.preloaded || !font.job.finished()) {
    return;
  }
  for (const auto &[glyph, bitmap] : font.preloaded->glyphs) {
    font.atlas.insert(glyph, bitmap); // a glyph drawn before the job finished is already there
  }
  font.preloaded.reset();
}

std::size_t FontCache::upload(renderer::Renderer &renderer, std::uint64_t frame) {
  SONNET_ZONE();
  std::size_t uploads = 0;
  m_renderer = &renderer;
  for (auto &[uuid, font] : m_fonts) {
    std::vector<GlyphAtlas::Page> &pages = font->atlas.pages();
    font->pages.resize(pages.size());
    for (std::size_t index = 0; index < pages.size(); ++index) {
      GlyphAtlas::Page &page = pages[index];
      LoadedFont::PageTexture &texture = font->pages[index];
      if (!page.dirty || texture.uploadedFrame == frame) {
        continue;
      }
      renderer::TextureData data{.size = {AtlasPageSize, AtlasPageSize},
                                 .format = rhi::Format::R8Unorm,
                                 .mipLevels = 1,
                                 .cube = false,
                                 .data = {}};
      data.data.resize(page.texels.size());
      std::ranges::transform(page.texels, data.data.begin(), [](std::uint8_t texel) { return std::byte{texel}; });
      const renderer::TextureHandle created = renderer.createTexture(data, "gui glyph atlas");
      if (!created.isValid()) {
        continue;
      }
      if (texture.texture.isValid()) {
        renderer.destroyTexture(texture.texture);
      }
      texture.texture = created;
      texture.uploadedFrame = frame;
      page.dirty = false;
      ++uploads;
    }
  }
  return uploads;
}

} // namespace sonnet::gui
