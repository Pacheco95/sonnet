#define GLM_ENABLE_EXPERIMENTAL
#include "ViewportPanel.hpp"

#include <cmath>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <imgui.h>

namespace sonnet::editor {

static constexpr float kFovY = 60.0F;
static constexpr float kNearPlane = 0.1F;
static constexpr float kFarPlane = 1000.0F;

static constexpr std::array<glm::vec3, 3> kAxes{
    glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 1.0F}};

static constexpr std::array<ImU32, 3> kAxisColors{
    IM_COL32(220, 50, 50, 255), IM_COL32(50, 200, 50, 255), IM_COL32(50, 100, 220, 255)};

// Perpendicular basis vectors for each ring (X ring in YZ plane, Y ring in XZ, Z ring in XY)
static constexpr std::array<glm::vec3, 3> kRingPerp1{
    glm::vec3{0.0F, 1.0F, 0.0F}, glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{1.0F, 0.0F, 0.0F}};
static constexpr std::array<glm::vec3, 3> kRingPerp2{
    glm::vec3{0.0F, 0.0F, 1.0F},  // cross(X, Y) = +Z
    glm::vec3{0.0F, 0.0F, -1.0F}, // cross(Y, X) = -Z
    glm::vec3{0.0F, 1.0F, 0.0F}}; // cross(Z, X) = +Y

// Numeric constants
static constexpr float kOffScreenSentinel = -9999.0F;
static constexpr float kNdcHalf = 0.5F;
static constexpr float kNdcScale = 2.0F;
static constexpr float kDenomEps = 1e-6F;
static constexpr float kMinScaleValue = 0.01F;
static constexpr float kGizmoHitRadiusSq = 100.0F; // 10-pixel radius squared
static constexpr float kRingHitTolerance = 0.3F;
static constexpr float kBillHitRadius = 16.0F;
static constexpr float kGizmoLineThick = 2.5F;
static constexpr float kGizmoRingActiveThick = 3.5F;
static constexpr float kGizmoRingThick = 2.0F;
static constexpr float kGizmoHandleOuter = 9.0F;
static constexpr int kCircleSegsGizmo = 16;
static constexpr float kGizmoHandleInner = 6.0F;
static constexpr float kGizmoCenterDot = 5.0F;
static constexpr float kRingRenderSentinel = -8000.0F;
static constexpr float kBtnCornerRadius = 4.0F;
static constexpr float kBtnTextOffX = 8.0F;
static constexpr float kBtnTextOffY = 5.0F;
static constexpr int kSunCircleSegs = 32;
static constexpr float kSunSpokeThick = 2.0F;
static constexpr float kFwdProjectDist = 2.0F;
static constexpr float kArrowThick = 3.0F;
static constexpr float kArrowHeadLen = 8.0F;
static constexpr float kArrowHeadHalfW = 4.0F;
static constexpr float kScaleBoxHalf = 6.0F;
static constexpr float kScaleBoxOutlineOff = 2.0F;
static constexpr float kGizmoScaleFactor = 0.15F;
static constexpr float kMinGizmoScale = 0.5F;

ViewportPanel::ViewportPanel(uint64_t viewportTextureId, SceneContext& sceneCtx,
                             renderer::IRendererBackend& backend)
    : m_viewportTextureId(viewportTextureId), m_sceneCtx(sceneCtx), m_backend(backend) {}

const char* ViewportPanel::title() const { return "Viewport"; }

