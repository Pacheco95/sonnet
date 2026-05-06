#define GLM_ENABLE_EXPERIMENTAL
#include "ViewportPanel.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <imgui.h>

namespace sonnet::editor {

static constexpr float kFovY      = 60.0F;
static constexpr float kNearPlane = 0.1F;
static constexpr float kFarPlane  = 1000.0F;

static constexpr std::array<glm::vec3, 3> kAxes{
    glm::vec3{1.0F, 0.0F, 0.0F},
    glm::vec3{0.0F, 1.0F, 0.0F},
    glm::vec3{0.0F, 0.0F, 1.0F}};

static constexpr std::array<ImU32, 3> kAxisColors{
    IM_COL32(220, 50,  50,  255),
    IM_COL32(50,  200, 50,  255),
    IM_COL32(50,  100, 220, 255)};

// Perpendicular basis vectors for each ring (X ring in YZ plane, Y ring in XZ, Z ring in XY)
static constexpr std::array<glm::vec3, 3> kRingPerp1{
    glm::vec3{0.0F, 1.0F, 0.0F},
    glm::vec3{1.0F, 0.0F, 0.0F},
    glm::vec3{1.0F, 0.0F, 0.0F}};
static constexpr std::array<glm::vec3, 3> kRingPerp2{
    glm::vec3{ 0.0F, 0.0F,  1.0F},  // cross(X, Y) = +Z
    glm::vec3{ 0.0F, 0.0F, -1.0F},  // cross(Y, X) = -Z
    glm::vec3{ 0.0F, 1.0F,  0.0F}}; // cross(Z, X) = +Y

ViewportPanel::ViewportPanel(uint64_t viewportTextureId, SceneContext& sceneCtx,
                             renderer::IRendererBackend& backend)
    : m_viewportTextureId(viewportTextureId), m_sceneCtx(sceneCtx), m_backend(backend) {}

const char* ViewportPanel::title() const { return "Viewport"; }

// ── Helpers ──────────────────────────────────────────────────────────────────

ImVec2 ViewportPanel::worldToScreen(const glm::vec3& world, const glm::mat4& vp,
                                     ImVec2 panelMin, ImVec2 panelSize) {
    glm::vec4 clip = vp * glm::vec4(world, 1.0F);
    if (clip.w <= 0.0F) {
        return {-9999.0F, -9999.0F};
    }
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    return {panelMin.x + ((ndcX * 0.5F) + 0.5F) * panelSize.x,
            panelMin.y + ((ndcY * 0.5F) + 0.5F) * panelSize.y};
}

glm::vec3 ViewportPanel::mouseRay(ImVec2 mouse, ImVec2 panelMin, ImVec2 panelSize,
                                   const glm::mat4& vpInv, const glm::vec3& camPos) {
    float ndcX = ((mouse.x - panelMin.x) / panelSize.x) * 2.0F - 1.0F;
    float ndcY = ((mouse.y - panelMin.y) / panelSize.y) * 2.0F - 1.0F;
    glm::vec4 worldPos = vpInv * glm::vec4(ndcX, ndcY, 0.5F, 1.0F);
    worldPos /= worldPos.w;
    return glm::normalize(glm::vec3(worldPos) - camPos);
}

float ViewportPanel::axisRayParam(const glm::vec3& P, const glm::vec3& A,
                                   const glm::vec3& C, const glm::vec3& D) {
    // Closest point on axis ray (P+s*A) to camera ray (C+t*D); returns s.
    float b     = glm::dot(A, D);
    float denom = 1.0F - b * b;
    if (std::abs(denom) < 1e-6F) {
        return 0.0F;
    }
    float d = glm::dot(A, P - C);
    float e = glm::dot(D, P - C);
    return (e * b - d) / denom;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void ViewportPanel::draw() {
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    ImVec2       size       = ImGui::GetContentRegionAvail();
    size.x = size.x < 1.0F ? 1.0F : size.x;
    size.y = size.y < 1.0F ? 1.0F : size.y;

    const float aspect = size.x / size.y;
    if (aspect != m_lastAspect) {
        m_lastAspect = aspect;
        m_proj       = glm::perspective(glm::radians(kFovY), aspect, kNearPlane, kFarPlane);
        m_proj[1][1] *= -1.0F;
    }

    if (ImGui::IsWindowHovered()) {
        m_camera.update(ImGui::GetIO().DeltaTime, ImGui::GetIO());
    }

    // W/E/R gizmo mode switching
    if (ImGui::IsWindowFocused() && m_sceneCtx.selectedId() != 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) { m_gizmoMode = GizmoMode::Translate; }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) { m_gizmoMode = GizmoMode::Rotate; }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) { m_gizmoMode = GizmoMode::Scale; }
    }

    const glm::mat4 vp    = m_proj * m_camera.viewMatrix();
    const glm::mat4 vpInv = glm::inverse(vp);
    bool clickHandled = false;

    // ── T/R/S button hit-test (before GPU pick so clicks don't deselect) ────────
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        constexpr float kBtnSize = 24.0F;
        constexpr float kPad     = 4.0F;
        const GizmoMode modes[]  = {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
        for (int i = 0; i < 3; ++i) {
            const ImVec2 bMin = {contentMin.x + kPad + static_cast<float>(i) * (kBtnSize + kPad),
                                 contentMin.y + kPad};
            const ImVec2 bMax = {bMin.x + kBtnSize, bMin.y + kBtnSize};
            const ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x >= bMin.x && mouse.x <= bMax.x &&
                mouse.y >= bMin.y && mouse.y <= bMax.y) {
                m_gizmoMode  = modes[i];
                clickHandled = true;
                break;
            }
        }
    }

    // ── Gizmo drag continuation & handle hit-test ─────────────────────────────
    uint32_t       selId  = m_sceneCtx.selectedId();
    scene::GameObject* selObj = (selId != 0) ? m_sceneCtx.findById(selId) : nullptr;

    // Only show/interact with gizmo for mesh objects, not lights
    const bool selIsMesh = (selObj != nullptr && !selObj->light.has_value());

    if (selIsMesh && (m_gizmoMode == GizmoMode::Translate || m_gizmoMode == GizmoMode::Scale)) {
        const glm::vec3 objPos   = selObj->transform.getWorldPosition();
        const float     gScale   = std::max(glm::distance(m_camera.position(), objPos) * 0.15F, 0.5F);

        if (m_drag.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2    mouse  = ImGui::GetMousePos();
                const glm::vec3 rayDir = mouseRay(mouse, contentMin, size, vpInv,
                                                   m_camera.position());
                const float s     = axisRayParam(m_drag.startPos, kAxes[m_drag.axis],
                                                  m_camera.position(), rayDir);
                const float delta = s - m_drag.startS;
                if (m_gizmoMode == GizmoMode::Translate) {
                    selObj->transform.setWorldPosition(m_drag.startPos +
                                                       kAxes[m_drag.axis] * delta);
                } else {
                    glm::vec3 newScale = m_drag.startScale;
                    newScale[m_drag.axis] = std::max(0.01F, m_drag.startScale[m_drag.axis] + delta);
                    selObj->transform.setLocalScale(newScale);
                }
                clickHandled = true;
            } else {
                m_drag.active = false;
            }
        }

        if (!m_drag.active && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            for (int i = 0; i < 3; ++i) {
                ImVec2 handleScreen =
                    worldToScreen(objPos + kAxes[i] * gScale, vp, contentMin, size);
                float dx = mouse.x - handleScreen.x;
                float dy = mouse.y - handleScreen.y;
                if (dx * dx + dy * dy <= 100.0F) {  // 10 px radius
                    const glm::vec3 rayDir =
                        mouseRay(mouse, contentMin, size, vpInv, m_camera.position());
                    m_drag.active      = true;
                    m_drag.axis        = i;
                    m_drag.startS      = axisRayParam(objPos, kAxes[i],
                                                       m_camera.position(), rayDir);
                    m_drag.startPos    = objPos;
                    m_drag.startScale  = selObj->transform.getLocalScale();
                    clickHandled       = true;
                    break;
                }
            }
        }
    }

    // ── Rotate gizmo drag ─────────────────────────────────────────────────────
    if (selIsMesh && m_gizmoMode == GizmoMode::Rotate) {
        const glm::vec3 objPos = selObj->transform.getWorldPosition();
        const float     gScale = std::max(glm::distance(m_camera.position(), objPos) * 0.15F, 0.5F);

        if (m_drag.active) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2    mouse  = ImGui::GetMousePos();
                const glm::vec3 rayDir = mouseRay(mouse, contentMin, size, vpInv,
                                                   m_camera.position());
                const glm::vec3& axis  = kAxes[m_drag.axis];
                const float denom = glm::dot(axis, rayDir);
                if (std::abs(denom) > 1e-6F) {
                    const float      t      = glm::dot(axis, objPos - m_camera.position()) / denom;
                    const glm::vec3  hitPt  = m_camera.position() + rayDir * t;
                    const glm::vec3  fromC  = hitPt - objPos;
                    const float      a1     = glm::dot(fromC, kRingPerp1[m_drag.axis]);
                    const float      a2     = glm::dot(fromC, kRingPerp2[m_drag.axis]);
                    const float      angle  = std::atan2(a2, a1);
                    const float      delta  = angle - m_drag.startAngle;
                    const glm::quat  dRot   = glm::angleAxis(delta, axis);
                    selObj->transform.setWorldRotation(dRot * m_drag.startRot);
                }
                clickHandled = true;
            } else {
                m_drag.active = false;
            }
        }

        if (!m_drag.active && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            for (int i = 0; i < 3; ++i) {
                const glm::vec3& axis  = kAxes[i];
                const glm::vec3  rayDir = mouseRay(mouse, contentMin, size, vpInv,
                                                    m_camera.position());
                const float denom = glm::dot(axis, rayDir);
                if (std::abs(denom) < 1e-6F) { continue; }
                const float     t     = glm::dot(axis, objPos - m_camera.position()) / denom;
                if (t <= 0.0F) { continue; }
                const glm::vec3 hitPt = m_camera.position() + rayDir * t;
                const float     dist  = glm::length(hitPt - objPos);
                if (std::abs(dist - gScale) < gScale * 0.3F) {
                    const glm::vec3 fromC = hitPt - objPos;
                    m_drag.active     = true;
                    m_drag.axis       = i;
                    m_drag.startAngle = std::atan2(glm::dot(fromC, kRingPerp2[i]),
                                                   glm::dot(fromC, kRingPerp1[i]));
                    m_drag.startRot   = selObj->transform.getWorldRotation();
                    clickHandled      = true;
                    break;
                }
            }
        }
    }

    // ── Light billboard click (before GPU pick) ───────────────────────────────
    {
        const uint32_t lightId = m_sceneCtx.directionalLightId();
        if (lightId != 0 && !clickHandled && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            constexpr glm::vec3 kBillPos{0.0F, 3.0F, 0.0F};
            const ImVec2 screen = worldToScreen(kBillPos, vp, contentMin, size);
            const ImVec2 mouse  = ImGui::GetMousePos();
            float dx = mouse.x - screen.x;
            float dy = mouse.y - screen.y;
            if (dx * dx + dy * dy <= 16.0F * 16.0F) {
                m_sceneCtx.selectObject(lightId);
                clickHandled = true;
            }
        }
    }

    // ── GPU picking ───────────────────────────────────────────────────────────
    if (!clickHandled && ImGui::IsWindowHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2      mouse        = ImGui::GetMousePos();
        const int         px           = static_cast<int>(mouse.x - contentMin.x);
        const int         py           = static_cast<int>(mouse.y - contentMin.y);
        const glm::ivec2  offscreenSz  = m_backend.getOffscreenSize();
        if (px >= 0 && py >= 0 && px < static_cast<int>(size.x) &&
            py < static_cast<int>(size.y) && offscreenSz.x > 0 && offscreenSz.y > 0) {
            const int pxScaled = static_cast<int>(px * offscreenSz.x / size.x);
            const int pyScaled = static_cast<int>(py * offscreenSz.y / size.y);
            const int32_t id = m_backend.pick({pxScaled, pyScaled});
            if (id > 0) {
                m_sceneCtx.selectObject(static_cast<uint32_t>(id));
            } else {
                m_sceneCtx.deselectAll();
            }
        }
    }

    // ── Render scene ─────────────────────────────────────────────────────────
    const glm::ivec2 vpSize{static_cast<int>(size.x), static_cast<int>(size.y)};
    const auto desc = m_sceneCtx.buildRenderDesc(m_camera.viewMatrix(), m_proj,
                                                  m_camera.position(), vpSize);
    m_backend.renderScene(desc);
    ImGui::Image(static_cast<ImTextureID>(m_viewportTextureId), size);

    // ── Overlays ─────────────────────────────────────────────────────────────
    // Re-fetch selObj in case a pick just changed it
    selId  = m_sceneCtx.selectedId();
    selObj = (selId != 0) ? m_sceneCtx.findById(selId) : nullptr;
    const bool selIsMeshOverlay = (selObj != nullptr && !selObj->light.has_value());

    if (selIsMeshOverlay) {
        if (m_gizmoMode == GizmoMode::Translate) {
            drawTranslateGizmo(contentMin, size, selObj, vp);
        } else if (m_gizmoMode == GizmoMode::Rotate) {
            drawRotateGizmo(contentMin, size, selObj, vp);
        } else {
            drawScaleGizmo(contentMin, size, selObj, vp);
        }
    }

    bool unused = false;
    drawLightBillboard(contentMin, size, vp, unused);

    // ── T/R/S mode buttons ────────────────────────────────────────────────────
    ImDrawList* overlayDl = ImGui::GetWindowDrawList();
    constexpr float kBtnSize  = 24.0F;
    constexpr float kPad      = 4.0F;
    const ImVec2    btnOrigin = {contentMin.x + kPad, contentMin.y + kPad};
    const char*     labels[]  = {"T", "R", "S"};
    const GizmoMode modes[]   = {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 bMin = {btnOrigin.x + static_cast<float>(i) * (kBtnSize + kPad), btnOrigin.y};
        const ImVec2 bMax = {bMin.x + kBtnSize, bMin.y + kBtnSize};
        const bool   active = (m_gizmoMode == modes[i]);
        const ImU32  bgCol  = active ? IM_COL32(80, 160, 255, 220) : IM_COL32(40, 40, 40, 160);
        overlayDl->AddRectFilled(bMin, bMax, bgCol, 4.0F);
        overlayDl->AddRect(bMin, bMax, IM_COL32(180, 180, 180, 200), 4.0F);
        const ImVec2 textPos = {bMin.x + 8.0F, bMin.y + 5.0F};
        overlayDl->AddText(textPos, IM_COL32(255, 255, 255, 255), labels[i]);
    }
}

