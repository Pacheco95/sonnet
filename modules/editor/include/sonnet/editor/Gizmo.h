#pragma once

#include <sonnet/world/Components.h>
#include <sonnet/world/World.h>

#include <sonnet/core/Math.h>

#include <cstdint>
#include <optional>

struct ImDrawList;

namespace sonnet::editor {

enum class GizmoMode : std::uint8_t {
  Translate,
  Rotate,
  Scale,
};

enum class GizmoAxis : std::uint8_t {
  None,
  X,
  Y,
  Z,
};

// What the gizmo needs from the viewport for one frame. Pixels are screen coordinates, the
// viewport's rectangle included, so the maths works without Dear ImGui in tests.
struct GizmoView {
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  glm::vec3 cameraPosition{0.0f};
  glm::vec2 origin{0.0f}; // the viewport's top-left corner
  glm::vec2 size{1.0f};
  glm::vec2 mouse{0.0f};
  bool mouseDown{false};
  bool mouseClicked{false};
};

struct GizmoResult {
  bool hovered{false};
  bool active{false};
  bool finished{false};      // the drag ended this frame: the caller records the command
  world::Transform before{}; // the local transform when the drag started, valid with finished
};

// Translate, rotate and scale handles over the primary selection (docs/editor.md, "Gizmos").
// Translation and rotation work along the world axes, scale along the entity's own. The drag
// resolves the mouse ray against the axis or the rotation plane from the drag's fixed start
// position, so the handle never chases the object it moves.
class Gizmo {
public:
  void setMode(GizmoMode mode) noexcept {
    m_mode = mode;
  }
  [[nodiscard]] GizmoMode mode() const noexcept {
    return m_mode;
  }
  [[nodiscard]] bool isDragging() const noexcept {
    return m_drag.has_value();
  }
  [[nodiscard]] GizmoAxis hoveredAxis() const noexcept {
    return m_hover;
  }

  // Handles this frame's interaction with `entity`'s transform and draws the handles into
  // `drawList` when one is given.
  GizmoResult update(const GizmoView &view, world::World &world, flecs::entity entity, ImDrawList *drawList);

  // Camera-space maths, shared with the tests.
  [[nodiscard]] static glm::vec3 rayDirection(const GizmoView &view, glm::vec2 pixel);
  [[nodiscard]] static std::optional<glm::vec2> project(const GizmoView &view, glm::vec3 point);
  // The parameter along `axis` from `origin` of the point closest to the ray.
  [[nodiscard]] static float axisRayParam(glm::vec3 origin, glm::vec3 axis, glm::vec3 rayOrigin,
                                          glm::vec3 rayDirection);
  [[nodiscard]] static std::optional<glm::vec3> planeRayHit(glm::vec3 origin, glm::vec3 normal, glm::vec3 rayOrigin,
                                                            glm::vec3 rayDirection);

private:
  struct Drag {
    GizmoAxis axis{GizmoAxis::None};
    world::Transform startLocal;
    glm::mat4 parentWorld{1.0f};
    glm::vec3 startPosition{0.0f}; // world
    glm::quat startWorldRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 axisDirection{0.0f};
    float startParam{0.0f};
    float startAngle{0.0f};
    float length{1.0f};
  };

  [[nodiscard]] GizmoAxis hitTest(const GizmoView &view, glm::vec3 origin, const glm::vec3 (&axes)[3],
                                  float length) const;
  void draw(ImDrawList *drawList, const GizmoView &view, glm::vec3 origin, const glm::vec3 (&axes)[3], float length,
            GizmoAxis highlighted) const;

  GizmoMode m_mode{GizmoMode::Translate};
  GizmoAxis m_hover{GizmoAxis::None};
  std::optional<Drag> m_drag;
};

} // namespace sonnet::editor
