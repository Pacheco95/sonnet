#include "LayoutManager.hpp"

#include <sonnet/logging/Logger.hpp>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sonnet::editor {

LayoutManager::LayoutManager(std::filesystem::path layoutsDir)
    : m_layoutsDir(std::move(layoutsDir)) {
    std::error_code errCode;
    std::filesystem::create_directories(m_layoutsDir, errCode);
}

bool LayoutManager::isValidName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    return std::ranges::all_of(name, [](char ch) { return ch != '/' && ch != '\\'; });
}

std::vector<std::string> LayoutManager::listLayouts() const {
    std::vector<std::string> names;
    std::error_code errCode;
    for (const auto& entry : std::filesystem::directory_iterator(m_layoutsDir, errCode)) {
        if (entry.path().extension() == ".ini") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool LayoutManager::saveLayout(std::string_view name) {
    std::string trimmed{name};
    while (!trimmed.empty() && trimmed.front() == ' ') {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && trimmed.back() == ' ') {
        trimmed.pop_back();
    }

    if (!isValidName(trimmed)) {
        SONNET_LOG_ERROR("Layout save failed: invalid name '{}'", std::string(name));
        return false;
    }

    const char* iniData = ImGui::SaveIniSettingsToMemory();
    auto path = m_layoutsDir / (trimmed + ".ini");
    std::ofstream out(path);
    if (!out) {
        SONNET_LOG_ERROR("Layout save failed: cannot write to '{}'", path.string());
        return false;
    }
    out << iniData;
    out.close();

    // Update active.txt
    auto activePath = m_layoutsDir / "active.txt";
    std::ofstream active(activePath);
    if (active) {
        active << trimmed;
    }

    return true;
}

bool LayoutManager::loadLayout(std::string_view name) {
    auto path = m_layoutsDir / (std::string(name) + ".ini");
    std::ifstream inFile(path);
    if (!inFile) {
        SONNET_LOG_WARN("Layout load failed: '{}' not found", std::string(name));
        return false;
    }
    std::ostringstream buf;
    buf << inFile.rdbuf();
    std::string content = buf.str();
    if (content.empty()) {
        SONNET_LOG_WARN("Layout load failed: '{}' is empty or corrupt", std::string(name));
        return false;
    }
    ImGui::LoadIniSettingsFromMemory(content.c_str(), content.size());

    // Update active.txt
    auto activePath = m_layoutsDir / "active.txt";
    std::ofstream active(activePath);
    if (active) {
        active << std::string(name);
    }

    return true;
}

std::string LayoutManager::activeLayoutName() const {
    auto activePath = m_layoutsDir / "active.txt";
    std::ifstream inFile(activePath);
    if (!inFile) {
        return {};
    }
    std::string name;
    std::getline(inFile, name);
    return name;
}

void LayoutManager::restoreLastLayout() {
    auto name = activeLayoutName();
    if (!name.empty()) {
        if (!loadLayout(name)) {
            SONNET_LOG_WARN("Could not restore last layout '{}'; using default", name);
        }
    }
}

void LayoutManager::resetToDefault() { ImGui::LoadIniSettingsFromMemory("", 0); }

} // namespace sonnet::editor
