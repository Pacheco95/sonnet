#pragma once

#include <sonnet/renderer/Camera.h>

#include <sonnet/core/Math.h>

namespace sonnet::editor {

// The viewport camera: mouse look plus WASD for the horizontal plane and Q/E for down and up
// (docs/roadmap.md, M1). Angles are radians (docs/conventions.md).
class FlyCamera {
public:
  struct Input {
    glm::vec2 lookDelta{0.0f, 0.0f}; // pixels this frame, +x right, +y down
    bool forward{false};
    bool back{false};
    bool left{false};
    bool right{false};
    bool up{false};
    bool down{false};
    bool fast{false};
  };

  FlyCamera();

  void update(float dt, const Input &input);
  // Places the camera so that it looks at `target` from `position`.
  void lookAt(glm::vec3 position, glm::vec3 target);
  // Multiplies the movement speed, e.g. from the mouse wheel; clamped to a sane range.
  void scaleSpeed(float factor);
  // Moves along the view direction by `notches` wheel steps (positive is forward). A step is a
  // tenth of `referenceDistance`, the distance to what the camera looks at, so a close-up creeps
  // and a wide shot leaps; without a reference (<= 0) it is a fifth of the fly speed. `fast`
  // multiplies the step like the keys' Shift.
  void dolly(float notches, float referenceDistance, bool fast);

  [[nodiscard]] const renderer::Camera &camera() const noexcept {
    return m_camera;
  }
  [[nodiscard]] float speed() const noexcept {
    return m_speed;
  }
  [[nodiscard]] float yaw() const noexcept {
    return m_yaw;
  }
  [[nodiscard]] float pitch() const noexcept {
    return m_pitch;
  }

private:
  void applyOrientation();

  renderer::Camera m_camera;
  float m_yaw{0.0f};            // around +Y, 0 looks down -Z
  float m_pitch{0.0f};          // around the camera's X, positive looks up
  float m_speed{5.0f};          // metres per second
  float m_sensitivity{0.0035f}; // radians per pixel
};

} // namespace sonnet::editor
