#include <sonnet/editor/AssetBrowserPanel.h>

#include <sonnet/core/Log.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace sonnet::editor {

namespace {

constexpr std::array<std::pair<const char *, std::optional<assets::AssetType>>, 6> TypeFilters{{
    {"All", std::nullopt},
    {"Textures", assets::AssetType::Texture},
    {"Meshes", assets::AssetType::Mesh},
    {"Materials", assets::AssetType::Material},
    {"Models", assets::AssetType::Model},
    {"Environments", assets::AssetType::Environment},
}};

bool containsIgnoringCase(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  return std::ranges::search(text, needle, [&](char a, char b) {
           return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
         }).begin() != text.end();
}

} // namespace

std::string assetLabel(const assets::AssetDatabase &assets, const core::Uuid &uuid) {
  if (uuid.isNil()) {
    return "(none)";
  }
  const assets::AssetInfo *info = assets.find(uuid);
  if (info == nullptr) {
    return "(missing)";
  }
  return std::format("{} ({})", info->name, assets::toString(info->type));
}

AssetBrowserPanel::AssetBrowserPanel(assets::AssetDatabase &assets, Selection &selection)
    : m_assets(assets), m_selection(selection) {
}

void AssetBrowserPanel::draw(bool &open) {
  if (!ImGui::Begin("Assets", &open)) {
    ImGui::End();
    return;
  }
  if (!m_assets.isOpen()) {
    ImGui::TextDisabled("No project open");
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(160.0f);
  ImGui::InputTextWithHint("##filter", "filter", &m_filter);
  ImGui::SameLine();
  const char *current = "All";
  for (const auto &[name, type] : TypeFilters) {
    if (type == m_type) {
      current = name;
    }
  }
  ImGui::SetNextItemWidth(130.0f);
  if (ImGui::BeginCombo("##type", current)) {
    for (const auto &[name, type] : TypeFilters) {
      if (ImGui::Selectable(name, type == m_type)) {
        m_type = type;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("New material")) {
    createMaterial();
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("assets", 3, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    for (const assets::AssetInfo *info : m_assets.assets(m_type)) {
      if (!containsIgnoringCase(info->name, m_filter)) {
        continue;
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(info->uuid.toString().c_str());
      const bool selected = m_selection.empty() && m_inspected == info->uuid;
      if (ImGui::Selectable(info->name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        m_inspected = info->uuid;
        m_selection.clear();
      }
      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(AssetDragPayload, info->uuid.bytes().data(), info->uuid.bytes().size());
        ImGui::TextUnformatted(info->name.c_str());
        ImGui::EndDragDropSource();
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(assets::toString(info->type).data());
      ImGui::TableNextColumn();
      const std::string source = info->source == "builtin"
                                     ? std::string{"built-in"}
                                     : info->source.lexically_relative(m_assets.projectRoot()).generic_string() +
                                           (info->parent.isNil() ? std::string{} : std::format(" / {}", info->name));
      ImGui::TextDisabled("%s", source.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void AssetBrowserPanel::createMaterial() {
  const std::filesystem::path root = m_assets.projectRoot() / "assets";
  std::filesystem::path file;
  for (int i = 1; i < 1000; ++i) {
    file = root / (i == 1 ? std::string{"new.material.json"} : std::format("new {}.material.json", i));
    if (!std::filesystem::exists(file)) {
      break;
    }
  }
  const auto created = m_assets.createMaterial(file, assets::MaterialSource{});
  if (!created) {
    SONNET_LOG_ERROR("{}", created.error().toString());
    return;
  }
  m_inspected = *created;
  m_selection.clear();
  SONNET_LOG_INFO("created {}", file.string());
}

} // namespace sonnet::editor
