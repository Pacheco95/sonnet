#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace sonnet::scene {

class Transform {
public:
    Transform() = default;

    // ── Hierarchy ─────────────────────────────────────────────────────────────
    // keepWorldTransform=true re-computes local values so world pos/rot don't change.
    // Silently rejects if newParent is this or any ancestor of this (circular guard).
    void setParent(Transform* newParent, bool keepWorldTransform = true);

    [[nodiscard]] Transform*                      getParent()   const { return m_parent; }
    [[nodiscard]] const std::vector<Transform*>&  getChildren() const { return m_children; }

    // ── Local values ──────────────────────────────────────────────────────────
    void setLocalPosition(glm::vec3 pos);
    void setLocalRotation(glm::quat rot);
    void setLocalScale(glm::vec3 scale);

    [[nodiscard]] glm::vec3 getLocalPosition() const { return m_localPosition; }
    [[nodiscard]] glm::quat getLocalRotation() const { return m_localRotation; }
    [[nodiscard]] glm::vec3 getLocalScale()    const { return m_localScale; }

    // ── World values ──────────────────────────────────────────────────────────
    void setWorldPosition(glm::vec3 worldPos);
    void setWorldRotation(glm::quat worldRot);

    [[nodiscard]] glm::vec3 getWorldPosition() const;
    [[nodiscard]] glm::quat getWorldRotation() const;

    // ── Model matrix (lazy, dirty-flag cached) ────────────────────────────────
    [[nodiscard]] const glm::mat4& getModelMatrix() const;

    // ── World-space direction vectors ─────────────────────────────────────────
    [[nodiscard]] glm::vec3 forward() const;
    [[nodiscard]] glm::vec3 up()      const;
    [[nodiscard]] glm::vec3 right()   const;

private:
    [[nodiscard]] bool isAncestor(const Transform* candidate) const;
    void markDirty();
    [[nodiscard]] glm::mat4 localMatrix()       const;
    [[nodiscard]] glm::mat4 parentWorldMatrix() const;

    glm::vec3 m_localPosition{0.0F, 0.0F, 0.0F};
    glm::quat m_localRotation{1.0F, 0.0F, 0.0F, 0.0F}; // identity
    glm::vec3 m_localScale{1.0F, 1.0F, 1.0F};

    Transform*              m_parent{nullptr};
    std::vector<Transform*> m_children;

    mutable glm::mat4 m_modelMatrix{1.0F};
    mutable bool      m_dirty{true};
};

} // namespace sonnet::scene