// ── Translate gizmo ──────────────────────────────────────────────────────────

void ViewportPanel::drawTranslateGizmo(ImVec2 panelMin, ImVec2 size,
                                        scene::GameObject* obj, const glm::mat4& vp) {
    const glm::vec3 objPos = obj->transform.getWorldPosition();
    const float gScale = std::max(glm::distance(m_camera.position(), objPos) * 0.15F, 0.5F);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = worldToScreen(objPos, vp, panelMin, size);

    for (int i = 0; i < 3; ++i) {
        const ImVec2 end = worldToScreen(objPos + kAxes[i] * gScale, vp, panelMin, size);
        const ImU32  col = kAxisColors[i];
        dl->AddLine(origin, end, col, 2.5F);
        if (m_drag.active && m_drag.axis == i) {
            dl->AddCircle(end, 9.0F, IM_COL32(255, 255, 100, 255), 16, 2.0F);
        }
        dl->AddCircleFilled(end, 6.0F, col);
    }
    dl->AddCircleFilled(origin, 5.0F, IM_COL32(255, 255, 255, 200));
}

// ── Rotate gizmo (visual rings, one per axis) ────────────────────────────────

void ViewportPanel::drawRotateGizmo(ImVec2 panelMin, ImVec2 size,
                                     scene::GameObject* obj, const glm::mat4& vp) {
    const glm::vec3 objPos  = obj->transform.getWorldPosition();
    const float     gScale  = std::max(glm::distance(m_camera.position(), objPos) * 0.15F, 0.5F);
    ImDrawList*     dl      = ImGui::GetWindowDrawList();

    constexpr int   kSegs = 32;
    constexpr float kPi2  = 6.28318530F;

    for (int axis = 0; axis < 3; ++axis) {
        const bool  isActive = (m_drag.active && m_drag.axis == axis);
        const float thickness = isActive ? 3.5F : 2.0F;
        const ImU32 col = isActive ? IM_COL32(255, 255, 100, 255) : kAxisColors[axis];
        ImVec2 prev{};
        for (int s = 0; s <= kSegs; ++s) {
            const float theta = (static_cast<float>(s) / static_cast<float>(kSegs)) * kPi2;
            const glm::vec3 worldPt = objPos +
                (kRingPerp1[axis] * std::cos(theta) + kRingPerp2[axis] * std::sin(theta)) * gScale;
            const ImVec2 screen = worldToScreen(worldPt, vp, panelMin, size);
            if (s > 0 && screen.x > -8000.0F && prev.x > -8000.0F) {
                dl->AddLine(prev, screen, col, thickness);
            }
            prev = screen;
        }
    }
    dl->AddCircleFilled(worldToScreen(objPos, vp, panelMin, size), 5.0F,
                        IM_COL32(255, 255, 255, 200));
}

