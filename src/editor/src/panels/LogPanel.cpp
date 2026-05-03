#include "LogPanel.hpp"

#include <imgui.h>

#include <chrono>
#include <ctime>
#include <mutex>

namespace sonnet::editor {

LogPanel::LogPanel(std::shared_ptr<LogBuffer> buffer) : m_buffer(std::move(buffer)) {}

const char* LogPanel::title() const {
    return "Log";
}

void LogPanel::draw() {
    // Filter toggles
    ImGui::PushStyleColor(ImGuiCol_Button,
                          m_filter.showInfo ? ImVec4(0.2F, 0.5F, 0.8F, 1.0F)
                                            : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button("Info")) {
        m_filter.showInfo = !m_filter.showInfo;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          m_filter.showWarnings ? ImVec4(0.8F, 0.7F, 0.1F, 1.0F)
                                                : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button("Warnings")) {
        m_filter.showWarnings = !m_filter.showWarnings;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          m_filter.showErrors ? ImVec4(0.8F, 0.2F, 0.2F, 1.0F)
                                              : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button("Errors")) {
        m_filter.showErrors = !m_filter.showErrors;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    if (ImGui::Button("Clear")) {
        m_buffer->clear();
    }

    ImGui::Separator();

    // Scrollable log region
    ImGui::BeginChild("##log", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    {
        std::lock_guard lock(m_buffer->mutex);

        for (const auto& entry : m_buffer->entries) {
            bool show = false;
            switch (entry.severity) {
            case LogSeverity::Info:
                show = m_filter.showInfo;
                break;
            case LogSeverity::Warning:
                show = m_filter.showWarnings;
                break;
            case LogSeverity::Error:
                show = m_filter.showErrors;
                break;
            }
            if (!show) {
                continue;
            }

            switch (entry.severity) {
            case LogSeverity::Warning:
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.85F, 0.0F, 1.0F));
                break;
            case LogSeverity::Error:
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.3F, 0.3F, 1.0F));
                break;
            default:
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
                break;
            }

            ImGui::TextUnformatted(entry.message.c_str());
            ImGui::PopStyleColor();
        }
    }

    // Auto-scroll logic
    float scrollY = ImGui::GetScrollY();
    float scrollMaxY = ImGui::GetScrollMaxY();
    if (scrollMaxY > 0.0F && scrollY < scrollMaxY - 1.0F) {
        m_autoScroll = false;
    }
    if (scrollMaxY > 0.0F && scrollY >= scrollMaxY - 1.0F) {
        m_autoScroll = true;
    }
    if (m_autoScroll) {
        ImGui::SetScrollHereY(1.0F);
    }

    ImGui::EndChild();
}

} // namespace sonnet::editor
