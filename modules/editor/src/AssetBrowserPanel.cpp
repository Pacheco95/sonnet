#include <sonnet/editor/AssetBrowserPanel.h>

#include <sonnet/editor/TypeFilter.h>

#include <sonnet/core/Log.h>
#include <sonnet/world/Components.h>

#include <SDL3/SDL_misc.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sonnet::editor {

namespace {

// What the New asset menu's Script entry writes: every hook, empty, and the shape a script has to return.
constexpr std::string_view ScriptTemplate = R"lua(-- Runs on its entity in play mode; see docs/scripting.md.
local Script = {
  -- Values the inspector lets you change per entity, as in
  -- speed = { type = "number", default = 1, min = 0, max = 10 }.
  properties = {},
}

function Script:start()
end

function Script:update(dt)
end

function Script:fixedUpdate(dt)
end

function Script:onContactBegin(other, contact)
end

function Script:onContactEnd(other)
end

function Script:onTriggerEnter(other)
end

function Script:onTriggerExit(other)
end

return Script
)lua";

// -1, 0 or 1 as `a` sorts before, with or after `b`, comparing bytes with ASCII letters folded to lower case.
int compareIgnoringCase(std::string_view a, std::string_view b) {
  const auto lower = [](char c) { return std::tolower(static_cast<unsigned char>(c)); };
  const auto [left, right] = std::ranges::mismatch(a, b, {}, lower, lower);
  if (left != a.end() && right != b.end()) {
    return lower(*left) < lower(*right) ? -1 : 1;
  }
  return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

} // namespace

// A file: URL for a directory, percent-encoded so a space or a non-ASCII byte survives the trip to the
// platform's opener.
std::string directoryUrl(const std::filesystem::path &directory) {
  const std::string path = directory.generic_string();
  std::string url = "file://";
  if (path.empty() || path.front() != '/') {
    url += '/'; // a drive letter: file:///C:/...
  }
  for (const char c : path) {
    const auto byte = static_cast<unsigned char>(c);
    if (std::isalnum(byte) != 0 || std::string_view{"/:-._~"}.contains(c)) {
      url += c;
    } else {
      url += std::format("%{:02X}", byte);
    }
  }
  return url;
}

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

std::string assetSourceLabel(const assets::AssetInfo &info, const std::filesystem::path &projectRoot) {
  if (info.source == "builtin") {
    return "built-in";
  }
  return info.source.lexically_relative(projectRoot).generic_string() +
         (info.parent.isNil() ? std::string{} : std::format(" / {}", info.name));
}

AssetRow assetRow(const assets::AssetInfo &info, const std::filesystem::path &projectRoot) {
  AssetRow row;
  row.info = &info;
  row.uuid = info.uuid;
  row.name = info.name;
  row.type = assets::toString(info.type);
  row.source = assetSourceLabel(info, projectRoot);
  if (info.source != "builtin") {
    row.file = info.source;
  }
  return row;
}

AssetRow prefabRow(const core::Uuid &uuid, std::string name, const std::filesystem::path &file,
                   const std::filesystem::path &projectRoot) {
  AssetRow row;
  row.uuid = uuid;
  row.name = std::move(name);
  row.type = "Prefab";
  row.source = file.lexically_relative(projectRoot).generic_string();
  row.file = file;
  return row;
}

void sortAssetRows(std::span<AssetRow> rows, AssetSortColumn column, bool descending) {
  // Ignoring case first, then exactly, so "a" and "A" still have an order.
  const auto byName = [](const AssetRow &a, const AssetRow &b) {
    const int key = compareIgnoringCase(a.name, b.name);
    return key != 0 ? key : a.name.compare(b.name);
  };
  std::ranges::sort(rows, [&](const AssetRow &a, const AssetRow &b) {
    int key = 0;
    switch (column) {
    case AssetSortColumn::Name:
      key = byName(a, b);
      break;
    case AssetSortColumn::Type:
      key = compareIgnoringCase(a.type, b.type);
      break;
    case AssetSortColumn::Source:
      key = compareIgnoringCase(a.source, b.source);
      break;
    }
    if (key != 0) {
      return descending ? key > 0 : key < 0;
    }
    if (const int name = byName(a, b); name != 0) {
      return name < 0;
    }
    return a.uuid < b.uuid;
  });
}

