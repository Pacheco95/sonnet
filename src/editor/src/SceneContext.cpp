#include "SceneContext.hpp"

#include <sonnet/primitives/MeshPrimitives.hpp>

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

namespace sonnet::editor {

SceneContext::SceneContext(renderer::IRendererBackend& backend) : m_backend(backend) {}

// NOLINTNEXTLINE(modernize-use-equals-default)
SceneContext::~SceneContext() {
    for (auto& [id, handle] : m_meshHandles) {
        if (handle != 0) {
            m_backend.releaseMesh(handle);
        }
    }
}

static renderer::CPUMesh makeMeshForType(scene::PrimitiveType type) {
    switch (type) {
    case scene::PrimitiveType::Cube:
        return primitives::makeBox({1.0F, 1.0F, 1.0F});
    case scene::PrimitiveType::Sphere:
        return primitives::makeUVSphere(24, 16);
    case scene::PrimitiveType::Cylinder:
        return primitives::makeCylinder(0.5F, 1.0F, 24);
    case scene::PrimitiveType::Plane:
        return primitives::makePlane({2.0F, 2.0F});
    case scene::PrimitiveType::Capsule:
        return primitives::makeCapsule(0.5F, 1.0F, 16);
    }
    return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void SceneContext::addPrimitive(scene::PrimitiveType type, const std::string& name) {
    auto cpuMesh = makeMeshForType(type);
    const uint64_t meshHandle = m_backend.uploadMesh(cpuMesh);

    auto& obj = m_scene.createObject(name);
    obj.primitiveType = type;

    const uint32_t id = m_nextId++;
    m_objects[id] = &obj;
    m_meshHandles[id] = meshHandle;
}

void SceneContext::addDirectionalLight(const std::string& name) {
    if (hasDirectionalLight()) {
        return;
    }
    auto& obj = m_scene.createObject(name);
    obj.light = scene::LightData{};

    const uint32_t id = m_nextId++;
    m_objects[id] = &obj;
    m_meshHandles[id] = 0;
}

void SceneContext::removeObject(uint32_t id) {
    auto objIt = m_objects.find(id);
    if (objIt == m_objects.end()) {
        return;
    }
    auto handleIt = m_meshHandles.find(id);
    if (handleIt != m_meshHandles.end()) {
        if (handleIt->second != 0) {
            m_backend.releaseMesh(handleIt->second);
        }
        m_meshHandles.erase(handleIt);
    }
    m_scene.destroyObject(objIt->second);
    m_objects.erase(objIt);

    if (m_selectedId == id) {
        m_selectedId = 0;
    }
}

void SceneContext::selectObject(uint32_t id) { m_selectedId = id; }

void SceneContext::deselectAll() { m_selectedId = 0; }

bool SceneContext::hasDirectionalLight() const {
    return std::ranges::any_of(m_objects,
                               [](const auto& pair) { return pair.second->light.has_value(); });
}

uint32_t SceneContext::directionalLightId() const {
    for (const auto& [id, ptr] : m_objects) {
        if (ptr->light.has_value()) {
            return id;
        }
    }
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters,readability-make-member-function-const)
void SceneContext::setParent(uint32_t childId, uint32_t parentId) {
    auto* child = findById(childId);
    auto* parent = findById(parentId);
    if (child == nullptr) {
        return;
    }
    child->transform.setParent(parent != nullptr ? &parent->transform : nullptr, true);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
renderer::SceneRenderDesc SceneContext::buildRenderDesc(const glm::mat4& view,
                                                        const glm::mat4& proj,
                                                        const glm::vec3& cameraPos,
                                                        glm::ivec2 viewportSize) const {

    renderer::SceneRenderDesc desc;
    desc.viewMatrix = view;
    desc.projMatrix = proj;
    desc.cameraPosition = cameraPos;
    desc.viewportSize = viewportSize;

    for (const auto& [id, objPtr] : m_objects) {
        if (objPtr == nullptr) {
            continue;
        }
        // Directional light
        if (objPtr->light.has_value() && !desc.dirLight.has_value()) {
            const auto& ld = objPtr->light.value();
            renderer::DirectionalLightDesc dl;
            dl.direction = objPtr->transform.forward();
            dl.color = ld.color;
            dl.intensity = ld.intensity;
            desc.dirLight = dl;
            continue;
        }
        // Primitive objects with mesh handles
        auto handleIt = m_meshHandles.find(id);
        if (handleIt == m_meshHandles.end() || handleIt->second == 0) {
            continue;
        }
        renderer::DrawItem item;
        item.meshHandle = handleIt->second;
        item.modelMatrix = objPtr->transform.getModelMatrix();
        item.objectId = id;
        item.isSelected = (id == m_selectedId);
        desc.objects.push_back(item);
    }

    return desc;
}

scene::GameObject* SceneContext::findById(uint32_t id) const {
    auto it = m_objects.find(id);
    return (it != m_objects.end()) ? it->second : nullptr;
}

scene::GameObject* SceneContext::findByTransform(const scene::Transform* t) const {
    for (const auto& [id, ptr] : m_objects) {
        if (&ptr->transform == t) {
            return ptr;
        }
    }
    return nullptr;
}

uint32_t SceneContext::idOf(const scene::GameObject* obj) const {
    for (const auto& [id, ptr] : m_objects) {
        if (ptr == obj) {
            return id;
        }
    }
    return 0;
}

} // namespace sonnet::editor
