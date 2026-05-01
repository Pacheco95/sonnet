#pragma once

#include <memory>

namespace sonnet::renderer {

class IRenderer;
class IRendererBackend;

std::unique_ptr<IRenderer> createRenderer(std::unique_ptr<IRendererBackend> backend);

} // namespace sonnet::renderer
