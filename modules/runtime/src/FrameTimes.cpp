#include <sonnet/runtime/FrameTimes.h>

#include <algorithm>
#include <format>

namespace sonnet::runtime {

void FrameTimes::record(float cpuMilliseconds, float intervalMilliseconds, const renderer::GraphStatistics &graph) {
  Frame frame{.cpuMilliseconds = cpuMilliseconds, .intervalMilliseconds = intervalMilliseconds, .passes = {}};
  frame.passes.reserve(graph.passes.size());
  for (const renderer::PassTiming &pass : graph.passes) {
    frame.passes.push_back(
        {.name = pass.name, .gpuMilliseconds = pass.gpuMilliseconds, .gpuAvailable = pass.gpuAvailable});
  }
  if (m_frames.size() < Capacity) {
    m_frames.push_back(std::move(frame));
    return;
  }
  m_frames[m_next] = std::move(frame);
  m_next = (m_next + 1) % Capacity;
}

std::string FrameTimes::summary() const {
  if (m_frames.empty()) {
    return "no frames recorded";
  }
  float cpu = 0.0f;
  float interval = 0.0f;
  for (const Frame &frame : m_frames) {
    cpu += frame.cpuMilliseconds;
    interval += frame.intervalMilliseconds;
  }
  const auto frames = static_cast<float>(m_frames.size());
  // The latest frame's passes, in its order; a pass another frame ran and it did not is left out.
  const Frame &latest = m_frames[(m_next + m_frames.size() - 1) % m_frames.size()];
  std::string passes;
  float gpu = 0.0f;
  bool complete = true;
  for (auto current = latest.passes.begin(); current != latest.passes.end(); ++current) {
    const Pass &pass = *current;
    // A name twice in a frame is averaged once, from its first pass.
    if (std::ranges::find(latest.passes.begin(), current, pass.name, &Pass::name) != current) {
      continue;
    }
    float sum = 0.0f;
    int ran = 0;
    for (const Frame &frame : m_frames) {
      const auto found = std::ranges::find(frame.passes, pass.name, &Pass::name);
      if (found != frame.passes.end() && found->gpuAvailable) {
        sum += found->gpuMilliseconds;
        ++ran;
      }
    }
    if (ran == 0) {
      complete = false;
      passes += std::format("{}{} n/a", passes.empty() ? "" : ", ", pass.name);
      continue;
    }
    const float mean = sum / static_cast<float>(ran);
    gpu += mean;
    passes += std::format("{}{} {:.3f} ms", passes.empty() ? "" : ", ", pass.name, mean);
  }
  return std::format("over the last {} frames: CPU {:.2f} ms a frame, {:.2f} ms apart; GPU {}: {}", m_frames.size(),
                     cpu / frames, interval / frames, complete ? std::format("{:.3f} ms", gpu) : "n/a", passes);
}

} // namespace sonnet::runtime
