#include <sonnet/editor/AssetBrowserPanel.h>

#include <sonnet/editor/FileManager.h>
#include <sonnet/editor/TypeFilter.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/Log.h>
#include <sonnet/world/Components.h>

#include <imgui.h>
#include <imgui_internal.h>
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
  const auto filename = file.filename().string();
  row.name = filename.ends_with(".prefab.json") ? filename.substr(0, filename.size() - 12) : std::move(name);
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
    if (a.directory != b.directory) {
      return a.directory;
    }
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
    if (a.uuid != b.uuid) {
      return a.uuid < b.uuid;
    }
    return a.file.generic_string() < b.file.generic_string();
  });
}

AssetBrowserPanel::AssetBrowserPanel(assets::AssetDatabase &assets, world::World &world, Selection &selection)
    : m_assets(assets), m_world(world), m_selection(selection) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  m_settingsInstalled = true;
  ImGuiSettingsHandler handler;
  handler.TypeName = "SonnetAssets";
  handler.TypeHash = ImHashStr(handler.TypeName);
  handler.UserData = this;
  handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *self, const char *) -> void * {
    return self->UserData;
  };
  handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
    auto &panel = *static_cast<AssetBrowserPanel *>(entry);
    const std::string_view record{line};
    if (!record.starts_with("Entry=")) {
      return;
    }
    const auto text = record.substr(6);
    auto value = assets::parseJson(std::as_bytes(std::span{text.data(), text.size()}));
    if (value.is_array() && value.size() == 2 && value[0].is_string() && value[1].is_string()) {
      panel.m_folders[value[0].get<std::string>()] = value[1].get<std::string>();
      panel.m_project.clear();
    }
  };
  handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *self, ImGuiTextBuffer *out) {
    auto &panel = *static_cast<AssetBrowserPanel *>(self->UserData);
    out->append("[SonnetAssets][Folders]\n");
    for (const auto &[project, folder] : panel.m_folders) {
      out->appendf("Entry=%s\n", nlohmann::json::array({project, folder}).dump().c_str());
    }
    out->append("\n");
  };
  ImGui::AddSettingsHandler(&handler);
}

AssetBrowserPanel::~AssetBrowserPanel() {
  if (m_settingsInstalled && ImGui::GetCurrentContext() != nullptr) {
    ImGui::RemoveSettingsHandler("SonnetAssets");
  }
}

namespace {
bool within(const std::filesystem::path &path, const std::filesystem::path &root) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && *relative.begin() != "..";
}
ImGuiID rowId(const AssetRow &row) {
  const auto key = row.file.generic_string() + ":" + row.uuid.toString();
  return ImHashStr(key.c_str());
}
bool containsText(std::string_view name, std::string_view text) {
  return std::ranges::search(
             name, text,
             [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
             }).begin() != name.end() ||
         text.empty();
}
} // namespace

void AssetBrowserPanel::syncProject() {
  if (m_project == m_assets.projectRoot()) {
    return;
  }
  m_project = m_assets.projectRoot();
  m_folder = m_project;
  if (const auto found = m_folders.find(m_project.generic_string()); found != m_folders.end()) {
    auto folder = (m_project / found->second).lexically_normal();
    if (folder.filename().empty()) {
      folder = folder.parent_path();
    }
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(folder, error);
    const auto root = std::filesystem::weakly_canonical(m_project, error);
    if (!error && within(resolved, root) && std::filesystem::is_directory(folder, error)) {
      m_folder = folder;
    }
  }
  m_history = {m_folder};
  m_historyIndex = 0;
  m_selected.Clear();
  m_focused = m_renaming = 0;
}

const std::filesystem::path &AssetBrowserPanel::currentFolder() {
  syncProject();
  std::error_code error;
  if (!std::filesystem::is_directory(m_folder, error)) {
    navigate(m_project);
  }
  return m_folder;
}

