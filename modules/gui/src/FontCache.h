#pragma once

#include "Atlas.h"
#include "Font.h"

#include <sonnet/assets/Asset.h>
#include <sonnet/core/JobSystem.h>
#include <sonnet/core/Uuid.h>
#include <sonnet/renderer/Renderer.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
}

namespace sonnet::gui {

// A font in use: the parsed face, its glyph atlas, the atlas's pages as textures, and the preload
// still in flight. Lives at a stable address, since the preload job points into it.
struct LoadedFont {
  LoadedFont(FontFace &&parsed, std::uint64_t fileRevision) : face(std::move(parsed)), revision(fileRevision) {
  }
  LoadedFont(const LoadedFont &) = delete;
  LoadedFont &operator=(const LoadedFont &) = delete;

  struct PageTexture {
    renderer::TextureHandle texture;
    std::uint64_t uploadedFrame{0}; // the frame it was last uploaded in; 0 for never
  };
  struct Preloaded {
    std::vector<std::pair<std::uint32_t, SdfBitmap>> glyphs;
  };

  FontFace face;
  std::uint64_t revision;
  GlyphAtlas atlas;
  std::vector<PageTexture> pages;
  core::JobHandle job;
  std::unique_ptr<Preloaded> preloaded; // written by the job, read once it has finished
};

// The fonts of a GuiSystem, by identity: loads them from the asset database, rasterises glyphs
// when they are first drawn, and keeps each atlas page's texture current (ADR-0026, "Text").
// The page textures are released when their font is replaced and with the cache, which therefore
// has to go before the renderer it uploaded to.
class FontCache {
public:
  FontCache() = default;
  FontCache(const FontCache &) = delete;
  FontCache &operator=(const FontCache &) = delete;
  // Waits for the preload jobs, which read the faces, and releases the page textures.
  ~FontCache();

  // The font, parsed on first use and again when its file was re-read; null for a nil identity, a
  // missing or unreadable file. The first use schedules the preload on the asset database's jobs.
  [[nodiscard]] LoadedFont *get(const core::Uuid &uuid, assets::AssetDatabase &assets);
  // The glyph's place in the atlas, rasterising it now when no one has: a glyph seen for the first
  // time is in the atlas the frame it is drawn.
  [[nodiscard]] const AtlasGlyph &glyph(LoadedFont &font, std::uint32_t glyph);
  // Moves the preload's glyphs into the atlas once its job has finished.
  void collect(LoadedFont &font);
  // Creates the texture of every page that changed and releases the one it replaces, at most one
  // upload per page per `frame`; a page that changed after its upload this frame waits for the next.
  // The number of uploads made.
  std::size_t upload(renderer::Renderer &renderer, std::uint64_t frame);

private:
  // Waits for the font's preload and releases its textures: nothing of it is in use after.
  void retire(LoadedFont &font);

  std::unordered_map<core::Uuid, std::unique_ptr<LoadedFont>> m_fonts;
  core::JobSystem *m_jobs{nullptr};
  renderer::Renderer *m_renderer{nullptr}; // set by the first upload, since only that makes a texture
};

} // namespace sonnet::gui
