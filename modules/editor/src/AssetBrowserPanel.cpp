#include <sonnet/editor/AssetBrowserPanel.h>

#include <sonnet/editor/FileManager.h>
#include <sonnet/editor/Icons.h>
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

// Whether the tile shows a rendered or decoded picture rather than its type's icon: a file asset that has one, a mesh
// (a model's sub-asset or a built-in one) and a prefab the world has loaded.
bool hasThumbnail(const AssetRow &row) {
  if (row.info == nullptr) {
    return row.type == "Prefab" && !row.uuid.isNil();
  }
  switch (row.info->type) {
  case assets::AssetType::Mesh:
    return true;
  case assets::AssetType::Texture:
  case assets::AssetType::Environment:
  case assets::AssetType::Material:
  case assets::AssetType::Model:
    return row.info->parent.isNil();
  default:
    return false;
  }
}

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

std::vector<std::filesystem::path> AssetBrowserPanel::filesOf(const AssetRow *row) const {
  const bool selection = row == nullptr || m_selected.Contains(rowId(*row));
  std::vector<std::filesystem::path> files;
  for (const AssetRow &candidate : m_rows) {
    const bool wanted = selection ? m_selected.Contains(rowId(candidate)) : rowId(candidate) == rowId(*row);
    const bool movable = !candidate.file.empty() && (candidate.info == nullptr || candidate.info->parent.isNil());
    // A model's sub-asset rows share its file, which is listed once.
    if (wanted && movable && !std::ranges::contains(files, candidate.file)) {
      files.push_back(candidate.file);
    }
  }
  // Moving a folder moves what is in it.
  std::erase_if(files, [&](const std::filesystem::path &file) {
    return std::ranges::any_of(files, [&](const auto &other) { return other != file && within(file, other); });
  });
  return files;
}

void AssetBrowserPanel::queueMoves(std::span<const std::filesystem::path> files, const std::filesystem::path &folder) {
  for (const std::filesystem::path &file : files) {
    std::error_code error;
    // Not into the folder it is in, itself or what it holds.
    if (file == folder || within(folder, file) || std::filesystem::equivalent(file.parent_path(), folder, error)) {
      continue;
    }
    m_queuedMoves.emplace_back(file, folder / file.filename());
  }
}

void AssetBrowserPanel::acceptDrop(const std::filesystem::path &folder) {
  if (m_dragged.empty() || !m_moveMany || !ImGui::BeginDragDropTarget()) {
    return;
  }
  const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetDragPayload);
  if (payload == nullptr) {
    payload = ImGui::AcceptDragDropPayload(AssetMovePayload);
  }
  if (payload != nullptr && payload->IsDelivery()) {
    queueMoves(m_dragged, folder);
  }
  ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::queueDuplicate(std::vector<std::filesystem::path> files) {
  std::erase_if(files, [](const std::filesystem::path &file) { return !assets::AssetDatabase::sourceType(file); });
  m_queuedDuplicates = std::move(files);
}

// Runs what the frame queued, after every row is drawn, because a move or a copy changes the database the rows point
// into.
void AssetBrowserPanel::runQueued() {
  if (!m_queuedMoves.empty() && m_moveMany) {
    if (const auto result = m_moveMany(m_queuedMoves); !result) {
      m_fileError = result.error().message;
    } else {
      m_fileError.clear();
      m_selected.Clear();
      m_focused = 0;
      m_cut.clear();
    }
  }
  m_queuedMoves.clear();
  if (!m_queuedDuplicates.empty() && m_duplicate) {
    if (const auto result = m_duplicate(m_queuedDuplicates); !result) {
      m_fileError = result.error().message;
    } else {
      m_fileError.clear();
    }
  }
  m_queuedDuplicates.clear();
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
  acceptDrop(m_project);
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
    acceptDrop(path);
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
  if (ImGui::GetDragDropPayload() == nullptr) {
    m_dragged.clear();
  }
  m_opened = m_opened || m_drawnFrame < ImGui::GetFrameCount() - 1;
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
  ImGui::SameLine();
  drawViewButtons();

  if (!m_fileError.empty()) {
    ImGui::TextWrapped("%s", m_fileError.c_str());
  }
  std::optional<std::filesystem::path> destination;
  const bool keyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
  if (m_view.grid) {
    drawGrid(destination, keyboard);
  } else {
    drawTable(destination, keyboard);
    drawBackgroundMenu();
  }
  if (destination) {
    navigate(*destination);
  }
  runQueued();
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
  drawRenameModal();
  drawDeleteModal();
  ImGui::End();
}