void AssetBrowserPanel::navigate(const std::filesystem::path &folder) {
  syncProject();
  const auto target = folder.lexically_normal();
  std::error_code error;
  const auto resolved = std::filesystem::weakly_canonical(target, error);
  const auto root = std::filesystem::weakly_canonical(m_project, error);
  if (error || !within(resolved, root) || !std::filesystem::is_directory(target, error) || target == m_folder) {
    return;
  }
  m_history.resize(m_historyIndex + 1);
  m_history.push_back(target);
  ++m_historyIndex;
  m_folder = target;
  m_folders[m_project.generic_string()] = m_folder.lexically_relative(m_project).generic_string();
  m_selected.Clear();
  m_focused = m_renaming = 0;
  ImGui::MarkIniSettingsDirty();
}

void AssetBrowserPanel::back() {
  syncProject();
  if (m_historyIndex == 0) {
    return;
  }
  const auto target = m_history[--m_historyIndex];
  m_folder = target;
  m_folders[m_project.generic_string()] = m_folder.lexically_relative(m_project).generic_string();
  m_selected.Clear();
  m_focused = m_renaming = 0;
  ImGui::MarkIniSettingsDirty();
}
void AssetBrowserPanel::forward() {
  syncProject();
  if (m_historyIndex + 1 >= m_history.size()) {
    return;
  }
  m_folder = m_history[++m_historyIndex];
  m_folders[m_project.generic_string()] = m_folder.lexically_relative(m_project).generic_string();
  m_selected.Clear();
  m_focused = m_renaming = 0;
  ImGui::MarkIniSettingsDirty();
}
void AssetBrowserPanel::up() {
  navigate(currentFolder().parent_path());
}

std::filesystem::path AssetBrowserPanel::importFolder() {
  auto folder = currentFolder();
  std::error_code error;
  const auto resolved = std::filesystem::weakly_canonical(folder, error);
  for (const auto &root : m_assets.roots()) {
    const auto resolvedRoot = std::filesystem::weakly_canonical(m_project / root, error);
    if (!error && within(resolved, resolvedRoot)) {
      return folder;
    }
  }
  return m_assets.roots().empty() ? std::filesystem::path{} : m_project / m_assets.roots().front();
}

