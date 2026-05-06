#pragma once

#include <sonnet/scene/GameObject.hpp>

#include <memory>
#include <vector>

namespace sonnet::scene {

class Scene {
public:
    Scene() = default;

    // Creates and owns a new GameObject. parent may be null (root-level object).
    GameObject& createObject(const std::string& name, GameObject* parent = nullptr);

    // Removes and destroys the GameObject. Pointer is invalid after this call.
    void destroyObject(GameObject* obj);

    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>>& objects() const {
        return m_objects;
    }

private:
    std::vector<std::unique_ptr<GameObject>> m_objects;
};

} // namespace sonnet::scene
