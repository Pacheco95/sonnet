#include <sonnet/editor/HierarchyPanel.h>

#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/ScriptSlots.h>

#include <sonnet/audio/Components.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/Components.h>
#include <sonnet/scripting/Components.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <utility>

namespace sonnet::editor {

namespace {

std::string nameOf(flecs::entity entity) {
  const world::Name *name = entity.try_get<world::Name>();
  return name != nullptr && !name->value.empty() ? name->value : "(unnamed)";
}

// The kinds of entity the filter offers, each decided by which components the entity has.
// The order matches KindNames.
using KindTest = bool (*)(flecs::entity);
const std::array<KindTest, 10> Kinds{{
    [](flecs::entity e) { return e.has<world::MeshRenderer>(); },
    [](flecs::entity e) { return e.has<world::SkinnedMesh>(); },
    [](flecs::entity e) { return e.has<world::Animator>(); },
    [](flecs::entity e) {
      return e.has<world::PointLight>() || e.has<world::SpotLight>() || e.has<world::DirectionalLight>();
    },
    [](flecs::entity e) { return e.has<world::Camera>(); },
    [](flecs::entity e) { return e.has<audio::AudioSource>() || e.has<audio::AudioListener>(); },
    [](flecs::entity e) { return e.has<scripting::Scripts>(); },
    [](flecs::entity e) { return e.has<physics::RigidBody>(); },
    [](flecs::entity e) {
      return e.has<physics::BoxCollider>() || e.has<physics::SphereCollider>() || e.has<physics::CapsuleCollider>() ||
             e.has<physics::MeshCollider>();
    },
    [](flecs::entity e) { return e.has<world::Environment>(); },
}};

constexpr std::array<const char *, 10> KindNames{"Meshes",    "Skinned meshes", "Animators", "Lights",
                                                 "Cameras",   "Audio",          "Scripts",   "Rigid bodies",
                                                 "Colliders", "Environments"};

bool containsIgnoringCase(std::string_view text, std::string_view needle) {
  const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  return std::ranges::search(text, needle, [&](char a, char b) {
           return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
         }).begin() != text.end();
}

} // namespace

std::span<const char *const> HierarchyPanel::kindNames() noexcept {
  return KindNames;
}

bool HierarchyPanel::matchesFilter(flecs::entity entity) const {
  if (!m_filter.empty() && !containsIgnoringCase(nameOf(entity), m_filter)) {
    return false;
  }
  if (m_kinds.empty()) {
    return true;
  }
  for (std::size_t i = 0; i < Kinds.size(); ++i) {
    if (m_kinds.checked(i) && Kinds[i](entity)) {
      return true;
    }
  }
  return false;
}

// True when the entity or anything under it matches: the rows the pruned tree keeps.
bool HierarchyPanel::subtreeMatches(flecs::entity entity) const {
  if (matchesFilter(entity)) {
    return true;
  }
  const std::vector<flecs::entity> children = m_world.children(entity);
  return std::ranges::any_of(children, [&](flecs::entity child) { return subtreeMatches(child); });
}

void HierarchyPanel::collectVisible(flecs::entity entity, std::vector<core::Uuid> &out) const {
  if (filtering() && !subtreeMatches(entity)) {
    return;
  }
  out.push_back(m_world.uuidOf(entity));
  for (const flecs::entity child : m_world.children(entity)) {
    collectVisible(child, out);
  }
}

std::vector<core::Uuid> HierarchyPanel::visibleEntities() const {
  std::vector<core::Uuid> out;
  for (const flecs::entity root : m_world.roots()) {
    if (!root.has<world::EditorOnly>()) {
      collectVisible(root, out);
    }
  }
  return out;
}

HierarchyPanel::HierarchyPanel(world::World &world, Selection &selection, CommandStack &commands,
                               std::function<void()> focus)
    : m_world(world), m_selection(selection), m_commands(commands), m_focus(std::move(focus)) {
}

void HierarchyPanel::draw(bool &open) {
  if (!ImGui::Begin("Hierarchy", &open)) {
    ImGui::End();
    return;
  }
  // A menu click lands mid-frame, so the request is applied from the next frame's first node on.
  m_activeOpen = std::exchange(m_pendingOpen, std::nullopt);
  // Right after Begin the last item is the window's title bar, or its tab when docked (imgui#7914).
  if (ImGui::BeginPopupContextItem("tab menu")) {
    drawBackgroundMenu();
    ImGui::EndPopup();
  }
  ImGui::SetNextItemWidth(160.0f);
  ImGui::InputTextWithHint("##filter", "filter", &m_filter);
  ImGui::SameLine();
  typeFilterCombo("##kind", 130.0f, kindNames(), m_kinds);
  for (const flecs::entity root : m_world.roots()) {
    if (!root.has<world::EditorOnly>() && (!filtering() || subtreeMatches(root))) {
      drawNode(root);
    }
  }
  // The rest of the window: a drop target for the root and the background context menu.
  const ImVec2 remaining = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("background", ImVec2{std::max(remaining.x, 1.0f), std::max(remaining.y, 24.0f)});
  acceptDrop({});
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    m_selection.clear();
  }
  // Anywhere in the window away from the rows, and the drop area below them, opens the same menu, so the tree's
  // menu never needs scrolling to.
  if (ImGui::BeginPopupContextWindow("window menu",
                                     ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    drawBackgroundMenu();
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupContextItem("background menu")) {
    drawBackgroundMenu();
    ImGui::EndPopup();
  }
  ImGui::End();
}

void HierarchyPanel::drawBackgroundMenu() {
  if (ImGui::MenuItem("Expand all")) {
    setOpenRecursive({}, true);
  }
  if (ImGui::MenuItem("Collapse all")) {
    setOpenRecursive({}, false);
  }
  ImGui::Separator();
  drawCreateMenu({});
}

void HierarchyPanel::setOpenRecursive(core::Uuid root, bool open) {
  m_pendingOpen = PendingOpen{root, open};
}

// Closed nodes do not draw their children, so the open state of a whole subtree is written to the window's
// storage under the ids the children will have: an open tree node pushes its own id, then each child its pick id.
void HierarchyPanel::applyOpenToDescendants(flecs::entity entity, bool open) {
  for (const flecs::entity child : m_world.children(entity)) {
    ImGui::PushID(static_cast<int>(world::World::pickId(child)));
    const ImGuiID node = ImGui::GetID("node");
    ImGui::GetStateStorage()->SetInt(node, open ? 1 : 0);
    ImGui::PushID("node");
    applyOpenToDescendants(child, open);
    ImGui::PopID();
    ImGui::PopID();
  }
}

void HierarchyPanel::drawNode(flecs::entity entity) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  std::vector<flecs::entity> children = m_world.children(entity);
  const bool filtered = filtering();
  if (filtered) {
    std::erase_if(children, [&](flecs::entity child) { return !subtreeMatches(child); });
  }
  const bool ancestorOnly = filtered && !matchesFilter(entity);
  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
  if (children.empty()) {
    flags |= ImGuiTreeNodeFlags_Leaf;
  }
  if (m_selection.contains(uuid)) {
    flags |= ImGuiTreeNodeFlags_Selected;
  }
  std::string label = nameOf(entity);
  if (m_world.isInstance(entity)) {
    label += " (instance)";
  }
  if (entity.has<world::Disabled>()) {
    label += " (disabled)";
  }
  ImGui::PushID(static_cast<int>(world::World::pickId(entity)));
  if (m_activeOpen && (m_activeOpen->root.isNil() || m_activeOpen->root == uuid)) {
    ImGui::SetNextItemOpen(m_activeOpen->open, ImGuiCond_Always);
    ImGui::PushID("node");
    applyOpenToDescendants(entity, m_activeOpen->open);
    ImGui::PopID();
  }
  // While filtering, the ancestors of matches are open, and those that do not match are greyed.
  if (filtered && !children.empty()) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
  }
  if (ancestorOnly) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  }
  const bool opened = ImGui::TreeNodeEx("node", flags, "%s", label.c_str());
  if (ancestorOnly) {
    ImGui::PopStyleColor();
  }
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
    selectClicked(entity);
  }
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
    m_focus();
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(EntityDragPayload, uuid.bytes().data(), uuid.bytes().size());
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDragDropSource();
  }
  acceptDrop(uuid);
  if (ImGui::BeginPopupContextItem("entity menu")) {
    if (!m_selection.contains(uuid)) {
      m_selection.select(uuid);
    }
    drawContextMenu(entity);
    ImGui::EndPopup();
  }
  if (opened) {
    for (const flecs::entity child : children) {
      drawNode(child);
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void HierarchyPanel::selectClicked(flecs::entity entity) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  const ImGuiIO &io = ImGui::GetIO();
  m_selection.select(uuid, io.KeyCtrl    ? Selection::Mode::Toggle
                           : io.KeyShift ? Selection::Mode::Add
                                         : Selection::Mode::Replace);
}

void HierarchyPanel::acceptDrop(core::Uuid newParent) {
  if (!ImGui::BeginDragDropTarget()) {
    return;
  }
  // A model from the asset browser: an instance of its prefab under the drop target.
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetDragPayload)) {
    core::Uuid::Bytes bytes{};
    std::copy_n(static_cast<const std::uint8_t *>(payload->Data), bytes.size(), bytes.begin());
    const core::Uuid dropped{bytes};
    if (const flecs::entity prefab = m_world.find(dropped); prefab && prefab.has(flecs::Prefab)) {
      instantiatePrefab(prefab, newParent);
    } else if (const assets::AssetInfo *asset = m_assets != nullptr ? m_assets->find(dropped) : nullptr;
               asset != nullptr && asset->type == assets::AssetType::Script && !newParent.isNil()) {
      // A script dropped on an entity: one more slot (ADR-0022).
      if (auto command = appendScriptCommand(m_world, newParent, dropped)) {
        m_commands.push(std::move(command), m_world);
      }
    }
  }
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(EntityDragPayload)) {
    core::Uuid::Bytes bytes{};
    if (payload->DataSize == static_cast<int>(bytes.size())) {
      std::copy_n(static_cast<const std::uint8_t *>(payload->Data), bytes.size(), bytes.begin());
      const core::Uuid dragged{bytes};
      const flecs::entity entity = m_world.find(dragged);
      const flecs::entity parent = m_world.find(newParent);
      const bool valid = entity && dragged != newParent && (!parent || !m_world.isDescendant(parent, entity)) &&
                         m_world.parentOf(entity) != parent;
      if (valid) {
        m_commands.push(reparentCommand(dragged, newParent), m_world);
      }
    }
  }
  ImGui::EndDragDropTarget();
}

