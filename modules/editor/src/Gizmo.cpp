#include <sonnet/editor/Gizmo.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sonnet::editor {

namespace {

constexpr float HandleSizeFraction = 0.18f; // of the distance to the camera
constexpr float PickDistancePixels = 8.0f;
constexpr int CircleSegments = 48;
constexpr float MinimumScale = 0.01f;

float pointSegmentDistance(glm::vec2 point, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 ab = b - a;
  const float lengthSquared = glm::dot(ab, ab);
  if (lengthSquared < 1e-6f) {
    return glm::length(point - a);
  }
  const float t = std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f);
  return glm::length(point - (a + ab * t));
}

// Two directions spanning the plane perpendicular to `axis`.
void planeBasis(glm::vec3 axis, glm::vec3 &u, glm::vec3 &v) {
  const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
  u = glm::normalize(glm::cross(axis, helper));
  v = glm::cross(axis, u);
}

float angleInPlane(glm::vec3 point, glm::vec3 origin, glm::vec3 axis) {
  glm::vec3 u;
  glm::vec3 v;
  planeBasis(axis, u, v);
  const glm::vec3 d = point - origin;
  return std::atan2(glm::dot(d, v), glm::dot(d, u));
}

ImU32 axisColor(GizmoAxis axis, bool highlighted) {
  switch (axis) {
  case GizmoAxis::X:
    return highlighted ? IM_COL32(255, 130, 130, 255) : IM_COL32(225, 60, 60, 255);
  case GizmoAxis::Y:
    return highlighted ? IM_COL32(150, 255, 150, 255) : IM_COL32(70, 200, 70, 255);
  case GizmoAxis::Z:
    return highlighted ? IM_COL32(150, 170, 255, 255) : IM_COL32(70, 110, 255, 255);
  case GizmoAxis::None:
    break;
  }
  return IM_COL32(220, 220, 220, 255);
}

} // namespace

glm::vec3 Gizmo::rayDirection(const GizmoView &view, glm::vec2 pixel) {
  const glm::vec2 relative = (pixel - view.origin) / glm::max(view.size, glm::vec2{1.0f});
  // Clip-space Y points up on screen (the negative-height viewport undoes Vulkan's flip).
  const glm::vec2 ndc{relative.x * 2.0f - 1.0f, 1.0f - relative.y * 2.0f};
  const glm::mat4 inverse = glm::inverse(view.projection * view.view);
  // Reversed-Z: depth 1 is the near plane; 0 is at infinity, so a point between them gives the
  // direction.
  const glm::vec4 nearPoint = inverse * glm::vec4{ndc, 1.0f, 1.0f};
  const glm::vec4 farPoint = inverse * glm::vec4{ndc, 0.5f, 1.0f};
  const glm::vec3 a = glm::vec3{nearPoint} / nearPoint.w;
  const glm::vec3 b = glm::vec3{farPoint} / farPoint.w;
  return glm::normalize(b - a);
}

std::optional<glm::vec2> Gizmo::project(const GizmoView &view, glm::vec3 point) {
  const glm::vec4 clip = view.projection * view.view * glm::vec4{point, 1.0f};
  if (clip.w <= 1e-5f) {
    return std::nullopt; // behind the camera
  }
  const glm::vec3 ndc = glm::vec3{clip} / clip.w;
  return glm::vec2{view.origin.x + (ndc.x * 0.5f + 0.5f) * view.size.x,
                   view.origin.y + (0.5f - ndc.y * 0.5f) * view.size.y};
}

float Gizmo::axisRayParam(glm::vec3 origin, glm::vec3 axis, glm::vec3 rayOrigin, glm::vec3 rayDirection) {
  // Closest points of two lines: origin + s * axis and rayOrigin + t * rayDirection.
  const glm::vec3 w = origin - rayOrigin;
  const float a = glm::dot(axis, axis);
  const float b = glm::dot(axis, rayDirection);
  const float c = glm::dot(rayDirection, rayDirection);
  const float d = glm::dot(axis, w);
  const float e = glm::dot(rayDirection, w);
  const float denominator = a * c - b * b;
  if (std::abs(denominator) < 1e-6f) {
    return 0.0f; // the axis points at the camera; no useful parameter
  }
  return (b * e - c * d) / denominator;
}