std::vector<AssetRow> AssetBrowserPanel::rows() {
  const auto folder = currentFolder();
  std::vector<AssetRow> result;
  std::vector<std::filesystem::path> files;
  std::error_code error;
  const auto add = [&](const std::filesystem::directory_entry &entry) {
    const auto &file = entry.path();
    // Sidecars and editor state belong to their source rows, never to user file operations.
    if (file.filename().string().starts_with('.') || file.extension() == ".meta" || entry.is_symlink(error)) {
      return;
    }
    if (entry.is_directory(error)) {
      if (containsText(file.filename().string(), m_filter)) {
        AssetRow row;
        row.name = file.filename().string();
        row.type = "Folder";
        row.file = file;
        row.directory = true;
        row.source = file.lexically_relative(m_project).generic_string();
        result.push_back(std::move(row));
      }
    } else if (entry.is_regular_file(error)) {
      files.push_back(file);
    }
  };
  if (m_filter.empty()) {
    for (std::filesystem::directory_iterator it{folder, error}, end; !error && it != end; it.increment(error)) {
      add(*it);
    }
  } else {
    for (std::filesystem::recursive_directory_iterator
             it{folder, std::filesystem::directory_options::skip_permission_denied, error},
         end;
         !error && it != end; it.increment(error)) {
      if (it->path().filename().string().starts_with('.') || it->is_symlink(error)) {
        it.disable_recursion_pending();
        continue;
      }
      add(*it);
    }
  }
  std::map<std::filesystem::path, std::vector<const assets::AssetInfo *>> byFile;
  for (const auto *info : m_assets.assets()) {
    byFile[info->source].push_back(info);
  }
  std::map<std::filesystem::path, core::Uuid> prefabFiles;
  for (const auto prefab : m_world.prefabs()) {
    if (const auto *file = prefab.try_get<world::PrefabFile>()) {
      prefabFiles[file->path] = m_world.uuidOf(prefab);
    }
  }
  for (const auto &file : files) {
    bool known = false;
    for (const auto *info : byFile[file]) {
      known = true;
      if (assetPassesFilter(*info, m_filter, m_types)) {
        result.push_back(assetRow(*info, m_project));
      }
    }
    if (const auto found = prefabFiles.find(file); found != prefabFiles.end()) {
      known = true;
      auto row = prefabRow(found->second, file.stem().string(), file, m_project);
      if (prefabPassesFilter(row.name, m_filter, m_types)) {
        result.push_back(std::move(row));
      }
    }
    if (!known) {
      AssetRow row;
      row.file = file;
      row.name = file.filename().string();
      row.source = file.lexically_relative(m_project).generic_string();
      row.type = row.name.ends_with(".scene.json") ? "Scene" : row.name.ends_with(".prefab.json") ? "Prefab" : "File";
      if (containsText(row.name, m_filter) &&
          (m_types.empty() || (row.type == "Prefab" && prefabPassesFilter(row.name, m_filter, m_types)) ||
           (row.type == "Scene" && m_types.matches(filterableAssetTypes().size() + 1)) ||
           (row.type == "File" && m_types.matches(filterableAssetTypes().size() + 2)))) {
        result.push_back(std::move(row));
      }
    }
  }
  // Built-ins have no disk location; keep them available at the project root.
  if (folder == m_project) {
    for (const auto *info : m_assets.assets()) {
      if (info->source == "builtin" && assetPassesFilter(*info, m_filter, m_types)) {
        result.push_back(assetRow(*info, m_project));
      }
    }
  }
  sortAssetRows(result, m_sortColumn, m_sortDescending);
  return result;
}

void AssetBrowserPanel::drawNavigation() {
  (void)currentFolder();
  ImGui::BeginDisabled(m_historyIndex == 0);
  if (ImGui::SmallButton("Back")) {
    back();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(m_historyIndex + 1 >= m_history.size());
  if (ImGui::SmallButton("Forward")) {
    forward();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(m_folder == m_project);
  if (ImGui::SmallButton("Up")) {
    up();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton(m_project.filename().string().c_str())) {
    navigate(m_project);
  }
  auto path = m_project;
  const auto relative = m_folder.lexically_relative(m_project);
  for (const auto &part : relative) {
    if (part == ".") {
      continue;
    }
    path /= part;
    ImGui::SameLine();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::PushID(path.generic_string().c_str());
    if (ImGui::SmallButton(part.string().c_str())) {
      navigate(path);
    }
    ImGui::PopID();
  }
  if (focused() && !ImGui::GetIO().WantTextInput) {
    const bool alt = ImGui::GetIO().KeyAlt;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) || (alt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))) {
      up();
    }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      back();
    }
    if (alt && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      forward();
    }
  }
}

void AssetBrowserPanel::createFolder() {
  if (!m_createFolder) {
    return;
  }
  auto file = currentFolder() / "New folder";
  std::error_code error;
  for (int i = 2; std::filesystem::exists(file, error); ++i) {
    file = currentFolder() / std::format("New folder {}", i);
  }
  if (auto result = m_createFolder(file); !result) {
    m_fileError = result.error().message;
  }
}

