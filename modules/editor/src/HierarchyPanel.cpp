#include <sonnet/editor/HierarchyPanel.h>

#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/EntityCommands.h>
#include <sonnet/editor/ScriptSlots.h>

#include <sonnet/audio/Components.h>
#include <sonnet/core/Log.h>
#include <sonnet/physics/Components.h>
#include <sonnet/scripting/Components.h>
#include <sonnet/world/DrawList.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <format>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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
  if (m_afterTree) {
    std::exchange(m_afterTree, nullptr)();
  }
  // The rest of the window: a drop target for the root and the background context menu.
  const ImVec2 remaining = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("background", ImVec2{std::max(remaining.x, 1.0f), std::max(remaining.y, 24.0f)});
  acceptDrop({}, false);
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
  acceptDrop(entity, opened && !children.empty());
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

void HierarchyPanel::acceptDrop(flecs::entity row, bool openParent) {
  if (!ImGui::BeginDragDropTarget()) {
    return;
  }
  const core::Uuid rowUuid = row ? m_world.uuidOf(row) : core::Uuid{};
  // A model from the asset browser: an instance of its prefab under the drop target; an environment: the sky.
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetDragPayload)) {
    core::Uuid::Bytes bytes{};
    std::copy_n(static_cast<const std::uint8_t *>(payload->Data), bytes.size(), bytes.begin());
    const core::Uuid dropped{bytes};
    if (const flecs::entity prefab = m_world.find(dropped); prefab && prefab.has(flecs::Prefab)) {
      instantiatePrefab(prefab, rowUuid);
    } else if (const assets::AssetInfo *asset = m_assets != nullptr ? m_assets->find(dropped) : nullptr;
               asset != nullptr && asset->type == assets::AssetType::Script && row) {
      // A script dropped on an entity: one more slot (ADR-0022).
      if (auto command = appendScriptCommand(m_world, rowUuid, dropped)) {
        m_commands.push(std::move(command), m_world);
      }
    } else if (asset != nullptr && asset->type == assets::AssetType::Environment) {
      useEnvironment(dropped, asset->name);
    }
  }
  // An entity: the upper and lower quarters of a row put it before or after the row, the middle under the
  // row, last; below the tree it becomes the last root. The target is drawn only where the drop is allowed.
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
          EntityDragPayload, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
    core::Uuid::Bytes bytes{};
    if (payload->DataSize == static_cast<int>(bytes.size())) {
      std::copy_n(static_cast<const std::uint8_t *>(payload->Data), bytes.size(), bytes.begin());
      const core::Uuid dragged{bytes};
      const flecs::entity entity = m_world.find(dragged);
      // The position of `sibling` among the children of its parent, the dragged entity left out.
      const auto positionOf = [&](flecs::entity sibling) {
        const flecs::entity parent = m_world.parentOf(sibling);
        std::vector<flecs::entity> siblings = parent ? m_world.children(parent) : m_world.roots();
        std::erase(siblings, entity);
        return static_cast<std::size_t>(std::ranges::find(siblings, sibling) - siblings.begin());
      };
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      const float edge = (max.y - min.y) * 0.25f;
      const float mouse = ImGui::GetMousePos().y;
      enum class Zone {
        Before,
        After,
        Onto
      };
      Zone zone = Zone::Onto;
      if (row && mouse < min.y + edge) {
        zone = Zone::Before;
      } else if (row && mouse > max.y - edge) {
        zone = Zone::After;
      }
      core::Uuid parent = rowUuid;
      std::size_t index = std::numeric_limits<std::size_t>::max(); // last
      if (zone == Zone::Before || (zone == Zone::After && !openParent)) {
        parent = m_world.uuidOf(m_world.parentOf(row));
        index = positionOf(row) + (zone == Zone::After ? 1 : 0);
      } else if (zone == Zone::After) {
        index = 0; // first among the children the row shows below it
      }
      if (entity && entity != row && canMove(dragged, parent, index)) {
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImU32 colour = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
        if (zone == Zone::Onto) {
          draw->AddRect(min, max, colour, 0.0f, 0, 2.0f);
        } else {
          const float y = zone == Zone::Before ? min.y : max.y;
          draw->AddLine(ImVec2{min.x, y}, ImVec2{max.x, y}, colour, 2.0f);
        }
        if (payload->IsDelivery()) {
          moveEntity(dragged, parent, index);
        }
      }
    }
  }
  ImGui::EndDragDropTarget();
}