// ── Scale gizmo (axis lines + box handles) ───────────────────────────────────

void ViewportPanel::drawScaleGizmo(ImVec2 panelMin, ImVec2 size,
                                    scene::GameObject* obj, const glm::mat4& vp) {
    const glm::vec3 objPos  = obj->transform.getWorldPosition();
    const float     gScale  = std::max(glm::distance(m_camera.position(), objPos) * 0.15F, 0.5F);
    ImDrawList*     dl      = ImGui::GetWindowDrawList();
    const ImVec2    origin  = worldToScreen(objPos, vp, panelMin, size);

    for (int i = 0; i < 3; ++i) {
        const ImVec2 end = worldToScreen(objPos + kAxes[i] * gScale, vp, panelMin, size);
        const ImU32  col = kAxisColors[i];
        dl->AddLine(origin, end, col, 2.5F);
        // Box handle
        constexpr float kBox = 6.0F;
        if (m_drag.active && m_drag.axis == i) {
            dl->AddRect({end.x - kBox - 2, end.y - kBox - 2},
                        {end.x + kBox + 2, end.y + kBox + 2},
                        IM_COL32(255, 255, 100, 255), 0.0F, 0, 2.0F);
        }
        dl->AddRectFilled({end.x - kBox, end.y - kBox},
                          {end.x + kBox, end.y + kBox}, col);
    }
    dl->AddCircleFilled(origin, 5.0F, IM_COL32(255, 255, 255, 200));
}

