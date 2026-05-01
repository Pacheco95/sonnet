#pragma once

#include <sonnet/renderer/IRenderer.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>

#include <memory>

namespace sonnet::renderer {

class Renderer : public IRenderer {
public:
    explicit Renderer(std::unique_ptr<IRendererBackend> backend);
    ~Renderer() override;

    bool init(sonnet::window::IWindow& window) override;
    void shutdown() override;
    void renderFrame() override;

private:
    std::unique_ptr<IRendererBackend> m_backend;
    bool m_initialized{false};
};

} // namespace sonnet::renderer