bool AssetBrowserPanel::focused() const noexcept {
  const auto *context = ImGui::GetCurrentContext();
  const auto *window = context != nullptr ? ImGui::FindWindowByName("Assets") : nullptr;
  return window != nullptr && context->NavWindow != nullptr && context->NavWindow->RootWindow == window->RootWindow;
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
  drawNavigation();
  m_drawnFrame = ImGui::GetFrameCount();
  m_min = {ImGui::GetWindowPos().x, ImGui::GetWindowPos().y};
  m_max = m_min + glm::vec2{ImGui::GetWindowSize().x, ImGui::GetWindowSize().y};
  ImGui::SetNextItemWidth(160.0f);
  if (ImGui::InputTextWithHint("##filter", "filter", &m_filter)) {
    m_selected.Clear();
    m_focused = 0;
  }
  ImGui::SameLine();
  if (typeFilterCombo("##type", 130.0f, filterableAssetTypeNames(), m_types)) {
    m_selected.Clear();
    m_focused = 0;
  }
  ImGui::SameLine();
  if (ImGui::Button("New asset")) {
    ImGui::OpenPopup("##new-asset");
  }
  if (ImGui::BeginPopup("##new-asset")) {
    if (ImGui::MenuItem("New folder", nullptr, false, static_cast<bool>(m_createFolder))) {
      createFolder();
    }
    if (ImGui::MenuItem("Material")) {
      createMaterial();
    }
    if (ImGui::MenuItem("Script")) {
      createScript();
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_import);
  if (ImGui::Button("Import")) {
    m_import();
  }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("Copy images, glTF models and .hdr skies into the project; dropping files here does the same");

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
    m_rows = rows();
    m_selected.UserData = &m_rows;
    m_selected.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage *self, int index) {
      return rowId((*static_cast<std::vector<AssetRow> *>(self->UserData))[static_cast<std::size_t>(index)]);
    };
    m_selected.ApplyRequests(
        ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid,
                                m_selected.Size, static_cast<int>(m_rows.size())));
    std::optional<std::filesystem::path> destination;
    const bool keyboard =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
    for (int index = 0; index < static_cast<int>(m_rows.size()); ++index) {
      const AssetRow &row = m_rows[static_cast<std::size_t>(index)];
      const auto id = rowId(row);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(id));
      const bool editable = !row.file.empty() && (row.info == nullptr || row.info->parent.isNil());
      if (keyboard && m_focused == id && editable) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2) && m_move) {
          beginRename(row);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_remove) {
          confirmDelete(row);
        }
      }
      if (m_renaming == id) {
        if (m_renameFocus) {
          ImGui::SetKeyboardFocusHere();
          m_renameFocus = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##rename", &m_rename, ImGuiInputTextFlags_EnterReturnsTrue)) {
          const auto filename = row.file.filename().string();
          const auto extension = row.directory                          ? std::string{}
                                 : filename.ends_with(".scene.json")    ? std::string{".scene.json"}
                                 : filename.ends_with(".material.json") ? std::string{".material.json"}
                                 : filename.ends_with(".prefab.json")   ? std::string{".prefab.json"}
                                                                        : row.file.extension().string();
          const std::filesystem::path name{m_rename + extension};
          if (m_rename.empty() || name.has_parent_path() || m_rename == "." || m_rename == ".." ||
              m_rename.find_first_of("/\\") != std::string::npos) {
            m_fileError = "Enter a file name without a folder.";
          } else {
            m_pendingRename = std::pair{row.file, row.file.parent_path() / name};
          }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
          m_renaming = {};
          m_fileError.clear();
        }
      } else {
        ImGui::SetNextItemSelectionUserData(index);
        if (ImGui::Selectable(row.name.c_str(), m_selected.Contains(id),
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
          m_focused = rowId(row);
          inspect(row.info != nullptr ? row.uuid : core::Uuid{});
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl &&
              !ImGui::GetIO().KeySuper && !ImGui::GetIO().KeyShift) {
            if (row.directory) {
              destination = row.file;
            } else if (row.type == "Scene" && m_scene) {
              m_scene(row.file);
            }
          }
        }
      }
      if (keyboard && m_focused == id && ImGui::IsKeyPressed(ImGuiKey_Enter) && m_renaming == 0) {
        if (row.directory) {
          destination = row.file;
        } else if (row.type == "Scene" && m_scene) {
          m_scene(row.file);
        }
      }
      if (!row.uuid.isNil() && ImGui::BeginDragDropSource()) {
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
    m_selected.ApplyRequests(ImGui::EndMultiSelect());
    ImGui::EndTable();
    if (destination) {
      navigate(*destination);
    }
  }
  if (ImGui::BeginPopupContextWindow("##background",
                                     ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem("New folder", nullptr, false, static_cast<bool>(m_createFolder))) {
      createFolder();
    }
    ImGui::EndPopup();
  }
  // Imports finished by a move or by reference counting may replace sub-assets. Do this
  // after drawing every row, when none of their database pointers will be used again.
  if (m_pendingRename) {
    if (const auto result = m_move(m_pendingRename->first, m_pendingRename->second); !result) {
      m_fileError = result.error().message;
    } else {
      for (auto row : m_rows) {
        if (rowId(row) == m_focused) {
          row.file = m_pendingRename->second;
          m_focused = rowId(row);
          break;
        }
      }
      m_selected.Clear();
      m_selected.SetItemSelected(m_focused, true);
      m_renaming = {};
      m_fileError.clear();
    }
    m_pendingRename.reset();
  }
  if (!m_fileError.empty()) {
    ImGui::TextWrapped("%s", m_fileError.c_str());
  }
  drawRenameModal();
  drawDeleteModal();
  ImGui::End();
}