// ── Helpers ──────────────────────────────────────────────────────────────────

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ImVec2 ViewportPanel::worldToScreen(const glm::vec3& world, const glm::mat4& vp, ImVec2 panelMin,
                                    ImVec2 panelSize) {
    glm::vec4 clip = vp * glm::vec4(world, 1.0F);
    if (clip.w <= 0.0F) {
        return {kOffScreenSentinel, kOffScreenSentinel};
    }
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    return {panelMin.x + (((ndcX * kNdcHalf) + kNdcHalf) * panelSize.x),
            panelMin.y + (((ndcY * kNdcHalf) + kNdcHalf) * panelSize.y)};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
glm::vec3 ViewportPanel::mouseRay(ImVec2 mouse, ImVec2 panelMin, ImVec2 panelSize,
                                  const glm::mat4& vpInv, const glm::vec3& camPos) {
    float ndcX = (((mouse.x - panelMin.x) / panelSize.x) * kNdcScale) - 1.0F;
    float ndcY = (((mouse.y - panelMin.y) / panelSize.y) * kNdcScale) - 1.0F;
    glm::vec4 worldPos = vpInv * glm::vec4(ndcX, ndcY, kNdcHalf, 1.0F);
    worldPos /= worldPos.w;
    return glm::normalize(glm::vec3(worldPos) - camPos);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float ViewportPanel::axisRayParam(const glm::vec3& P, const glm::vec3& A, const glm::vec3& C,
                                  const glm::vec3& D) {
    // Closest point on axis ray (P+s*A) to camera ray (C+t*D); returns s.
    float b = glm::dot(A, D);
    float denom = 1.0F - (b * b);
    if (std::abs(denom) < kDenomEps) {
        return 0.0F;
    }
    float d = glm::dot(A, P - C);
    float e = glm::dot(D, P - C);
    return (e * b - d) / denom;
}

// ── Extracted interaction helpers ─────────────────────────────────────────────

bool ViewportPanel::handleGizmoButtonHitTest(ImVec2 contentMin) {
    if (!ImGui::IsWindowHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }
    constexpr float kBtnSize = 24.0F;
    constexpr float kPad = 4.0F;
    const std::array<GizmoMode, 3> modes{GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 bMin = {contentMin.x + kPad + (static_cast<float>(i) * (kBtnSize + kPad)),
                             contentMin.y + kPad};
        const ImVec2 bMax = {bMin.x + kBtnSize, bMin.y + kBtnSize};
        const ImVec2 mouse = ImGui::GetMousePos();
        if (mouse.x >= bMin.x && mouse.x <= bMax.x && mouse.y >= bMin.y && mouse.y <= bMax.y) {
            m_gizmoMode = modes[static_cast<std::size_t>(i)];
            return true;
        }
    }
    return false;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ViewportPanel::updateDragTranslateScale(ImVec2 contentMin, ImVec2 size, const glm::mat4& vp,
                                             bool& clickHandled, const glm::mat4& vpInv) {
    const uint32_t selId = m_sceneCtx.selectedId();
    scene::GameObject* selObj = (selId != 0) ? m_sceneCtx.findById(selId) : nullptr;
    const bool selIsMesh = (selObj != nullptr && !selObj->light.has_value());
    if (!selIsMesh || (m_gizmoMode != GizmoMode::Translate && m_gizmoMode != GizmoMode::Scale)) {
        return;
    }

    const glm::vec3 objPos = selObj->transform.getWorldPosition();
    const float gScale =
        std::max(glm::distance(m_camera.position(), objPos) * kGizmoScaleFactor, kMinGizmoScale);

    if (m_drag.active) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const glm::vec3 rayDir = mouseRay(mouse, contentMin, size, vpInv, m_camera.position());
            const float s =
                axisRayParam(m_drag.startPos, kAxes[static_cast<std::size_t>(m_drag.axis)],
                             m_camera.position(), rayDir);
            const float delta = s - m_drag.startS;
            if (m_gizmoMode == GizmoMode::Translate) {
                selObj->transform.setWorldPosition(
                    m_drag.startPos + kAxes[static_cast<std::size_t>(m_drag.axis)] * delta);
            } else {
                glm::vec3 newScale = m_drag.startScale;
                newScale[m_drag.axis] =
                    std::max(kMinScaleValue, m_drag.startScale[m_drag.axis] + delta);
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
            ImVec2 handleScreen = worldToScreen(
                objPos + kAxes[static_cast<std::size_t>(i)] * gScale, vp, contentMin, size);
            float dx = mouse.x - handleScreen.x;
            float dy = mouse.y - handleScreen.y;
            if ((dx * dx) + (dy * dy) <= kGizmoHitRadiusSq) {
                const glm::vec3 rayDir =
                    mouseRay(mouse, contentMin, size, vpInv, m_camera.position());
                m_drag.active = true;
                m_drag.axis = i;
                m_drag.startS = axisRayParam(objPos, kAxes[static_cast<std::size_t>(i)],
                                             m_camera.position(), rayDir);
                m_drag.startPos = objPos;
                m_drag.startScale = selObj->transform.getLocalScale();
                clickHandled = true;
                break;
            }
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ViewportPanel::updateDragRotate(ImVec2 contentMin, ImVec2 size, const glm::mat4& vpInv,
                                     bool& clickHandled) {
    const uint32_t selId = m_sceneCtx.selectedId();
    scene::GameObject* selObj = (selId != 0) ? m_sceneCtx.findById(selId) : nullptr;
    const bool selIsMesh = (selObj != nullptr && !selObj->light.has_value());
    if (!selIsMesh || m_gizmoMode != GizmoMode::Rotate) {
        return;
    }

    const glm::vec3 objPos = selObj->transform.getWorldPosition();
    const float gScale =
        std::max(glm::distance(m_camera.position(), objPos) * kGizmoScaleFactor, kMinGizmoScale);

    if (m_drag.active) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const glm::vec3 rayDir = mouseRay(mouse, contentMin, size, vpInv, m_camera.position());
            const glm::vec3& axis = kAxes[static_cast<std::size_t>(m_drag.axis)];
            const float denom = glm::dot(axis, rayDir);
            if (std::abs(denom) > kDenomEps) {
                const float t = glm::dot(axis, objPos - m_camera.position()) / denom;
                const glm::vec3 hitPt = m_camera.position() + rayDir * t;
                const glm::vec3 fromC = hitPt - objPos;
                const float a1 = glm::dot(fromC, kRingPerp1[static_cast<std::size_t>(m_drag.axis)]);
                const float a2 = glm::dot(fromC, kRingPerp2[static_cast<std::size_t>(m_drag.axis)]);
                const float angle = std::atan2(a2, a1);
                const float delta = angle - m_drag.startAngle;
                const glm::quat dRot = glm::angleAxis(delta, axis);
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
            const glm::vec3& axis = kAxes[static_cast<std::size_t>(i)];
            const glm::vec3 rayDir = mouseRay(mouse, contentMin, size, vpInv, m_camera.position());
            const float denom = glm::dot(axis, rayDir);
            if (std::abs(denom) < kDenomEps) {
                continue;
            }
            const float t = glm::dot(axis, objPos - m_camera.position()) / denom;
            if (t <= 0.0F) {
                continue;
            }
            const glm::vec3 hitPt = m_camera.position() + rayDir * t;
            const float dist = glm::length(hitPt - objPos);
            if (std::abs(dist - gScale) < gScale * kRingHitTolerance) {
                const glm::vec3 fromC = hitPt - objPos;
                m_drag.active = true;
                m_drag.axis = i;
                m_drag.startAngle =
                    std::atan2(glm::dot(fromC, kRingPerp2[static_cast<std::size_t>(i)]),
                               glm::dot(fromC, kRingPerp1[static_cast<std::size_t>(i)]));
                m_drag.startRot = selObj->transform.getWorldRotation();
                clickHandled = true;
                break;
            }
        }
    }
}

void ViewportPanel::drawGizmoModeButtons(ImVec2 contentMin) const {
    ImDrawList* overlayDl = ImGui::GetWindowDrawList();
    constexpr float kBtnSize = 24.0F;
    constexpr float kPad = 4.0F;
    const ImVec2 btnOrigin = {contentMin.x + kPad, contentMin.y + kPad};
    const std::array<const char*, 3> labels{"T", "R", "S"};
    const std::array<GizmoMode, 3> modes{GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 bMin = {btnOrigin.x + (static_cast<float>(i) * (kBtnSize + kPad)),
                             btnOrigin.y};
        const ImVec2 bMax = {bMin.x + kBtnSize, bMin.y + kBtnSize};
        const bool active = (m_gizmoMode == modes[static_cast<std::size_t>(i)]);
        const ImU32 bgCol = active ? IM_COL32(80, 160, 255, 220) : IM_COL32(40, 40, 40, 160);
        overlayDl->AddRectFilled(bMin, bMax, bgCol, kBtnCornerRadius);
        overlayDl->AddRect(bMin, bMax, IM_COL32(180, 180, 180, 200), kBtnCornerRadius);
        const ImVec2 textPos = {bMin.x + kBtnTextOffX, bMin.y + kBtnTextOffY};
        overlayDl->AddText(textPos, IM_COL32(255, 255, 255, 255),
                           labels[static_cast<std::size_t>(i)]);
    }
}

void ViewportPanel::updateGizmoModeHotkeys() {
    if (!ImGui::IsWindowFocused() || m_sceneCtx.selectedId() == 0) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W)) {
        m_gizmoMode = GizmoMode::Translate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E)) {
        m_gizmoMode = GizmoMode::Rotate;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        m_gizmoMode = GizmoMode::Scale;
    }
}

void ViewportPanel::handleGpuPicking(ImVec2 contentMin, ImVec2 size, bool& clickHandled) {
    if (clickHandled || !ImGui::IsWindowHovered() ||
        !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return;
    }
    const ImVec2 mouse = ImGui::GetMousePos();
    const int px = static_cast<int>(mouse.x - contentMin.x);
    const int py = static_cast<int>(mouse.y - contentMin.y);
    const glm::ivec2 offscreenSz = m_backend.getOffscreenSize();
    if (px < 0 || py < 0 || px >= static_cast<int>(size.x) || py >= static_cast<int>(size.y) ||
        offscreenSz.x <= 0 || offscreenSz.y <= 0) {
        return;
    }
    const int pxScaled =
        static_cast<int>(static_cast<float>(px) * static_cast<float>(offscreenSz.x) / size.x);
    const int pyScaled =
        static_cast<int>(static_cast<float>(py) * static_cast<float>(offscreenSz.y) / size.y);
    const int32_t id = m_backend.pick({pxScaled, pyScaled});
    if (id > 0) {
        m_sceneCtx.selectObject(static_cast<uint32_t>(id));
    } else {
        m_sceneCtx.deselectAll();
    }
    clickHandled = true;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void ViewportPanel::draw() {
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = size.x < 1.0F ? 1.0F : size.x;
    size.y = size.y < 1.0F ? 1.0F : size.y;

    const float aspect = size.x / size.y;
    if (aspect != m_lastAspect) {
        m_lastAspect = aspect;
        m_proj = glm::perspective(glm::radians(kFovY), aspect, kNearPlane, kFarPlane);
        m_proj[1][1] *= -1.0F;
    }

    if (ImGui::IsWindowHovered()) {
        m_camera.update(ImGui::GetIO().DeltaTime, ImGui::GetIO());
    }

    updateGizmoModeHotkeys();

    const glm::mat4 vp = m_proj * m_camera.viewMatrix();
    const glm::mat4 vpInv = glm::inverse(vp);
    bool clickHandled = handleGizmoButtonHitTest(contentMin);

    updateDragTranslateScale(contentMin, size, vp, clickHandled, vpInv);
    updateDragRotate(contentMin, size, vpInv, clickHandled);

    {
        const uint32_t lightId = m_sceneCtx.directionalLightId();
        if (lightId != 0 && !clickHandled && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            constexpr glm::vec3 kBillPos{0.0F, 3.0F, 0.0F};
            const ImVec2 screen = worldToScreen(kBillPos, vp, contentMin, size);
            const ImVec2 mouse = ImGui::GetMousePos();
            float dx = mouse.x - screen.x;
            float dy = mouse.y - screen.y;
            if ((dx * dx) + (dy * dy) <= (kBillHitRadius * kBillHitRadius)) {
                m_sceneCtx.selectObject(lightId);
                clickHandled = true;
            }
        }
    }

    handleGpuPicking(contentMin, size, clickHandled);

    const glm::ivec2 vpSize{static_cast<int>(size.x), static_cast<int>(size.y)};
    const auto desc =
        m_sceneCtx.buildRenderDesc(m_camera.viewMatrix(), m_proj, m_camera.position(), vpSize);
    m_backend.renderScene(desc);
    ImGui::Image(static_cast<ImTextureID>(m_viewportTextureId), size);

    const uint32_t selId = m_sceneCtx.selectedId();
    const scene::GameObject* selObj = (selId != 0) ? m_sceneCtx.findById(selId) : nullptr;
    const bool selIsMeshOverlay = (selObj != nullptr && !selObj->light.has_value());

    if (selIsMeshOverlay) {
        scene::GameObject* selObjMut = m_sceneCtx.findById(selId);
        if (m_gizmoMode == GizmoMode::Translate) {
            drawTranslateGizmo(contentMin, size, selObjMut, vp);
        } else if (m_gizmoMode == GizmoMode::Rotate) {
            drawRotateGizmo(contentMin, size, selObjMut, vp);
        } else {
            drawScaleGizmo(contentMin, size, selObjMut, vp);
        }
    }

    bool unused = false;
    drawLightBillboard(contentMin, size, vp, unused);
    drawGizmoModeButtons(contentMin);
}

// ── Translate gizmo ──────────────────────────────────────────────────────────

void ViewportPanel::drawTranslateGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                                       const glm::mat4& vp) {
    const glm::vec3 objPos = obj->transform.getWorldPosition();
    const float gScale =
        std::max(glm::distance(m_camera.position(), objPos) * kGizmoScaleFactor, kMinGizmoScale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = worldToScreen(objPos, vp, panelMin, size);

    for (int i = 0; i < 3; ++i) {
        const ImVec2 end =
            worldToScreen(objPos + kAxes[static_cast<std::size_t>(i)] * gScale, vp, panelMin, size);
        const ImU32 col = kAxisColors[static_cast<std::size_t>(i)];
        dl->AddLine(origin, end, col, kGizmoLineThick);
        if (m_drag.active && m_drag.axis == i) {
            dl->AddCircle(end, kGizmoHandleOuter, IM_COL32(255, 255, 100, 255), kCircleSegsGizmo,
                          kGizmoRingThick);
        }
        dl->AddCircleFilled(end, kGizmoHandleInner, col);
    }
    dl->AddCircleFilled(origin, kGizmoCenterDot, IM_COL32(255, 255, 255, 200));
}

// ── Rotate gizmo (visual rings, one per axis) ────────────────────────────────

void ViewportPanel::drawRotateGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                                    const glm::mat4& vp) {
    const glm::vec3 objPos = obj->transform.getWorldPosition();
    const float gScale =
        std::max(glm::distance(m_camera.position(), objPos) * kGizmoScaleFactor, kMinGizmoScale);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    constexpr int kSegs = 32;
    constexpr float kPi2 = 2.0F * std::numbers::pi_v<float>;

    for (int axis = 0; axis < 3; ++axis) {
        const bool isActive = (m_drag.active && m_drag.axis == axis);
        const float thickness = isActive ? kGizmoRingActiveThick : kGizmoRingThick;
        const ImU32 col =
            isActive ? IM_COL32(255, 255, 100, 255) : kAxisColors[static_cast<std::size_t>(axis)];
        ImVec2 prev{};
        for (int s = 0; s <= kSegs; ++s) {
            const float theta = (static_cast<float>(s) / static_cast<float>(kSegs)) * kPi2;
            const glm::vec3 worldPt =
                objPos + (kRingPerp1[static_cast<std::size_t>(axis)] * std::cos(theta) +
                          kRingPerp2[static_cast<std::size_t>(axis)] * std::sin(theta)) *
                             gScale;
            const ImVec2 screen = worldToScreen(worldPt, vp, panelMin, size);
            if (s > 0 && screen.x > kRingRenderSentinel && prev.x > kRingRenderSentinel) {
                dl->AddLine(prev, screen, col, thickness);
            }
            prev = screen;
        }
    }
    dl->AddCircleFilled(worldToScreen(objPos, vp, panelMin, size), kGizmoCenterDot,
                        IM_COL32(255, 255, 255, 200));
}

// ── Scale gizmo (axis lines + box handles) ───────────────────────────────────

void ViewportPanel::drawScaleGizmo(ImVec2 panelMin, ImVec2 size, scene::GameObject* obj,
                                   const glm::mat4& vp) {
    const glm::vec3 objPos = obj->transform.getWorldPosition();
    const float gScale =
        std::max(glm::distance(m_camera.position(), objPos) * kGizmoScaleFactor, kMinGizmoScale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = worldToScreen(objPos, vp, panelMin, size);

    for (int i = 0; i < 3; ++i) {
        const ImVec2 end =
            worldToScreen(objPos + kAxes[static_cast<std::size_t>(i)] * gScale, vp, panelMin, size);
        const ImU32 col = kAxisColors[static_cast<std::size_t>(i)];
        dl->AddLine(origin, end, col, kGizmoLineThick);
        if (m_drag.active && m_drag.axis == i) {
            dl->AddRect({end.x - kScaleBoxHalf - kScaleBoxOutlineOff,
                         end.y - kScaleBoxHalf - kScaleBoxOutlineOff},
                        {end.x + kScaleBoxHalf + kScaleBoxOutlineOff,
                         end.y + kScaleBoxHalf + kScaleBoxOutlineOff},
                        IM_COL32(255, 255, 100, 255), 0.0F, 0, kGizmoRingThick);
        }
        dl->AddRectFilled({end.x - kScaleBoxHalf, end.y - kScaleBoxHalf},
                          {end.x + kScaleBoxHalf, end.y + kScaleBoxHalf}, col);
    }
    dl->AddCircleFilled(origin, kGizmoCenterDot, IM_COL32(255, 255, 255, 200));
}

// ── Directional light billboard + direction arrow ────────────────────────────

void ViewportPanel::drawLightBillboard(ImVec2 panelMin, ImVec2 size, const glm::mat4& vp,
                                       bool& clickHandled) {
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
    if (screen.x < kRingRenderSentinel) {
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool isSelected = (m_sceneCtx.selectedId() == lightId);
    const ImU32 col = isSelected ? IM_COL32(255, 240, 50, 255) : IM_COL32(255, 220, 80, 220);

    constexpr float kRadius = 10.0F;
    constexpr float kSpokeLen = 16.0F;
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0F * kPi;
    constexpr int kSpokes = 8;

    dl->AddCircle(screen, kRadius, col, kSunCircleSegs, kSunSpokeThick);
    for (int i = 0; i < kSpokes; ++i) {
        float angle = (static_cast<float>(i) / static_cast<float>(kSpokes)) * kTwoPi;
        ImVec2 inner{screen.x + (kRadius * std::cos(angle)),
                     screen.y + (kRadius * std::sin(angle))};
        ImVec2 outer{screen.x + (kSpokeLen * std::cos(angle)),
                     screen.y + (kSpokeLen * std::sin(angle))};
        dl->AddLine(inner, outer, col, kSunSpokeThick);
    }

    const glm::vec3 fwd = lightObj->transform.forward();
    const ImVec2 arrowEnd = worldToScreen(kBillPos + fwd * kFwdProjectDist, vp, panelMin, size);
    if (arrowEnd.x > kRingRenderSentinel) {
        constexpr ImU32 kArrowCol = IM_COL32(255, 200, 50, 200);
        dl->AddLine(screen, arrowEnd, kArrowCol, kArrowThick);
        float dx = arrowEnd.x - screen.x;
        float dy = arrowEnd.y - screen.y;
        float len = std::sqrt((dx * dx) + (dy * dy));
        if (len > 1.0F) {
            dx /= len;
            dy /= len;
            ImVec2 perp{-dy, dx};
            ImVec2 b1{arrowEnd.x - (dx * kArrowHeadLen) + (perp.x * kArrowHeadHalfW),
                      arrowEnd.y - (dy * kArrowHeadLen) + (perp.y * kArrowHeadHalfW)};
            ImVec2 b2{arrowEnd.x - (dx * kArrowHeadLen) - (perp.x * kArrowHeadHalfW),
                      arrowEnd.y - (dy * kArrowHeadLen) - (perp.y * kArrowHeadHalfW)};
            dl->AddTriangleFilled(arrowEnd, b1, b2, kArrowCol);
        }
    }

    if (!clickHandled && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        float dx = mouse.x - screen.x;
        float dy = mouse.y - screen.y;
        if ((dx * dx) + (dy * dy) <= (kSpokeLen * kSpokeLen)) {
            m_sceneCtx.selectObject(lightId);
            clickHandled = true;
        }
    }
}

} // namespace sonnet::editor