std::optional<glm::vec3> Gizmo::planeRayHit(glm::vec3 origin, glm::vec3 normal, glm::vec3 rayOrigin,
                                            glm::vec3 rayDirection) {
  const float denominator = glm::dot(normal, rayDirection);
  if (std::abs(denominator) < 1e-4f) {
    return std::nullopt;
  }
  const float t = glm::dot(normal, origin - rayOrigin) / denominator;
  if (t < 0.0f) {
    return std::nullopt;
  }
  return rayOrigin + rayDirection * t;
}

GizmoAxis Gizmo::hitTest(const GizmoView &view, glm::vec3 origin, const glm::vec3 (&axes)[3], float length) const {
  const auto originPixel = project(view, origin);
  if (!originPixel) {
    return GizmoAxis::None;
  }
  GizmoAxis best = GizmoAxis::None;
  float bestDistance = PickDistancePixels;
  for (int i = 0; i < 3; ++i) {
    const auto axis = static_cast<GizmoAxis>(i + 1);
    float distance = std::numeric_limits<float>::max();
    if (m_mode == GizmoMode::Rotate) {
      glm::vec3 u;
      glm::vec3 v;
      planeBasis(axes[i], u, v);
      std::optional<glm::vec2> previous;
      for (int segment = 0; segment <= CircleSegments; ++segment) {
        const float angle = static_cast<float>(segment) / CircleSegments * 2.0f * std::numbers::pi_v<float>;
        const auto pixel = project(view, origin + (u * std::cos(angle) + v * std::sin(angle)) * length);
        if (pixel && previous) {
          distance = std::min(distance, pointSegmentDistance(view.mouse, *previous, *pixel));
        }
        previous = pixel;
      }
    } else if (const auto tip = project(view, origin + axes[i] * length)) {
      distance = pointSegmentDistance(view.mouse, *originPixel, *tip);
    }
    if (distance < bestDistance) {
      bestDistance = distance;
      best = axis;
    }
  }
  return best;
}

GizmoResult Gizmo::update(const GizmoView &view, world::World &world, flecs::entity entity, ImDrawList *drawList) {
  GizmoResult result;
  if (!entity || !entity.is_alive() || !entity.has<world::Transform>()) {
    m_hover = GizmoAxis::None;
    m_drag.reset();
    return result;
  }
  const flecs::entity parent = world.parentOf(entity);
  const world::WorldTransform *parentTransform = parent ? parent.try_get<world::WorldTransform>() : nullptr;
  const glm::mat4 parentWorld = parentTransform != nullptr ? parentTransform->matrix : glm::mat4{1.0f};
  // From the local transform as it is now, not the world matrix of the last transform system
  // run, so the handles sit on the object even in the frame that moves it.
  const glm::mat4 worldMatrix = parentWorld * entity.get<world::Transform>().matrix();

  // The handles are drawn and hit-tested where the object is; the drag maths below anchors at
  // the drag's start position instead, so a handle never chases what it moves.
  glm::vec3 origin{worldMatrix[3]};
  const float length = m_drag ? m_drag->length : glm::distance(view.cameraPosition, origin) * HandleSizeFraction;
  glm::vec3 axes[3];
  for (int i = 0; i < 3; ++i) {
    axes[i] = m_mode == GizmoMode::Scale ? glm::normalize(glm::vec3{worldMatrix[i]}) : glm::vec3{glm::mat4{1.0f}[i]};
  }

  const glm::vec3 ray = rayDirection(view, view.mouse);
  if (!m_drag) {
    m_hover = hitTest(view, origin, axes, length);
    if (view.mouseClicked && m_hover != GizmoAxis::None) {
      const int index = static_cast<int>(m_hover) - 1;
      Drag drag{.axis = m_hover,
                .startLocal = entity.get<world::Transform>(),
                .parentWorld = parentWorld,
                .startPosition = origin,
                .startWorldRotation = world::Transform::fromMatrix(worldMatrix).rotation,
                .axisDirection = axes[index],
                .startParam = 0.0f,
                .startAngle = 0.0f,
                .length = length};
      if (m_mode == GizmoMode::Rotate) {
        const auto hit = planeRayHit(origin, drag.axisDirection, view.cameraPosition, ray);
        drag.startAngle = hit ? angleInPlane(*hit, origin, drag.axisDirection) : 0.0f;
      } else {
        drag.startParam = axisRayParam(origin, drag.axisDirection, view.cameraPosition, ray);
      }
      m_drag = drag;
    }
  }

  if (m_drag) {
    result.active = true;
    if (view.mouseDown) {
      const Drag &drag = *m_drag;
      const int index = static_cast<int>(drag.axis) - 1;
      world::Transform local = drag.startLocal;
      switch (m_mode) {
      case GizmoMode::Translate: {
        // Always from the drag's start position: a moving origin makes the object oscillate.
        const float param = axisRayParam(drag.startPosition, drag.axisDirection, view.cameraPosition, ray);
        const glm::vec3 worldPosition = drag.startPosition + drag.axisDirection * (param - drag.startParam);
        local.position = glm::vec3{glm::inverse(drag.parentWorld) * glm::vec4{worldPosition, 1.0f}};
        break;
      }
      case GizmoMode::Rotate: {
        if (const auto hit = planeRayHit(drag.startPosition, drag.axisDirection, view.cameraPosition, ray)) {
          const float delta = angleInPlane(*hit, drag.startPosition, drag.axisDirection) - drag.startAngle;
          const glm::quat worldRotation = glm::angleAxis(delta, drag.axisDirection) * drag.startWorldRotation;
          const glm::quat parentRotation = world::Transform::fromMatrix(drag.parentWorld).rotation;
          local.rotation = glm::normalize(glm::inverse(parentRotation) * worldRotation);
        }
        break;
      }
      case GizmoMode::Scale: {
        const float param = axisRayParam(drag.startPosition, drag.axisDirection, view.cameraPosition, ray);
        const float factor = 1.0f + (param - drag.startParam) / drag.length;
        local.scale[index] = std::max(drag.startLocal.scale[index] * factor, MinimumScale);
        break;
      }
      }
      entity.set<world::Transform>(local);
      origin = glm::vec3{(drag.parentWorld * local.matrix())[3]}; // the handles follow this frame's move
    } else {
      result.finished = true;
      result.before = m_drag->startLocal;
      m_drag.reset();
      m_hover = hitTest(view, origin, axes, length);
    }
  }
  result.hovered = m_hover != GizmoAxis::None;
  m_origin = origin;
  if (drawList != nullptr) {
    draw(drawList, view, origin, axes, length, m_drag ? m_drag->axis : m_hover);
  }
  return result;
}