bool AssetBrowserPanel::dropTarget(glm::vec2 screen) const {
  return m_drawnFrame == ImGui::GetFrameCount() && screen.x >= m_min.x && screen.y >= m_min.y && screen.x < m_max.x &&
         screen.y < m_max.y;
}

void AssetBrowserPanel::setView(AssetsView view) {
  view.tileSize = std::clamp(view.tileSize, AssetsView::MinTileSize, AssetsView::MaxTileSize);
  m_view = view;
}

namespace {
ScreenRect lastItemRect() {
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  return {{min.x, min.y}, {max.x, max.y}};
}
} // namespace

void AssetBrowserPanel::drawViewButtons() {
  bool changed = false;
  for (const bool grid : {false, true}) {
    const bool active = m_view.grid == grid;
    ImGui::PushID(grid ? "grid" : "list");
    if (active) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(grid ? IconGrid : IconList) && !active) {
      m_view.grid = grid;
      changed = true;
    }
    if (active) {
      ImGui::PopStyleColor();
    }
    (grid ? m_gridButton : m_listButton) = lastItemRect();
    ImGui::SetItemTooltip(grid ? "Grid view" : "List view");
    ImGui::PopID();
    if (!grid) {
      ImGui::SameLine(0.0f, 2.0f);
    }
  }
  if (m_view.grid) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("##tile", &m_view.tileSize, AssetsView::MinTileSize, AssetsView::MaxTileSize, "%.0f");
    ImGui::SetItemTooltip("Tile size");
    // The slider changes the view while it is dragged; the handler hears of it when it is let go.
    changed = changed || ImGui::IsItemDeactivatedAfterEdit();
  }
  if (changed && m_viewChanged) {
    m_viewChanged(m_view);
  }
}

void AssetBrowserPanel::drawBackgroundMenu() {
  if (ImGui::BeginPopupContextWindow("##background",
                                     ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem("New folder", nullptr, false, static_cast<bool>(m_createFolder))) {
      createFolder();
    }
    ImGui::EndPopup();
  }
}

void AssetBrowserPanel::beginSelection() {
  m_selected.UserData = &m_rows;
  m_selected.AdapterIndexToStorageId = [](ImGuiSelectionBasicStorage *self, int index) {
    return rowId((*static_cast<std::vector<AssetRow> *>(self->UserData))[static_cast<std::size_t>(index)]);
  };
  m_selected.ApplyRequests(
      ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid,
                              m_selected.Size, static_cast<int>(m_rows.size())));
}

void AssetBrowserPanel::endSelection(bool keyboard) {
  if (keyboard && m_renaming == 0) {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
      m_cut = filesOf(nullptr);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
      queueMoves(m_cut, currentFolder());
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
      queueDuplicate(filesOf(nullptr));
    }
  }
  m_selected.ApplyRequests(ImGui::EndMultiSelect());
}

void AssetBrowserPanel::handleRowKeys(const AssetRow &row, ImGuiID id, bool editable, bool keyboard) {
  if (keyboard && m_focused == id && editable) {
    if (ImGui::IsKeyPressed(ImGuiKey_F2) && m_move) {
      beginRename(row);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_remove) {
      confirmDelete(row);
    }
  }
}

