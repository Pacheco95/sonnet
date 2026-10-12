#pragma once

#include <sonnet/gui/Layout.h>

#include <sonnet/renderer/Renderer.h>

#include <vector>

namespace sonnet::gui {

struct GuiImage;

// Appends the quads of one image: the element's rectangle with the texture across it, or, when the
// image has borders and the texture a size, nine of them, the corners at the borders' size times
// the element's scale. Split from GuiSystem::buildQuads so it can be tested without assets.
void appendImage(std::vector<renderer::OverlayQuad> &out, const GuiElement &element, const GuiImage &image,
                 glm::vec4 color, renderer::TextureHandle texture, glm::uvec2 texels);

} // namespace sonnet::gui
