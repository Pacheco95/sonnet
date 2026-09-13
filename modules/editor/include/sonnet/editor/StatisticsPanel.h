#pragma once

#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Types.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace sonnet::editor {

// What one frame cost, gathered by the editor from the graph, the renderer and the device.
struct FrameStatistics {
  float frameMilliseconds{0.0f};
  const renderer::GraphStatistics *graph{nullptr};
  renderer::RenderStatistics renderer;
  rhi::MemoryBudget memory;
};

// Frame time history, per-pass CPU and GPU times, draw and triangle counts and the VMA budget
// per heap (docs/roadmap.md, M1). Drawn either as a dockable window or as an overlay in the
// viewport's corner.
class StatisticsPanel {
public:
  static constexpr std::size_t HistorySize = 120;

  void record(const FrameStatistics &statistics);
  void drawWindow(bool &open);
  // Inside another window: an opaque box at the current cursor position.
  void drawOverlay();

  [[nodiscard]] float averageFrameMilliseconds() const noexcept;

private:
  void drawContents(bool compact);

  FrameStatistics m_current;
  std::array<float, HistorySize> m_history{};
  std::size_t m_historyIndex{0};
  std::size_t m_historyCount{0};
};

} // namespace sonnet::editor