void AssetBrowserPanel::drawRenameField(const AssetRow &row, float width) {
  if (m_renameFocus) {
    ImGui::SetKeyboardFocusHere();
    m_renameFocus = false;
  }
  ImGui::SetNextItemWidth(width);
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
}

void AssetBrowserPanel::rowClicked(const AssetRow &row, std::optional<std::filesystem::path> &destination) {
  m_focused = rowId(row);
  inspect(row.info != nullptr ? row.uuid : core::Uuid{});
  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper &&
      !ImGui::GetIO().KeyShift) {
    if (row.directory) {
      destination = row.file;
    } else if (row.type == "Scene" && m_scene) {
      m_scene(row.file);
    }
  }
}

void AssetBrowserPanel::finishRow(const AssetRow &row, ImGuiID id, bool editable, bool keyboard,
                                  std::optional<std::filesystem::path> &destination) {
  if (keyboard && m_focused == id && ImGui::IsKeyPressed(ImGuiKey_Enter) && m_renaming == 0) {
    if (row.directory) {
      destination = row.file;
    } else if (row.type == "Scene" && m_scene) {
      m_scene(row.file);
    }
  }
  if ((!row.uuid.isNil() || (editable && m_moveMany)) && ImGui::BeginDragDropSource()) {
    m_dragged = editable ? filesOf(&row) : std::vector<std::filesystem::path>{};
    if (!row.uuid.isNil()) {
      ImGui::SetDragDropPayload(AssetDragPayload, row.uuid.bytes().data(), row.uuid.bytes().size());
    } else {
      ImGui::SetDragDropPayload(AssetMovePayload, row.name.data(), row.name.size());
    }
    if (m_dragged.size() > 1) {
      ImGui::Text("%zu items", m_dragged.size());
    } else {
      ImGui::TextUnformatted(row.name.c_str());
    }
    ImGui::EndDragDropSource();
  }
  if (row.directory) {
    acceptDrop(row.file);
  }
  drawContextMenu(row);
}

void AssetBrowserPanel::drawTable(std::optional<std::filesystem::path> &destination, bool keyboard) {
  m_tiles.clear();
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable;
  if (!ImGui::BeginTable("assets", 3, flags)) {
    return;
  }
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
  beginSelection();
  for (int index = 0; index < static_cast<int>(m_rows.size()); ++index) {
    const AssetRow &row = m_rows[static_cast<std::size_t>(index)];
    const auto id = rowId(row);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(static_cast<int>(id));
    const bool editable = !row.file.empty() && (row.info == nullptr || row.info->parent.isNil());
    handleRowKeys(row, id, editable, keyboard);
    if (m_renaming == id) {
      drawRenameField(row, -1.0f);
    } else {
      ImGui::SetNextItemSelectionUserData(index);
      if (ImGui::Selectable(row.name.c_str(), m_selected.Contains(id),
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
        rowClicked(row, destination);
      }
    }
    finishRow(row, id, editable, keyboard, destination);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.type.data(), row.type.data() + row.type.size());
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", row.source.c_str());
    ImGui::PopID();
  }
  endSelection(keyboard);
  ImGui::EndTable();
}

