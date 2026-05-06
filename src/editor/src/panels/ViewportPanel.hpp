#pragma once

#include "../FlyCamera.hpp"
#include "../SceneContext.hpp"

#include <sonnet/editor/IPanel.hpp>
#include <sonnet/renderer/IRendererBackend.hpp>

#include <glm/glm.hpp>
#include <imgui.h>

#include <array>
#include <cstdint>

namespace sonnet::editor {

enum class GizmoMode { Translate, Rotate, Scale };

class ViewportPanel : public IPanel {
public:
    ViewportPanel(uint64_t viewportTextureId, SceneContext& sceneCtx,
                  renderer::IRendererBackend& backend);

    [[nodiscard]] const char* title() const override;
    [[nodiscard]] bool isPermanent() const override { return true; }
    void draw() override;

private:
    struct DragState {
        bool      active{false};
        int       axis{-1};
        float     startS{0.0F};
        glm::vec3 startPos{0.0F};
        glm::vec3 startScale{1.0F};
        float     startAngle{0.0F};
        glm::quat startRot{1.0F, 0.0F, 0.0F, 0.0F};
    };

    static ImVec2 worldToScreen(const glm::vec3& world, const glm::mat4& vp,
                                 ImVec2 panelMin, ImVec2 panelSize);
    static glm::vec3 mouseRay(ImVec2 mouse, ImVec2 panelMin, ImVec2 panelSize,
                               const glm::mat4& vpInv, const glm::vec3& camPos);
    static float axisRayParam(const glm::vec3& P, const glm::vec3& A,
                               const glm::vec3& C, const glm::vec3& D);

    void drawTranslateGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                             const glm::mat4& vp);
    void drawRotateGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                          const glm::mat4& vp);
    void drawScaleGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                         const glm::mat4& vp);
    void drawLightBillboard(ImVec2 panelMin, ImVec2 size, const glm::mat4& vp,
                             bool& clickHandled);

    uint64_t                    m_viewportTextureId;
    SceneContext&               m_sceneCtx;
    renderer::IRendererBackend& m_backend;
    FlyCamera                   m_camera;
    glm::mat4                   m_proj{1.0F};
    float                       m_lastAspect{1.0F};
    GizmoMode                   m_gizmoMode{GizmoMode::Translate};
    DragState                   m_drag;
};

} // namespace sonnet::editor
