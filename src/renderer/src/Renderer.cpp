#include "Renderer.hpp"

#include <sonnet/logging/Logger.hpp>
#include <sonnet/renderer/RendererFactory.hpp>

namespace sonnet::renderer {

Renderer::Renderer(std::unique_ptr<IRendererBackend> backend) : m_backend(std::move(backend)) {}

Renderer::~Renderer() { shutdown(); } // NOLINT(clang-analyzer-optin.cplusplus.VirtualCall)

bool Renderer::init(sonnet::window::IWindow& window) {
    if (!m_backend->init(window)) {
        SONNET_LOG_ERROR("Renderer backend initialization failed");
        return false;
    }
    m_initialized = true;
    return true;
}

void Renderer::shutdown() {
    if (!m_initialized) {
        return;
    }
    m_initialized = false;
    m_backend->shutdown();
}

void Renderer::renderFrame() {
    m_backend->beginFrame();
    m_backend->drawPrimitive();
    m_backend->endFrame();
}

std::unique_ptr<IRenderer> createRenderer(std::unique_ptr<IRendererBackend> backend) {
    return std::make_unique<Renderer>(std::move(backend));
}

} // namespace sonnet::renderer