void AssetBrowserPanel::drawGrid(std::optional<std::filesystem::path> &destination, bool keyboard) {
  m_tiles.clear();
  if (!ImGui::BeginChild("##grid", ImVec2{0.0f, 0.0f}, ImGuiChildFlags_None)) {
    ImGui::EndChild();
    return;
  }
  m_rows = rows();
  beginSelection();
  const ImGuiStyle &style = ImGui::GetStyle();
  const float tile = m_view.tileSize;
  const float labelHeight = ImGui::GetTextLineHeight();
  const ImVec2 itemSize{tile, tile + labelHeight + 4.0f};
  const ImVec2 cell{itemSize.x + style.ItemSpacing.x, itemSize.y + style.ItemSpacing.y};
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell.x));
  ImDrawList *list = ImGui::GetWindowDrawList();
  for (int index = 0; index < static_cast<int>(m_rows.size()); ++index) {
    const AssetRow &row = m_rows[static_cast<std::size_t>(index)];
    const auto id = rowId(row);
    const int column = index % columns;
    const int line = index / columns;
    const ImVec2 min{origin.x + static_cast<float>(column) * cell.x, origin.y + static_cast<float>(line) * cell.y};
    const ImVec2 max{min.x + itemSize.x, min.y + itemSize.y};
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(static_cast<int>(id));
    const bool editable = !row.file.empty() && (row.info == nullptr || row.info->parent.isNil());
    handleRowKeys(row, id, editable, keyboard);
    if (m_renaming == id) {
      ImGui::Dummy(itemSize);
      ImGui::SetCursorScreenPos({min.x, min.y + tile});
      drawRenameField(row, tile);
    } else {
      ImGui::SetNextItemSelectionUserData(index);
      if (ImGui::Selectable("##tile", m_selected.Contains(id), ImGuiSelectableFlags_AllowDoubleClick, itemSize)) {
        rowClicked(row, destination);
      }
      ImGui::SetItemTooltip("%s\n%s", row.name.c_str(), std::string{row.type}.c_str());
    }
    finishRow(row, id, editable, keyboard, destination);
    ImGui::PopID();
    m_tiles.push_back({{min.x, min.y}, {max.x, max.y}});

    if (!ImGui::IsRectVisible(min, max)) {
      continue;
    }
    // The picture is asked for only here, so a folder of hundreds of textures makes the ones in view.
    ThumbnailImage picture;
    if (m_thumbnail && hasThumbnail(row)) {
      picture = m_thumbnail(row.uuid);
    }
    const ImVec2 square{min.x + tile, min.y + tile};
    list->AddRectFilled(min, square, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding + 2.0f);
    if (picture.texture != 0 && picture.size.x > 0 && picture.size.y > 0) {
      const float fit = (tile - 8.0f) / static_cast<float>(std::max(picture.size.x, picture.size.y));
      const ImVec2 extent{static_cast<float>(picture.size.x) * fit, static_cast<float>(picture.size.y) * fit};
      const ImVec2 corner{min.x + (tile - extent.x) * 0.5f, min.y + (tile - extent.y) * 0.5f};
      list->AddImage(picture.texture, corner, {corner.x + extent.x, corner.y + extent.y});
    } else {
      ImFont *font = ImGui::GetFont();
      const char *icon = iconForType(row.type);
      const float size = tile * 0.5f;
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
      list->AddText(font, size, {min.x + (tile - extent.x) * 0.5f, min.y + (tile - extent.y) * 0.5f},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);
    }
    if (m_renaming != id) {
      // The name is centred when it fits and starts at the tile's edge when it does not, clipped there.
      const ImVec2 extent = ImGui::CalcTextSize(row.name.c_str());
      list->PushClipRect({min.x, square.y}, max, true);
      list->AddText({extent.x < tile ? min.x + (tile - extent.x) * 0.5f : min.x, square.y + 2.0f},
                    ImGui::GetColorU32(ImGuiCol_Text), row.name.c_str());
      list->PopClipRect();
    }
  }
  const int lines = (static_cast<int>(m_rows.size()) + columns - 1) / columns;
  ImGui::SetCursorScreenPos({origin.x, origin.y + static_cast<float>(lines) * cell.y});
  ImGui::Dummy({1.0f, 1.0f}); // the extent of the tiles placed by position
  endSelection(keyboard);
  drawBackgroundMenu();
  ImGui::EndChild();
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
  if (ImGui::MenuItem("Cut", "Ctrl+X", false, editable && static_cast<bool>(m_moveMany))) {
    m_cut = filesOf(&row);
  }
  if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_cut.empty() && m_moveMany)) {
    queueMoves(m_cut, currentFolder());
  }
  if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
                      editable && assets::AssetDatabase::sourceType(row.file) && m_duplicate)) {
    queueDuplicate(filesOf(&row));
  }
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