void HierarchyPanel::drawCreateMenu(core::Uuid parent) {
  if (ImGui::MenuItem("Empty")) {
    createEntity("Entity", parent);
  }
  for (const assets::builtin::Entry &entry : assets::builtin::all()) {
    if (ImGui::MenuItem(entry.name)) {
      createMeshEntity(entry.name, entry.uuid(), parent);
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Camera")) {
    createEntity("Camera", parent, {{"Camera", nullptr}});
  }
  if (ImGui::MenuItem("Directional light")) {
    createEntity("Directional light", parent, {{"DirectionalLight", nullptr}});
  }
  if (ImGui::MenuItem("Point light")) {
    createEntity("Point light", parent, {{"PointLight", nullptr}});
  }
  if (ImGui::MenuItem("Spot light")) {
    createEntity("Spot light", parent, {{"SpotLight", nullptr}});
  }
  const std::vector<flecs::entity> prefabs = m_world.prefabs();
  if (!prefabs.empty()) {
    ImGui::Separator();
    if (ImGui::BeginMenu("Prefab instance")) {
      for (const flecs::entity prefab : prefabs) {
        ImGui::PushID(static_cast<int>(world::World::pickId(prefab)));
        if (ImGui::MenuItem(nameOf(prefab).c_str())) {
          instantiatePrefab(prefab, parent);
        }
        ImGui::PopID();
      }
      ImGui::EndMenu();
    }
  }
}

void HierarchyPanel::drawContextMenu(flecs::entity entity) {
  if (ImGui::MenuItem("Focus", "F")) {
    m_focus();
  }
  if (ImGui::MenuItem("Expand all")) {
    setOpenRecursive(m_world.uuidOf(entity), true);
  }
  if (ImGui::MenuItem("Collapse all")) {
    setOpenRecursive(m_world.uuidOf(entity), false);
  }
  ImGui::Separator();
  if (ImGui::BeginMenu("Create child")) {
    drawCreateMenu(m_world.uuidOf(entity));
    ImGui::EndMenu();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
    duplicateSelection();
  }
  if (ImGui::MenuItem("Delete", "Del")) {
    deleteSelection();
  }
}

void HierarchyPanel::createEntity(std::string_view name, core::Uuid parent, nlohmann::json components) {
  const core::Uuid uuid = core::Uuid::generate();
  m_commands.push(createEntityCommand(std::string{name}, parent, std::move(components), uuid), m_world);
  m_selection.select(uuid);
}

void HierarchyPanel::createMeshEntity(std::string_view name, core::Uuid mesh, core::Uuid parent) {
  createEntity(name, parent, {{"MeshRenderer", {{"mesh", mesh.toString()}}}});
}

void HierarchyPanel::instantiatePrefab(flecs::entity prefab, core::Uuid parent) {
  const core::Uuid uuid = core::Uuid::generate();
  m_commands.push(instantiatePrefabCommand(m_world.uuidOf(prefab), nameOf(prefab), parent, uuid), m_world);
  m_selection.select(uuid);
}

void HierarchyPanel::deleteSelection() {
  // Only the outermost selected entities: a selected child goes with its selected ancestor.
  std::vector<std::unique_ptr<ICommand>> commands;
  for (const core::Uuid uuid : m_selection.items()) {
    const flecs::entity entity = m_world.find(uuid);
    if (!entity) {
      continue;
    }
    const bool covered = std::ranges::any_of(m_selection.items(), [&](const core::Uuid &other) {
      const flecs::entity ancestor = m_world.find(other);
      return other != uuid && ancestor && m_world.isDescendant(entity, ancestor);
    });
    if (!covered) {
      commands.push_back(deleteEntityCommand(uuid));
    }
  }
  if (commands.empty()) {
    return;
  }
  const std::string description = commands.size() == 1 ? std::string{commands[0]->description()}
                                                       : std::format("delete {} entities", commands.size());
  m_commands.push(compositeCommand(description, std::move(commands)), m_world);
  m_selection.clear();
}

void HierarchyPanel::duplicateSelection() {
  std::vector<std::unique_ptr<ICommand>> commands;
  std::vector<core::Uuid> copies;
  for (const core::Uuid uuid : m_selection.items()) {
    if (m_world.find(uuid)) {
      copies.push_back(core::Uuid::generate());
      commands.push_back(duplicateEntityCommand(uuid, copies.back()));
    }
  }
  if (commands.empty()) {
    return;
  }
  const std::string description =
      commands.size() == 1 ? std::string{"duplicate"} : std::format("duplicate {} entities", commands.size());
  m_commands.push(compositeCommand(description, std::move(commands)), m_world);
  m_selection.clear();
  for (const core::Uuid copy : copies) {
    m_selection.select(copy, Selection::Mode::Add);
  }
}

} // namespace sonnet::editor