bool HierarchyPanel::canMove(core::Uuid entityUuid, core::Uuid parentUuid, std::size_t index) const {
  const flecs::entity entity = m_world.find(entityUuid);
  const flecs::entity parent = m_world.find(parentUuid);
  // Part of a prefab instance stays under the parent the prefab gives it, but may change place
  // among its siblings (ADR-0032).
  if (!entity || (!parentUuid.isNil() && !parent) || entityUuid == parentUuid ||
      (entity.has<world::PrefabSource>() && m_world.parentOf(entity) != parent) ||
      (parent && m_world.isDescendant(parent, entity))) {
    return false;
  }
  std::vector<flecs::entity> siblings = parent ? m_world.children(parent) : m_world.roots();
  std::erase(siblings, entity);
  index = std::min(index, siblings.size());
  return m_world.parentOf(entity) != parent || m_world.siblingIndex(entity) != index;
}

bool HierarchyPanel::moveEntity(core::Uuid entity, core::Uuid parent, std::size_t index) {
  if (!canMove(entity, parent, index)) {
    return false;
  }
  m_commands.push(reparentCommand(entity, parent, index), m_world);
  return true;
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
  // These replace or destroy entities the tree is still drawing, so they run once it is drawn.
  const core::Uuid uuid = m_world.uuidOf(entity);
  const bool part = entity.has<world::PrefabSource>();
  if (m_world.isInstance(entity) && !part && ImGui::MenuItem("Make local")) {
    m_afterTree = [this, uuid] { m_commands.push(makeLocalCommand(uuid), m_world); };
  }
  if (m_saveAsPrefab && !part && ImGui::MenuItem("Save as prefab")) {
    m_afterTree = [this, uuid] { m_saveAsPrefab(uuid); };
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !part)) {
    m_afterTree = [this] { duplicateSelection(); };
  }
  if (ImGui::MenuItem(part ? "Hide" : "Delete", "Del")) {
    m_afterTree = [this] { deleteSelection(); };
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

void HierarchyPanel::useEnvironment(core::Uuid map, std::string_view name) {
  const flecs::entity entity = world::environmentEntity(m_world);
  if (!entity) {
    createEntity(name, {}, {{"Environment", {{"map", map.toString()}}}});
    return;
  }
  const core::Uuid uuid = m_world.uuidOf(entity);
  const nlohmann::json before = m_world.componentToJson(entity, m_world.ecs().component<world::Environment>());
  nlohmann::json after = before;
  after["map"] = map.toString();
  if (after != before) {
    m_commands.push(
        componentCommand(uuid, "Environment", std::make_optional(before), std::make_optional(after), "set environment"),
        m_world);
  }
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
    if (entity.has<world::PrefabSource>()) {
      // Part of a prefab instance, which would bring it back: hidden instead (ADR-0028).
      if (!entity.has<world::Disabled>()) {
        commands.push_back(componentCommand(uuid, "Disabled", std::nullopt, std::make_optional(nlohmann::json{}),
                                            std::format("hide {}", nameOf(entity))));
      }
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
                                                       : std::format("delete or hide {} entities", commands.size());
  m_commands.push(compositeCommand(description, std::move(commands)), m_world);
  m_selection.clear();
}

void HierarchyPanel::duplicateSelection() {
  std::vector<std::unique_ptr<ICommand>> commands;
  std::vector<core::Uuid> copies;
  for (const core::Uuid uuid : m_selection.items()) {
    // Part of a prefab instance is not copied alone: what it changed is relative to the instance.
    if (const flecs::entity entity = m_world.find(uuid); entity && !entity.has<world::PrefabSource>()) {
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