void Gizmo::draw(ImDrawList *drawList, const GizmoView &view, glm::vec3 origin, const glm::vec3 (&axes)[3],
                 float length, GizmoAxis highlighted) const {
  const auto originPixel = project(view, origin);
  if (!originPixel) {
    return;
  }
  const ImVec2 centre{originPixel->x, originPixel->y};
  for (int i = 0; i < 3; ++i) {
    const auto axis = static_cast<GizmoAxis>(i + 1);
    const ImU32 color = axisColor(axis, axis == highlighted);
    if (m_mode == GizmoMode::Rotate) {
      glm::vec3 u;
      glm::vec3 v;
      planeBasis(axes[i], u, v);
      std::optional<ImVec2> previous;
      for (int segment = 0; segment <= CircleSegments; ++segment) {
        const float angle = static_cast<float>(segment) / CircleSegments * 2.0f * std::numbers::pi_v<float>;
        const auto pixel = project(view, origin + (u * std::cos(angle) + v * std::sin(angle)) * length);
        if (!pixel) {
          previous.reset();
          continue;
        }
        const ImVec2 current{pixel->x, pixel->y};
        if (previous) {
          drawList->AddLine(*previous, current, color, 2.0f);
        }
        previous = current;
      }
      continue;
    }
    const auto tip = project(view, origin + axes[i] * length);
    if (!tip) {
      continue;
    }
    const ImVec2 end{tip->x, tip->y};
    drawList->AddLine(centre, end, color, 2.5f);
    const glm::vec2 direction = *tip - *originPixel;
    const float shaft = glm::length(direction);
    if (shaft < 1.0f) {
      continue;
    }
    const glm::vec2 forward = direction / shaft;
    const glm::vec2 side{-forward.y, forward.x};
    if (m_mode == GizmoMode::Translate) {
      const glm::vec2 base = *tip - forward * 12.0f;
      const glm::vec2 left = base + side * 5.0f;
      const glm::vec2 right = base - side * 5.0f;
      drawList->AddTriangleFilled(end, ImVec2{left.x, left.y}, ImVec2{right.x, right.y}, color);
    } else {
      drawList->AddRectFilled(ImVec2{end.x - 5.0f, end.y - 5.0f}, ImVec2{end.x + 5.0f, end.y + 5.0f}, color);
    }
  }
  drawList->AddCircleFilled(centre, 4.0f, IM_COL32(230, 230, 230, 220));
}

} // namespace sonnet::editor
