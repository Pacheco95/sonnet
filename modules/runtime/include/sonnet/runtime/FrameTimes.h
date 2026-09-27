#pragma once

#include <sonnet/renderer/RenderGraph.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sonnet::runtime {

// The last hundred frames' times, which a player's capture run logs as it ends (docs/player.md,
// "Capture runs"), so a device's run reports what a frame costs there beside the image.
class FrameTimes {
public:
  static constexpr std::size_t Capacity = 100;

  // One frame: the CPU's time for it, the time since the frame before, and the graph's statistics,
  // whose GPU times are those of an earlier frame (docs/rendering.md, "Render graph").
  void record(float cpuMilliseconds, float intervalMilliseconds, const renderer::GraphStatistics &graph);
  [[nodiscard]] std::size_t count() const noexcept {
    return m_frames.size();
  }
  // Means over the frames recorded, oldest gone first: "over the last 100 frames: CPU 2.10 ms a
  // frame, 16.67 ms apart; GPU 3.05 ms: shadow cascade 0 0.20 ms, ...". A pass is averaged over
  // the frames that ran it, in the order of the latest frame, and the GPU total is their sum.
  [[nodiscard]] std::string summary() const;

private:
  struct Pass {
    std::string name;
    float gpuMilliseconds;
  };
  struct Frame {
    float cpuMilliseconds;
    float intervalMilliseconds;
    std::vector<Pass> passes;
  };

  std::vector<Frame> m_frames; // a ring of Capacity once full
  std::size_t m_next{0};       // where the next frame goes once full
};

} // namespace sonnet::runtime
