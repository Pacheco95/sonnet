#include <sonnet/scene/Scene.hpp>

#include <algorithm>

namespace sonnet::scene {

// NOLINTNEXTLINE(readability-non-const-parameter)
GameObject& Scene::createObject(const std::string& name, GameObject* parent) {
    auto obj = std::make_unique<GameObject>(name);
    if (parent != nullptr) {
        obj->transform.setParent(&parent->transform, false);
    }
    m_objects.push_back(std::move(obj));
    return *m_objects.back();
}

// NOLINTNEXTLINE(readability-non-const-parameter)
void Scene::destroyObject(GameObject* obj) {
    // Detach from parent so the hierarchy stays consistent.
    obj->transform.setParent(nullptr, false);
    // Re-parent any children to the root before removal.
    for (Transform* child : obj->transform.getChildren()) {
        child->setParent(nullptr, true);
    }
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                   [obj](const auto& ptr) { return ptr.get() == obj; }),
                    m_objects.end());
}

} // namespace sonnet::scene
