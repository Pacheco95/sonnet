#define GLM_ENABLE_EXPERIMENTAL
#include "InspectorPanel.hpp"

#include <cfloat>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

#include <imgui.h>

namespace sonnet::editor {

static constexpr float kHalf = 0.5F;
static constexpr float kDragSpeedTranslate = 0.1F;
static constexpr float kDragSpeedScale = 0.1F;
static constexpr float kDragSpeedIntensity = 0.01F;

InspectorPanel::InspectorPanel(SceneContext& sceneCtx) : m_sceneCtx(sceneCtx) {}

const char* InspectorPanel::title() const { return "Inspector"; }

void InspectorPanel::draw() {
    const uint32_t sel = m_sceneCtx.selectedId();
    if (sel == 0) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos({avail.x * kHalf - ImGui::CalcTextSize("No object selected").x * kHalf,
                             avail.y * kHalf});
        ImGui::TextDisabled("No object selected");
        return;
    }

    auto* obj = m_sceneCtx.findById(sel);
    if (obj == nullptr) {
        ImGui::TextDisabled("No object selected");
        return;
    }

    ImGui::SeparatorText(obj->name.c_str());
    ImGui::Spacing();

    // ── Transform ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Position (world space)
        glm::vec3 pos = obj->transform.getWorldPosition();
        if (ImGui::DragFloat3("Position", glm::value_ptr(pos), kDragSpeedTranslate)) {
            obj->transform.setWorldPosition(pos);
        }

        // Rotation (world space, displayed as Euler degrees)
        const glm::quat worldRot = obj->transform.getWorldRotation();
        glm::vec3 eulerDeg = glm::degrees(glm::eulerAngles(worldRot));
        if (ImGui::DragFloat3("Rotation", glm::value_ptr(eulerDeg), 1.0F)) {
            obj->transform.setWorldRotation(glm::quat(glm::radians(eulerDeg)));
        }

        // Scale (local space)
        glm::vec3 scale = obj->transform.getLocalScale();
        if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), kDragSpeedScale)) {
            obj->transform.setLocalScale(scale);
        }
    }

    // ── Light (if applicable) ─────────────────────────────────────────────
    if (obj->light.has_value()) {
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& ld = obj->light.value();
            ImGui::ColorEdit3("Color", glm::value_ptr(ld.color));
            ImGui::DragFloat("Intensity", &ld.intensity, kDragSpeedIntensity, 0.0F, FLT_MAX);
        }
    }
}

} // namespace sonnet::editor