AssetBrowserPanel::AssetBrowserPanel(assets::AssetDatabase &assets, world::World &world, Selection &selection)
    : m_assets(assets), m_world(world), m_selection(selection) {
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
  if (ImGui::Button("New asset")) {
    ImGui::OpenPopup("##new-asset");
  }
  if (ImGui::BeginPopup("##new-asset")) {
    if (ImGui::MenuItem("Material")) {
      createMaterial();
    }
    if (ImGui::MenuItem("Script")) {
      createScript();
    }
    ImGui::EndPopup();
  }

  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable;
  if (ImGui::BeginTable("assets", 3, flags)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 2.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
      if (specs->SpecsCount > 0) {
        m_sortColumn = static_cast<AssetSortColumn>(specs->Specs[0].ColumnIndex);
        m_sortDescending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
      }
      specs->SpecsDirty = false;
    }
    // Rebuilt and sorted every frame, so assets the database adds or drops land in order.
    std::vector<AssetRow> rows;
    for (const assets::AssetInfo *info : m_assets.assets()) {
      if (assetPassesFilter(*info, m_filter, m_types)) {
        rows.push_back(assetRow(*info, m_assets.projectRoot()));
      }
    }
    // Prefab files are not database assets; a model's prefab has no file, and its Model row stands for it.
    for (const flecs::entity prefab : m_world.prefabs()) {
      const auto *file = prefab.try_get<world::PrefabFile>();
      const auto *name = prefab.try_get<world::Name>();
      if (file != nullptr && name != nullptr && prefabPassesFilter(name->value, m_filter, m_types)) {
        rows.push_back(prefabRow(m_world.uuidOf(prefab), name->value, file->path, m_assets.projectRoot()));
      }
    }
    sortAssetRows(rows, m_sortColumn, m_sortDescending);
    for (const AssetRow &row : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(row.uuid.toString().c_str());
      // A prefab has no asset view in the inspector, so clicking one inspects nothing.
      const bool selected = row.info != nullptr && inspected() == row.uuid;
      if (ImGui::Selectable(row.name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) &&
          row.info != nullptr) {
        inspect(row.uuid);
      }
      if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload(AssetDragPayload, row.uuid.bytes().data(), row.uuid.bytes().size());
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::EndDragDropSource();
      }
      drawContextMenu(row);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.type.data(), row.type.data() + row.type.size());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", row.source.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void AssetBrowserPanel::drawContextMenu(const AssetRow &row) {
  if (!ImGui::BeginPopupContextItem("##asset-menu")) {
    return;
  }
  const bool hasFile = !row.file.empty();
  if (ImGui::MenuItem("Copy path", nullptr, false, hasFile)) {
    ImGui::SetClipboardText(row.file.string().c_str());
  }
  if (ImGui::MenuItem("Show in file manager", nullptr, false, hasFile)) {
    if (!SDL_OpenURL(directoryUrl(row.file.parent_path()).c_str())) {
      SONNET_LOG_ERROR("cannot open {}: {}", row.file.parent_path().string(), SDL_GetError());
    }
  }
  const bool script = row.info != nullptr && row.info->type == assets::AssetType::Script;
  if (ImGui::MenuItem("Open", nullptr, false, hasFile && script && m_open)) {
    m_open(row.file.string(), 1);
  }
  ImGui::EndPopup();
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
  inspect(*created);
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
  inspect(*created);
  SONNET_LOG_INFO("created {}", file.string());
}

} // namespace sonnet::editor
