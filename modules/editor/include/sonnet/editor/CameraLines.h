#pragma once

#include <sonnet/renderer/Camera.h>
#include <sonnet/renderer/SceneView.h>

#include <span>
#include <vector>

namespace sonnet::editor {

// How far past the near plane a camera gizmo is drawn, keeping it compact for either projection.
constexpr float CameraGizmoLength = 2.0f;

// Appends each camera's view frustum at `aspect`: a rectangle on the near plane, another
// `CameraGizmoLength` past it, and four lines from the camera (perspective) or near corners
// (orthographic) to the far corners.
void appendCameraFrustumLines(std::span<const renderer::Camera> cameras, float aspect,
                              std::vector<renderer::DebugLine> &lines);

} // namespace sonnet::editor
