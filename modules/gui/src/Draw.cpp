#include "Draw.h"

#include "FontCache.h"
#include "Text.h"

#include <sonnet/gui/GuiSystem.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/world/World.h>

#include <algorithm>
#include <array>

namespace sonnet::gui {

namespace {

// One axis of a nine-slice: the four edges of its three bands, in pixels and in texture
// coordinates. Borders wider than the rectangle between them are shrunk together to fit.
struct Bands {
  std::array<float, 4> position;
  std::array<float, 4> uv;
};

[[nodiscard]] Bands bands(float from, float to, float low, float high, float texels, float scale) {
  float lowSize = low * scale;
  float highSize = high * scale;
  if (const float room = to - from; lowSize + highSize > room && lowSize + highSize > 0.0f) {
    const float fit = std::max(room, 0.0f) / (lowSize + highSize);
    lowSize *= fit;
    highSize *= fit;
  }
  return {{from, from + lowSize, to - highSize, to}, {0.0f, low / texels, 1.0f - high / texels, 1.0f}};
}

} // namespace

void appendImage(std::vector<renderer::OverlayQuad> &out, const GuiElement &element, const GuiImage &image,
                 glm::vec4 color, renderer::TextureHandle texture, glm::uvec2 texels) {
  const glm::vec4 slice = image.slice;
  if (texture.isValid() && texels.x > 0 && texels.y > 0 && glm::any(glm::greaterThan(slice, glm::vec4{0.0f}))) {
    const Bands columns =
        bands(element.rect.min.x, element.rect.max.x, slice.x, slice.z, static_cast<float>(texels.x), element.scale);
    const Bands rows =
        bands(element.rect.min.y, element.rect.max.y, slice.y, slice.w, static_cast<float>(texels.y), element.scale);
    for (std::size_t row = 0; row < 3; ++row) {
      for (std::size_t column = 0; column < 3; ++column) {
        if (columns.position[column + 1] <= columns.position[column] || rows.position[row + 1] <= rows.position[row]) {
          continue;
        }
        out.push_back({.min = {columns.position[column], rows.position[row]},
                       .max = {columns.position[column + 1], rows.position[row + 1]},
                       .uvMin = {columns.uv[column], rows.uv[row]},
                       .uvMax = {columns.uv[column + 1], rows.uv[row + 1]},
                       .color = color,
                       .texture = texture});
      }
    }
    return;
  }
  out.push_back({.min = element.rect.min, .max = element.rect.max, .color = color, .texture = texture});
}

void GuiSystem::buildQuads(renderer::Renderer &renderer, assets::AssetDatabase &assets) {
  m_quads.clear();
  const flecs::world &ecs = m_world.ecs();
  // The glyph quads whose texture is their font's page, known once the pages are uploaded.
  struct PageQuad {
    std::size_t quad;
    const LoadedFont *font;
    std::uint32_t page;
  };
  std::vector<PageQuad> pageQuads;
  for (const GuiElement &element : m_elements) {
    const flecs::entity entity{ecs, element.entity};
    if (const GuiImage *image = entity.try_get<GuiImage>()) {
      glm::vec4 color = image->tint;
      if (const GuiButton *button = entity.try_get<GuiButton>()) {
        color *= !button->interactable ? button->disabled : pressed(element.entity) ? button->pressed : button->normal;
      }
      renderer::TextureHandle texture;
      if (!image->texture.isNil()) {
        texture = assets.requestTexture(image->texture);
      }
      // A texture still loading draws nothing: the white default would flash in its place.
      if ((image->texture.isNil() || texture.isValid()) && color.a > 0.0f && element.rect.size().x > 0.0f &&
          element.rect.size().y > 0.0f) {
        appendImage(m_quads, element, *image, color, texture, renderer.textureSize(texture));
      }
    }
    const GuiText *text = entity.try_get<GuiText>();
    if (text == nullptr || text->text.empty() || text->color.a <= 0.0f || text->size <= 0.0f) {
      continue;
    }
    LoadedFont *font = m_fonts->get(text->font, assets);
    if (font == nullptr) {
      continue; // no font, or its file is not readable: nothing, not a placeholder
    }
    m_fonts->collect(*font);
    const float pixelSize = text->size * element.scale;
    const TextLayout layout =
        layoutText(font->face, text->text, pixelSize, element.rect, text->horizontal, text->vertical, text->wrap);
    const float scale = pixelSize / static_cast<float>(SdfSize);
    constexpr float PageTexels = static_cast<float>(AtlasPageSize);
    for (const PlacedGlyph &placed : layout.glyphs) {
      const AtlasGlyph &glyph = m_fonts->glyph(*font, placed.glyph);
      if (glyph.size.x == 0 || glyph.size.y == 0) {
        continue;
      }
      const glm::vec2 min = placed.pen + glyph.offset * scale;
      pageQuads.push_back({m_quads.size(), font, glyph.page});
      m_quads.push_back({.min = min,
                         .max = min + glm::vec2{glyph.size} * scale,
                         .uvMin = glm::vec2{glyph.position} / PageTexels,
                         .uvMax = glm::vec2{glyph.position + glyph.size} / PageTexels,
                         .color = text->color,
                         .texture = {},
                         .kind = renderer::OverlayKind::Glyph});
    }
  }
  if (pageQuads.empty()) {
    return;
  }
  m_fonts->upload(renderer, m_frame);
  for (const PageQuad &pageQuad : pageQuads) {
    m_quads[pageQuad.quad].texture = pageQuad.font->pages[pageQuad.page].texture;
  }
  // A page that has no texture yet cannot be drawn from: the white default would be a block.
  std::erase_if(m_quads, [](const renderer::OverlayQuad &quad) {
    return quad.kind == renderer::OverlayKind::Glyph && !quad.texture.isValid();
  });
}

void GuiSystem::addPass(renderer::Renderer &renderer, assets::AssetDatabase &assets, renderer::RenderGraph &graph,
                        renderer::GraphImage target) {
  buildQuads(renderer, assets);
  renderer.addOverlayPass(graph, target, m_quads);
}

} // namespace sonnet::gui
