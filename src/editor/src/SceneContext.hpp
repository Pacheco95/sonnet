#pragma once

#include <sonnet/renderer/IRendererBackend.hpp>
#include <sonnet/renderer/SceneRenderTypes.hpp>
#include <sonnet/scene/Scene.hpp>
#include <sonnet/scene/Transform.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace sonnet::editor {

class SceneContext {
public:
    explicit SceneContext(renderer::IRendererBackend& backend);
    ~SceneContext();

    SceneContext(const SceneContext&) = delete;
    SceneContext& operator=(const SceneContext&) = delete;

    void addPrimitive(scene::PrimitiveType type, const std::string& name);
    void addDirectionalLight(const std::string& name);
    void removeObject(uint32_t id);

    void selectObject(uint32_t id);
    void deselectAll();
    [[nodiscard]] uint32_t selectedId() const { return m_selectedId; }
    [[nodiscard]] bool hasDirectionalLight() const;
    [[nodiscard]] uint32_t directionalLightId() const;

    void setParent(uint32_t childId, uint32_t parentId);

    [[nodiscard]] renderer::SceneRenderDesc buildRenderDesc(
        const glm::mat4& view, const glm::mat4& proj,
        const glm::vec3& cameraPos, glm::ivec2 viewportSize) const;

    [[nodiscard]] scene::Scene& scene() { return m_scene; }
    [[nodiscard]] const scene::Scene& scene() const { return m_scene; }
    [[nodiscard]] scene::GameObject* findById(uint32_t id) const;
    [[nodiscard]] scene::GameObject* findByTransform(const scene::Transform* t) const;
    [[nodiscard]] uint32_t idOf(const scene::GameObject* obj) const;

private:
    renderer::IRendererBackend& m_backend;
    scene::Scene m_scene;
    std::unordered_map<uint32_t, scene::GameObject*> m_objects;
    std::unordered_map<uint32_t, uint64_t>           m_meshHandles;
    uint32_t m_selectedId{0};
    uint32_t m_nextId{1};
};

} // namespace sonnet::editor
