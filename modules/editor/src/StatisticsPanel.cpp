#include <sonnet/editor/StatisticsPanel.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <numeric>

namespace sonnet::editor {

namespace {

std::string megabytes(std::uint64_t bytes) {
  return std::format("{:.0f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

} // namespace

void StatisticsPanel::record(const FrameStatistics &statistics) {
  m_current = statistics;
  m_history[m_historyIndex] = statistics.frameMilliseconds;
  m_historyIndex = (m_historyIndex + 1) % HistorySize;
  m_historyCount = std::min(m_historyCount + 1, HistorySize);
}

float StatisticsPanel::averageFrameMilliseconds() const noexcept {
  if (m_historyCount == 0) {
    return 0.0f;
  }
  const float sum =
      std::accumulate(m_history.begin(), m_history.begin() + static_cast<std::ptrdiff_t>(m_historyCount), 0.0f);
  return sum / static_cast<float>(m_historyCount);
}

void StatisticsPanel::drawWindow(bool &open) {
  if (ImGui::Begin("Statistics", &open)) {
    drawContents(false);
  }
  ImGui::End();
}

void StatisticsPanel::drawOverlay() {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0f, 0.0f, 0.0f, 0.6f});
  if (ImGui::BeginChild("statistics overlay", ImVec2{0.0f, 0.0f},
                        ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                            ImGuiChildFlags_AlwaysUseWindowPadding,
                        ImGuiWindowFlags_NoInputs)) {
    drawContents(true);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void StatisticsPanel::drawContents(bool compact) {
  const float average = averageFrameMilliseconds();
  ImGui::Text("%.2f ms  (%.0f fps)", static_cast<double>(average),
              average > 0.0f ? static_cast<double>(1000.0f / average) : 0.0);
  if (!compact) {
    // Oldest sample first: the ring starts at the write index.
    ImGui::PlotLines(
        "##frames",
        [](void *data, int index) {
          auto *panel = static_cast<const StatisticsPanel *>(data);
          const std::size_t i = (panel->m_historyIndex + static_cast<std::size_t>(index)) % HistorySize;
          return panel->m_history[i];
        },
        this, static_cast<int>(HistorySize), 0, nullptr, 0.0f, std::max(33.0f, average * 2.0f), ImVec2{-1.0f, 60.0f});
  }

  ImGui::Text("%u draws, %u triangles", m_current.renderer.drawCount, m_current.renderer.triangleCount);
  if (m_current.renderer.skinnedInstanceCount > 0) {
    ImGui::Text("%u skinned, %u vertices", m_current.renderer.skinnedInstanceCount,
                m_current.renderer.skinnedVertexCount);
  }
  if (m_current.graph != nullptr) {
    if (ImGui::BeginTable("passes", 3, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("pass");
      ImGui::TableSetupColumn("cpu ms");
      ImGui::TableSetupColumn("gpu ms");
      if (!compact) {
        ImGui::TableHeadersRow();
      }
      for (const renderer::PassTiming &pass : m_current.graph->passes) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(pass.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", static_cast<double>(pass.cpuMilliseconds));
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", static_cast<double>(pass.gpuMilliseconds));
      }
      ImGui::EndTable();
    }
    if (!compact) {
      ImGui::Text("%u barriers, %u transient images (%s)", m_current.graph->barrierCount,
                  m_current.graph->transientImageCount, megabytes(m_current.graph->transientImageBytes).c_str());
    }
  }

  for (std::uint32_t i = 0; i < m_current.memory.heapCount; ++i) {
    const rhi::HeapBudget &heap = m_current.memory.heaps[i];
    if (compact && !heap.deviceLocal) {
      continue;
    }
    const float fraction =
        heap.budget > 0 ? static_cast<float>(static_cast<double>(heap.usage) / static_cast<double>(heap.budget)) : 0.0f;
    const std::string label =
        std::format("{} {} / {}", heap.deviceLocal ? "GPU" : "host", megabytes(heap.usage), megabytes(heap.budget));
    ImGui::ProgressBar(fraction, ImVec2{compact ? 220.0f : -1.0f, 0.0f}, label.c_str());
  }
}

} // namespace sonnet::editor
