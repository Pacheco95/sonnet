#define GLM_ENABLE_EXPERIMENTAL
#include <sonnet/scene/Transform.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

namespace sonnet::scene {

// ── Hierarchy ─────────────────────────────────────────────────────────────────

bool Transform::isAncestor(const Transform* candidate) const {
    const Transform* node = m_parent;
    while (node != nullptr) {
        if (node == candidate) {
            return true;
        }
        node = node->m_parent;
    }
    return false;
}

void Transform::setParent(Transform* newParent, bool keepWorldTransform) {
    if (newParent == m_parent) {
        return;
    }
    // Silently reject circular parenting.
    if (newParent != nullptr && (newParent == this || newParent->isAncestor(this))) {
        return;
    }

    glm::vec3 savedWorldPos{};
    glm::quat savedWorldRot{};
    if (keepWorldTransform) {
        savedWorldPos = getWorldPosition();
        savedWorldRot = getWorldRotation();
    }

    if (m_parent != nullptr) {
        auto& siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
    }

    m_parent = newParent;

    if (m_parent != nullptr) {
        m_parent->m_children.push_back(this);
    }

    if (keepWorldTransform) {
        setWorldPosition(savedWorldPos);
        setWorldRotation(savedWorldRot);
    }

    markDirty();
}

// ── Local setters ─────────────────────────────────────────────────────────────

void Transform::setLocalPosition(glm::vec3 pos) {
    m_localPosition = pos;
    markDirty();
}

void Transform::setLocalRotation(glm::quat rot) {
    m_localRotation = rot;
    markDirty();
}

void Transform::setLocalScale(glm::vec3 scale) {
    m_localScale = scale;
    markDirty();
}

// ── World position ─────────────────────────────────────────────────────────────

void Transform::setWorldPosition(glm::vec3 worldPos) {
    if (m_parent == nullptr) {
        m_localPosition = worldPos;
    } else {
        const glm::mat4 invParent = glm::inverse(m_parent->getModelMatrix());
        m_localPosition = glm::vec3(invParent * glm::vec4(worldPos, 1.0F));
    }
    markDirty();
}

glm::vec3 Transform::getWorldPosition() const { return glm::vec3{getModelMatrix()[3]}; }

// ── World rotation ─────────────────────────────────────────────────────────────

void Transform::setWorldRotation(glm::quat worldRot) {
    m_localRotation =
        (m_parent == nullptr) ? worldRot : glm::inverse(m_parent->getWorldRotation()) * worldRot;
    markDirty();
}

glm::quat Transform::getWorldRotation() const {
    if (m_parent == nullptr) {
        return m_localRotation;
    }
    return m_parent->getWorldRotation() * m_localRotation;
}

// ── Model matrix ──────────────────────────────────────────────────────────────

const glm::mat4& Transform::getModelMatrix() const {
    if (m_dirty) {
        m_modelMatrix = parentWorldMatrix() * localMatrix();
        m_dirty = false;
    }
    return m_modelMatrix;
}

// ── Direction vectors ──────────────────────────────────────────────────────────

glm::vec3 Transform::forward() const { return getWorldRotation() * glm::vec3(0.0F, 0.0F, -1.0F); }

glm::vec3 Transform::up() const { return getWorldRotation() * glm::vec3(0.0F, 1.0F, 0.0F); }

glm::vec3 Transform::right() const { return getWorldRotation() * glm::vec3(1.0F, 0.0F, 0.0F); }

// ── Private helpers ───────────────────────────────────────────────────────────

void Transform::markDirty() {
    m_dirty = true;
    for (Transform* child : m_children) {
        child->markDirty();
    }
}

glm::mat4 Transform::localMatrix() const {
    glm::mat4 m = glm::mat4_cast(m_localRotation);
    m[0] *= m_localScale.x;
    m[1] *= m_localScale.y;
    m[2] *= m_localScale.z;
    m[3] = glm::vec4(m_localPosition, 1.0F);
    return m;
}

glm::mat4 Transform::parentWorldMatrix() const {
    if (m_parent == nullptr) {
        return glm::mat4(1.0F);
    }
    return m_parent->getModelMatrix();
}

} // namespace sonnet::scene
