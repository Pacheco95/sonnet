#include <sonnet/editor/AssetBrowserPanel.h>

#include <sonnet/editor/TypeFilter.h>

#include <sonnet/core/Log.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace sonnet::editor {

namespace {

// What "New script" writes: every hook, empty, and the shape a script has to return.
constexpr std::string_view ScriptTemplate = R"lua(-- Runs on its entity in play mode; see docs/scripting.md.
local Script = {}

function Script:start()
end

function Script:update(dt)
end

function Script:fixedUpdate(dt)
end

return Script
)lua";

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
  typeFilterCombo("##type", 130.0f, filterableAssetTypeNames(), m_types);
  ImGui::SameLine();
  if (ImGui::Button("New material")) {
    createMaterial();
  }
  ImGui::SameLine();
  if (ImGui::Button("New script")) {
    createScript();
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("assets", 3, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    for (const assets::AssetInfo *info : m_assets.assets()) {
      if (!assetPassesFilter(*info, m_filter, m_types)) {
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
      const std::string_view type = assets::toString(info->type);
      ImGui::TextUnformatted(type.data(), type.data() + type.size());
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

void AssetBrowserPanel::createScript() {
  // The project's scripts folder when it has one, otherwise its first asset root.
  const std::span<const std::string> roots = m_assets.roots();
  const bool scriptsRoot = std::ranges::find(roots, std::string{"scripts"}) != roots.end();
  const std::filesystem::path root =
      m_assets.projectRoot() / (scriptsRoot || roots.empty() ? std::string{"scripts"} : roots.front());
  std::filesystem::path file;
  for (int i = 1; i < 1000; ++i) {
    file = root / (i == 1 ? std::string{"new.lua"} : std::format("new {}.lua", i));
    if (!std::filesystem::exists(file)) {
      break;
    }
  }
  const auto created = m_assets.createScript(file, ScriptTemplate);
  if (!created) {
    SONNET_LOG_ERROR("{}", created.error().toString());
    return;
  }
  m_inspected = *created;
  m_selection.clear();
  SONNET_LOG_INFO("created {}", file.string());
}

} // namespace sonnet::editor