bool AssetBrowserPanel::dropTarget(glm::vec2 screen) const {
  return m_drawnFrame == ImGui::GetFrameCount() && screen.x >= m_min.x && screen.y >= m_min.y && screen.x < m_max.x &&
         screen.y < m_max.y;
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
    revealInFileManager(row.file);
  }
  const bool script = row.info != nullptr && row.info->type == assets::AssetType::Script;
  if (ImGui::MenuItem("Open", nullptr, false, hasFile && script && m_open)) {
    m_open(row.file.string(), 1);
  }
  const bool editable = hasFile && (row.info == nullptr || row.info->parent.isNil());
  if (ImGui::MenuItem("Rename", "F2", false, editable && static_cast<bool>(m_move))) {
    beginRename(row);
  }
  if (ImGui::MenuItem("Delete", "Delete", false, editable && static_cast<bool>(m_remove))) {
    confirmDelete(row);
  }
  ImGui::EndPopup();
}

namespace {
std::string rowExtension(const AssetRow &row) {
  if (row.directory) {
    return {};
  }
  const auto filename = row.file.filename().string();
  for (const std::string_view extension : {".scene.json", ".prefab.json", ".material.json"}) {
    if (filename.ends_with(extension)) {
      return std::string{extension};
    }
  }
  return row.file.extension().string();
}
bool validName(const std::string &name) {
  return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\") == std::string::npos;
}
} // namespace

