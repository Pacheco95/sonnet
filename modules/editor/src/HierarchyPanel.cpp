#include <sonnet/editor/HierarchyPanel.h>

#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Log.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace sonnet::editor {

namespace {

constexpr const char *DragPayload = "sonnet_entity";

struct PrimitiveEntry {
  world::Primitive primitive;
  const char *name;
};
constexpr std::array<PrimitiveEntry, 5> Primitives{{{world::Primitive::Box, "Box"},
                                                    {world::Primitive::Sphere, "Sphere"},
                                                    {world::Primitive::Plane, "Plane"},
                                                    {world::Primitive::Cylinder, "Cylinder"},
                                                    {world::Primitive::Capsule, "Capsule"}}};

const char *primitiveName(world::Primitive primitive) {
  const auto it = std::ranges::find(Primitives, primitive, &PrimitiveEntry::primitive);
  return it != Primitives.end() ? it->name : "Mesh";
}

std::string nameOf(flecs::entity entity) {
  const world::Name *name = entity.try_get<world::Name>();
  return name != nullptr && !name->value.empty() ? name->value : "(unnamed)";
}

} // namespace

HierarchyPanel::HierarchyPanel(world::World &world, Selection &selection, CommandStack &commands)
    : m_world(world), m_selection(selection), m_commands(commands) {
}

void HierarchyPanel::draw(bool &open) {
  if (!ImGui::Begin("Hierarchy", &open)) {
    ImGui::End();
    return;
  }
  for (const flecs::entity root : m_world.roots()) {
    if (!root.has<world::EditorOnly>()) {
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
  if (ImGui::BeginPopupContextItem("background menu")) {
    drawCreateMenu({});
    ImGui::EndPopup();
  }
  ImGui::End();
}

void HierarchyPanel::drawNode(flecs::entity entity) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  const std::vector<flecs::entity> children = m_world.children(entity);
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
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
  const bool opened = ImGui::TreeNodeEx("node", flags, "%s", label.c_str());
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
    selectClicked(entity);
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(DragPayload, uuid.bytes().data(), uuid.bytes().size());
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
  if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(DragPayload)) {
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
  for (const PrimitiveEntry &entry : Primitives) {
    if (ImGui::MenuItem(entry.name)) {
      createPrimitive(entry.primitive, parent);
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

void HierarchyPanel::createPrimitive(world::Primitive primitive, core::Uuid parent) {
  const char *name = primitiveName(primitive);
  createEntity(name, parent, {{"MeshRenderer", {{"primitive", name}}}});
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
