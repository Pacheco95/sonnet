#include <sonnet/editor/PrimitiveScene.h>

#include <sonnet/renderer/Primitives.h>

namespace sonnet::editor {

namespace {

glm::mat4 placeAt(glm::vec3 position) {
  return glm::translate(glm::mat4{1.0f}, position);
}

} // namespace

PrimitiveScene::PrimitiveScene(renderer::Renderer &renderer) : m_renderer(renderer) {
  const renderer::MeshHandle ground = renderer.createMesh(renderer::primitives::plane({12.0f, 12.0f}), "ground");
  const renderer::MeshHandle box = renderer.createMesh(renderer::primitives::box(), "box");
  const renderer::MeshHandle sphere = renderer.createMesh(renderer::primitives::sphere(), "sphere");
  const renderer::MeshHandle cylinder = renderer.createMesh(renderer::primitives::cylinder(), "cylinder");
  const renderer::MeshHandle capsule = renderer.createMesh(renderer::primitives::capsule(), "capsule");
  m_meshes = {ground, box, sphere, cylinder, capsule};

  m_draws = {
      {.mesh = ground, .transform = placeAt({0.0f, 0.0f, 0.0f}), .color = {0.45f, 0.47f, 0.5f, 1.0f}},
      {.mesh = box, .transform = placeAt({0.0f, 0.5f, 0.0f}), .color = {0.9f, 0.35f, 0.25f, 1.0f}},
      {.mesh = sphere, .transform = placeAt({2.0f, 0.5f, 0.0f}), .color = {0.25f, 0.55f, 0.9f, 1.0f}},
      {.mesh = cylinder, .transform = placeAt({-2.0f, 0.5f, 0.0f}), .color = {0.3f, 0.8f, 0.4f, 1.0f}},
      {.mesh = capsule, .transform = placeAt({0.0f, 0.5f, 2.0f}), .color = {0.9f, 0.8f, 0.3f, 1.0f}},
      {.mesh = box, .transform = placeAt({0.0f, 1.5f, -2.5f}), .color = {0.8f, 0.5f, 0.85f, 1.0f}},
  };
}

PrimitiveScene::~PrimitiveScene() {
  for (const renderer::MeshHandle mesh : m_meshes) {
    m_renderer.destroyMesh(mesh);
  }
}

void PrimitiveScene::update(float dt) {
  m_time += dt;
  // The raised box turns about its vertical axis.
  m_draws[5].transform = placeAt({0.0f, 1.5f, -2.5f}) *
                         glm::rotate(glm::mat4{1.0f}, m_time * 0.6f, glm::vec3{0.0f, 1.0f, 0.0f}) *
                         glm::rotate(glm::mat4{1.0f}, glm::radians(30.0f), glm::vec3{1.0f, 0.0f, 0.0f});
}

} // namespace sonnet::editor
