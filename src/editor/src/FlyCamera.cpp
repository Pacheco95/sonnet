#include "FlyCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace sonnet::editor {

static constexpr float kPitchLimit = 1.5F; // radians, just under pi/2

void FlyCamera::update(float dt, const ImGuiIO& io) {
    // Mouse look: only when right mouse button is held
    if (io.MouseDown[1]) { // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        m_yaw   += io.MouseDelta.x * kMouseSensitivity;
        m_pitch += io.MouseDelta.y * kMouseSensitivity;
        m_pitch = std::clamp(m_pitch, -kPitchLimit, kPitchLimit);
    }

    // Build forward/right vectors from yaw+pitch
    const float cy = std::cos(m_yaw);
    const float sy = std::sin(m_yaw);
    const float cp = std::cos(m_pitch);
    const float sp = std::sin(m_pitch);

    const glm::vec3 forward{ sy * cp,  -sp, -cy * cp };
    const glm::vec3 right  { cy,        0.0F,   sy   };

    // WASD movement (only when right mouse held, matching Blender-style fly mode)
    if (io.MouseDown[1]) { // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        const float speed = dt * kMoveSpeed;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { m_position += forward * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { m_position -= forward * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { m_position += right   * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { m_position -= right   * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_E)) { m_position.y += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) { m_position.y -= speed; }
    }
}

glm::mat4 FlyCamera::viewMatrix() const {
    const float cy = std::cos(m_yaw);
    const float sy = std::sin(m_yaw);
    const float cp = std::cos(m_pitch);
    const float sp = std::sin(m_pitch);

    const glm::vec3 forward{ sy * cp, -sp, -cy * cp };
    return glm::lookAt(m_position, m_position + forward, glm::vec3{0.0F, 1.0F, 0.0F});
}

} // namespace sonnet::editor
