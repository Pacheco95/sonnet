#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct ImGuiIO;

namespace sonnet::editor {

// WASD + mouse-drag fly camera. update() must be called every frame while the
// viewport is hovered; uses ImGuiIO for delta time and input state.
class FlyCamera {
public:
    FlyCamera() = default;

    void update(float dt, const ImGuiIO& io);

    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::vec3 position()   const { return m_position; }

    // Expose for inspector/serialisation if needed later.
    void setPosition(glm::vec3 pos) { m_position = pos; }
    void setYawPitch(float yaw, float pitch) { m_yaw = yaw; m_pitch = pitch; }

private:
    glm::vec3 m_position{0.0F, 3.0F, 5.0F};
    float     m_yaw{0.0F};      // radians, rotates around world Y
    float     m_pitch{-0.5F};   // radians, slight downward tilt by default

    static constexpr float kMoveSpeed    = 5.0F;
    static constexpr float kMouseSensitivity = 0.003F;
};

} // namespace sonnet::editor
