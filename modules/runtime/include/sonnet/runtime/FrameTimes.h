#pragma once

#include <sonnet/platform/Thermal.h>
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
  // The system's thermal state, sampled by the caller about once a second. The summary ends with
  // the first state, the last and the worst seen: "thermal nominal -> serious (worst serious)".
  // Unknown states never count as worst, and a run that saw only Unknown says "thermal n/a".
  void recordThermal(platform::ThermalState state);
  [[nodiscard]] std::size_t count() const noexcept {
    return m_frames.size();
  }
  // Means over the frames recorded, oldest gone first: "over the last 100 frames: CPU 2.10 ms a
  // frame, 16.67 ms apart; GPU 3.05 ms: shadow cascade 0 0.20 ms, ...". A pass is averaged over
  // available samples of frames that ran it, in the order of the latest frame. A pass without
  // samples and a total missing any pass are n/a; otherwise the total is their sum.
  [[nodiscard]] std::string summary() const;

private:
  struct Pass {
    std::string name;
    float gpuMilliseconds;
    bool gpuAvailable;
  };
  struct Frame {
    float cpuMilliseconds;
    float intervalMilliseconds;
    std::vector<Pass> passes;
  };

  bool m_thermalSeen{false};
  platform::ThermalState m_thermalFirst{platform::ThermalState::Unknown};
  platform::ThermalState m_thermalLast{platform::ThermalState::Unknown};
  platform::ThermalState m_thermalWorst{platform::ThermalState::Unknown};

  std::vector<Frame> m_frames; // a ring of Capacity once full
  std::size_t m_next{0};       // where the next frame goes once full
};

} // namespace sonnet::runtime
