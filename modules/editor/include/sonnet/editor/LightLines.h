#pragma once

#include <sonnet/renderer/SceneView.h>

#include <span>
#include <vector>

namespace sonnet::editor {

// Appends a wireframe cone for every spot light in `lights`: the apex at the light, the axis along
// its direction, the outer ring at `range` with radius `range * tan(outerAngle)`, a dimmer inner
// ring, and a few lines from the apex to the outer ring. Other light types draw nothing.
void appendSpotLightLines(std::span<const renderer::Light> lights, std::vector<renderer::DebugLine> &lines);

} // namespace sonnet::editor