void AssetBrowserPanel::drawRenameModal() {
  if (m_batchRenameRequested) {
    ImGui::OpenPopup("Rename selection");
    m_batchRenameRequested = false;
  }
  if (!ImGui::BeginPopupModal("Rename selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  for (std::size_t i = 0; i < m_renameRows.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::TextUnformatted(m_renameRows[i].source.c_str());
    ImGui::InputText("Name", &m_renameNames[i]);
    ImGui::PopID();
  }
  if (!m_fileError.empty()) {
    ImGui::TextWrapped("%s", m_fileError.c_str());
  }
  if (ImGui::Button("Rename")) {
    m_fileError.clear();
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    for (std::size_t i = 0; i < m_renameRows.size(); ++i) {
      if (!validName(m_renameNames[i])) {
        m_fileError = "Enter names without a folder.";
        break;
      }
      const auto &row = m_renameRows[i];
      const auto to = row.file.parent_path() / (m_renameNames[i] + rowExtension(row));
      if (to != row.file) {
        moves.emplace_back(row.file, to);
      }
    }
    if (m_fileError.empty()) {
      if (auto result = m_moveMany(moves); !result) {
        m_fileError = result.error().message;
      } else {
        m_selected.Clear();
        ImGui::CloseCurrentPopup();
      }
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_fileError.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void AssetBrowserPanel::beginRename(const AssetRow &row) {
  m_fileError.clear();
  if (m_moveMany && m_selected.Size > 1) {
    m_renameRows.clear();
    m_renameNames.clear();
    for (const auto &selected : m_rows) {
      if (m_selected.Contains(rowId(selected)) && !selected.file.empty() &&
          (selected.info == nullptr || selected.info->parent.isNil())) {
        if (std::ranges::any_of(m_renameRows, [&](const auto &parent) { return within(selected.file, parent.file); })) {
          continue;
        }
        const auto filename = selected.file.filename().string();
        m_renameRows.push_back(selected);
        m_renameNames.push_back(filename.substr(0, filename.size() - rowExtension(selected).size()));
      }
    }
    m_batchRenameRequested = true;
    return;
  }
  m_focused = rowId(row);
  m_renaming = rowId(row);
  const auto filename = row.file.filename().string();
  // File prefabs can have a display name different from their file stem.
  m_rename = row.directory                          ? filename
             : filename.ends_with(".scene.json")    ? filename.substr(0, filename.size() - 11)
             : filename.ends_with(".prefab.json")   ? filename.substr(0, filename.size() - 12)
             : filename.ends_with(".material.json") ? filename.substr(0, filename.size() - 14)
                                                    : row.file.stem().string();
  m_renameFocus = true;
  m_fileError.clear();
}

void AssetBrowserPanel::confirmDelete(const AssetRow &row) {
  m_focused = rowId(row);
  m_deleteFiles.clear();
  m_deleteIds.clear();
  if (m_selected.Size == 0) {
    m_selected.Clear();
    m_selected.SetItemSelected(rowId(row), true);
  }
  for (const auto &selected : m_rows) {
    if (m_selected.Contains(rowId(selected)) && !selected.file.empty() &&
        (selected.info == nullptr || selected.info->parent.isNil())) {
      m_deleteFiles.push_back(selected.file);
      if (selected.directory) {
        for (const auto *info : m_assets.assets()) {
          if (info->parent.isNil() && within(info->source, selected.file)) {
            m_deleteIds.push_back(info->uuid);
          }
        }
        for (const auto prefab : m_world.prefabs()) {
          const auto *file = prefab.try_get<world::PrefabFile>();
          if (file != nullptr && within(file->path, selected.file)) {
            m_deleteIds.push_back(m_world.uuidOf(prefab));
          }
        }
      } else if (!selected.uuid.isNil()) {
        m_deleteIds.push_back(selected.uuid);
      }
    }
  }
  m_deleteRequested = true;
  m_fileError.clear();
}

void AssetBrowserPanel::drawDeleteModal() {
  if (m_deleteRequested) {
    m_deleteReferences = m_references ? m_references(m_deleteIds) : AssetReferences{};
    ImGui::OpenPopup("Delete asset?");
    m_deleteRequested = false;
  }
  if (!ImGui::BeginPopupModal("Delete asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::Text("Delete %zu selected files or folders?", m_deleteFiles.size());
  for (const auto &file : m_deleteFiles) {
    ImGui::TextUnformatted(file.filename().string().c_str());
  }
  ImGui::Text("References in open scenes: %zu entities, %zu prefab instances.", m_deleteReferences.entities,
              m_deleteReferences.instances);
  ImGui::Text("Materials using these textures: %zu.", m_deleteReferences.materials);
  ImGui::TextUnformatted("This can be undone. Scenes that are not open are not counted.");
  if (!m_fileError.empty()) {
    ImGui::TextWrapped("%s", m_fileError.c_str());
  }
  if (ImGui::Button("Delete")) {
    if (const auto result = m_remove(m_deleteFiles); !result) {
      m_fileError = result.error().message;
    } else {
      inspect({});
      m_fileError.clear();
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_fileError.clear();
    ImGui::CloseCurrentPopup();
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
