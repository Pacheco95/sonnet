#pragma once

#include <sonnet/renderer/Camera.h>
#include <sonnet/renderer/SceneView.h>

#include <span>
#include <vector>

namespace sonnet::editor {

// How far past the near plane a camera's frustum is drawn. The projection has no far plane, so the
// gizmo stops here.
constexpr float CameraGizmoLength = 2.0f;

// Appends each camera's view frustum at `aspect`: a rectangle on the near plane, another
// `CameraGizmoLength` past it, and four lines from the camera's position to the far rectangle's
// corners, through the near one's.
void appendCameraFrustumLines(std::span<const renderer::Camera> cameras, float aspect,
                              std::vector<renderer::DebugLine> &lines);

} // namespace sonnet::editor
