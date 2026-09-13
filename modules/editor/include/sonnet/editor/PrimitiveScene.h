#pragma once

#include <sonnet/renderer/Renderer.h>
#include <sonnet/renderer/SceneView.h>

#include <span>
#include <vector>

namespace sonnet::editor {

// The M1 test scene: every primitive on a ground plane, one of them turning so the viewport is
// visibly live. Replaced by scenes from `world` in M2.
class PrimitiveScene {
public:
  explicit PrimitiveScene(renderer::Renderer &renderer);
  ~PrimitiveScene();
  PrimitiveScene(const PrimitiveScene &) = delete;
  PrimitiveScene &operator=(const PrimitiveScene &) = delete;

  void update(float dt);

  [[nodiscard]] std::span<const renderer::DrawItem> draws() const noexcept {
    return m_draws;
  }
  [[nodiscard]] const renderer::DirectionalLight &light() const noexcept {
    return m_light;
  }

private:
  renderer::Renderer &m_renderer;
  std::vector<renderer::MeshHandle> m_meshes;
  std::vector<renderer::DrawItem> m_draws;
  renderer::DirectionalLight m_light;
  float m_time{0.0f};
};

} // namespace sonnet::editor