// ── Directional light billboard + direction arrow ────────────────────────────

void ViewportPanel::drawLightBillboard(ImVec2 panelMin, ImVec2 size,
                                        const glm::mat4& vp, bool& clickHandled) {
    const uint32_t lightId = m_sceneCtx.directionalLightId();
    if (lightId == 0) {
        return;
    }
    const auto* lightObj = m_sceneCtx.findById(lightId);
    if (lightObj == nullptr) {
        return;
    }

    constexpr glm::vec3 kBillPos{0.0F, 3.0F, 0.0F};
    const ImVec2 screen = worldToScreen(kBillPos, vp, panelMin, size);
    if (screen.x < -8000.0F) {
        return;
    }

    ImDrawList*  dl         = ImGui::GetWindowDrawList();
    const bool   isSelected = (m_sceneCtx.selectedId() == lightId);
    const ImU32  col = isSelected ? IM_COL32(255, 240, 50, 255) : IM_COL32(255, 220, 80, 220);

    constexpr float kRadius   = 10.0F;
    constexpr float kSpokeLen = 16.0F;
    constexpr float kPi       = 3.14159265F;
    constexpr int   kSpokes   = 8;

    dl->AddCircle(screen, kRadius, col, 32, 2.0F);
    for (int i = 0; i < kSpokes; ++i) {
        float angle = (static_cast<float>(i) / static_cast<float>(kSpokes)) * 2.0F * kPi;
        ImVec2 inner{screen.x + kRadius   * std::cos(angle),
                     screen.y + kRadius   * std::sin(angle)};
        ImVec2 outer{screen.x + kSpokeLen * std::cos(angle),
                     screen.y + kSpokeLen * std::sin(angle)};
        dl->AddLine(inner, outer, col, 2.0F);
    }

    // Direction arrow: project kBillPos + forward to screen
    const glm::vec3 fwd      = lightObj->transform.forward();
    const ImVec2    arrowEnd = worldToScreen(kBillPos + fwd * 1.5F, vp, panelMin, size);
    if (arrowEnd.x > -8000.0F) {
        constexpr ImU32 kArrowCol = IM_COL32(255, 200, 50, 200);
        dl->AddLine(screen, arrowEnd, kArrowCol, 3.0F);
        // Arrowhead triangle
        float dx = arrowEnd.x - screen.x;
        float dy = arrowEnd.y - screen.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0F) {
            dx /= len;
            dy /= len;
            ImVec2 perp{-dy, dx};
            ImVec2 b1{arrowEnd.x - dx * 8.0F + perp.x * 4.0F,
                      arrowEnd.y - dy * 8.0F + perp.y * 4.0F};
            ImVec2 b2{arrowEnd.x - dx * 8.0F - perp.x * 4.0F,
                      arrowEnd.y - dy * 8.0F - perp.y * 4.0F};
            dl->AddTriangleFilled(arrowEnd, b1, b2, kArrowCol);
        }
    }

    // Click to select (only when not already handled)
    if (!clickHandled && ImGui::IsWindowHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        float dx = mouse.x - screen.x;
        float dy = mouse.y - screen.y;
        if (dx * dx + dy * dy <= kSpokeLen * kSpokeLen) {
            m_sceneCtx.selectObject(lightId);
            clickHandled = true;
        }
    }
}

} // namespace sonnet::editor
