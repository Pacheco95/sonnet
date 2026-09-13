#include <sonnet/editor/InspectorPanel.h>

#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Log.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace sonnet::editor {

namespace {

constexpr float DragSpeed = 0.01f;
constexpr float AngleDragSpeed = 0.5f; // degrees

template <typename T> T &at(void *data, std::size_t offset) {
  return *reinterpret_cast<T *>(static_cast<std::byte *>(data) + offset);
}

} // namespace

InspectorPanel::InspectorPanel(world::World &world, Selection &selection, CommandStack &commands)
    : m_world(world), m_selection(selection), m_commands(commands) {
}

void InspectorPanel::track() {
  m_activated = m_activated || ImGui::IsItemActivated();
  m_deactivatedAfterEdit = m_deactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit();
  m_deactivated = m_deactivated || ImGui::IsItemDeactivated();
}

void InspectorPanel::draw(bool &open) {
  if (!ImGui::Begin("Inspector", &open)) {
    ImGui::End();
    return;
  }
  const flecs::entity entity = m_world.find(m_selection.primary());
  if (!entity) {
    ImGui::TextDisabled(m_selection.empty() ? "Nothing selected" : "The selection no longer exists");
    m_edit.reset();
  } else {
    if (m_selection.items().size() > 1) {
      ImGui::TextDisabled("%zu selected, showing the last", m_selection.items().size());
    }
    drawEntity(entity);
  }
  ImGui::End();
}

void InspectorPanel::drawEntity(flecs::entity entity) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  if (m_edit && m_edit->entity != uuid) {
    m_edit.reset(); // the selection changed under a pending edit
  }
  drawName(entity);
  if (const flecs::entity prefab = m_world.prefabOf(entity)) {
    const world::Name *name = prefab.try_get<world::Name>();
    ImGui::TextDisabled("Instance of %s", name != nullptr ? name->value.c_str() : "prefab");
  }
  ImGui::TextDisabled("%s", uuid.toString().c_str());
  ImGui::Separator();
  for (const world::ComponentInfo &info : m_world.components()) {
    if (entity.has(info.id)) {
      drawComponent(entity, info);
    }
  }
  ImGui::Spacing();
  drawAddComponent(entity);
}

void InspectorPanel::drawName(flecs::entity entity) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  const world::Name *name = entity.try_get<world::Name>();
  const std::string current = name != nullptr ? name->value : std::string{};
  if (m_nameEntity != uuid || !ImGui::IsAnyItemActive()) {
    m_nameBuffer = current;
    m_nameEntity = uuid;
  }
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputText("##name", &m_nameBuffer);
  if (ImGui::IsItemDeactivatedAfterEdit() && m_nameBuffer != current) {
    m_commands.push(renameCommand(uuid, current, m_nameBuffer), m_world);
  }
}

void InspectorPanel::drawComponent(flecs::entity entity, const world::ComponentInfo &info) {
  const core::Uuid uuid = m_world.uuidOf(entity);
  const bool inherited = !entity.owns(info.id);
  const std::string label = inherited ? std::format("{} (inherited)", info.name) : info.name;
  ImGui::PushID(info.name.c_str());
  // The transform is structural: every scene entity has one and it cannot be removed.
  bool keep = true;
  const bool opened = ImGui::CollapsingHeader(label.c_str(), info.name == "Transform" ? nullptr : &keep,
                                              ImGuiTreeNodeFlags_DefaultOpen);
  if (!keep) {
    const nlohmann::json before = info.tag ? nlohmann::json{} : m_world.componentToJson(entity, info.id);
    m_commands.push(componentCommand(uuid, info.name, std::make_optional(before), std::nullopt,
                                     std::format("remove {}", info.name)),
                    m_world);
    ImGui::PopID();
    return;
  }
  if (opened && !info.tag) {
    // What the pending command restores: captured before any widget of this frame ran.
    const nlohmann::json before = m_edit ? nlohmann::json{} : m_world.componentToJson(entity, info.id);
    m_activated = m_deactivatedAfterEdit = m_deactivated = false;
    const flecs::entity type{m_world.ecs(), info.id};
    // An inherited value is edited on a copy and becomes the instance's own only when a widget
    // is used; ensure on the shared value would override it just by being looked at. Registered
    // components are plain data, so the copy is a byte copy.
    const std::size_t size = static_cast<std::size_t>(type.get<flecs::Component>().size);
    std::vector<std::byte> copy;
    void *data = nullptr;
    if (inherited) {
      copy.resize(size);
      std::memcpy(copy.data(), entity.get(info.id), size);
      data = copy.data();
    } else {
      data = entity.ensure(info.id);
    }
    drawStruct(type, data);
    if (inherited && m_activated) {
      std::memcpy(entity.ensure(info.id), copy.data(), size);
    }
    if (m_activated && !m_edit) {
      m_edit = Edit{.entity = uuid, .component = info.id, .before = before};
    }
    if (m_edit && m_edit->component == info.id) {
      if (m_deactivatedAfterEdit) {
        entity.modified(info.id);
        m_commands.push(componentCommand(uuid, info.name, std::make_optional(m_edit->before),
                                         std::make_optional(m_world.componentToJson(entity, info.id)),
                                         std::format("edit {}", info.name)),
                        m_world);
        m_edit.reset();
      } else if (m_deactivated) {
        m_edit.reset(); // released without a change
      }
    }
  }
  ImGui::PopID();
}

void InspectorPanel::drawAddComponent(flecs::entity entity) {
  if (ImGui::Button("Add component", ImVec2{-1.0f, 0.0f})) {
    ImGui::OpenPopup("add component");
  }
  if (!ImGui::BeginPopup("add component")) {
    return;
  }
  for (const world::ComponentInfo &info : m_world.components()) {
    if (entity.has(info.id)) {
      continue;
    }
    if (ImGui::MenuItem(info.name.c_str())) {
      m_commands.push(componentCommand(m_world.uuidOf(entity), info.name, std::nullopt,
                                       std::make_optional(nlohmann::json{}), std::format("add {}", info.name)),
                      m_world);
    }
  }
  ImGui::EndPopup();
}

void InspectorPanel::drawStruct(flecs::entity type, void *data) {
  const flecs::Struct *layout = type.try_get<flecs::Struct>();
  if (layout == nullptr) {
    ImGui::TextDisabled("(no reflection data)");
    return;
  }
  const auto *members = ecs_vec_first_t(&layout->members, ecs_member_t);
  const auto count = static_cast<std::size_t>(ecs_vec_count(&layout->members));
  for (std::size_t i = 0; i < count; ++i) {
    ImGui::PushID(static_cast<int>(i));
    drawMember(members[i], data);
    ImGui::PopID();
  }
}

bool InspectorPanel::drawEnum(flecs::entity type, void *data) {
  auto &value = at<std::int32_t>(data, 0);
  std::string current;
  std::vector<std::pair<std::string, std::int32_t>> constants;
  type.children([&](flecs::entity constant) {
    if (const std::int32_t *v = constant.try_get_second<std::int32_t>(flecs::Constant)) {
      constants.emplace_back(constant.name().c_str(), *v);
      if (*v == value) {
        current = constant.name().c_str();
      }
    }
  });
  bool changed = false;
  if (ImGui::BeginCombo("##enum", current.c_str())) {
    for (const auto &[name, constantValue] : constants) {
      if (ImGui::Selectable(name.c_str(), constantValue == value)) {
        value = constantValue;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

void InspectorPanel::drawMember(const ecs_member_t &member, void *data) {
  const flecs::entity type{m_world.ecs(), member.type};
  void *field = static_cast<std::byte *>(data) + member.offset;
  const std::string_view name = member.name;
  ImGui::SetNextItemWidth(-1.0f);
  const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("member", 2, tableFlags)) {
    return;
  }
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(member.name);
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-1.0f);

  const flecs::world &ecs = m_world.ecs();
  if (member.type == ecs.id<glm::vec3>()) {
    auto &value = at<glm::vec3>(field, 0);
    if (name == "color") {
      ImGui::ColorEdit3("##value", &value.x);
    } else {
      ImGui::DragFloat3("##value", &value.x, DragSpeed);
    }
    track();
  } else if (member.type == ecs.id<glm::vec4>()) {
    auto &value = at<glm::vec4>(field, 0);
    if (name == "color") {
      ImGui::ColorEdit4("##value", &value.x);
    } else {
      ImGui::DragFloat4("##value", &value.x, DragSpeed);
    }
    track();
  } else if (member.type == ecs.id<glm::quat>()) {
    // Euler angles in degrees for people; the quaternion stays the stored value.
    auto &value = at<glm::quat>(field, 0);
    glm::vec3 degrees = glm::degrees(glm::eulerAngles(value));
    if (ImGui::DragFloat3("##value", &degrees.x, AngleDragSpeed, 0.0f, 0.0f, "%.1f°")) {
      value = glm::normalize(glm::quat{glm::radians(degrees)});
    }
    track();
  } else if (const flecs::Primitive *primitive = type.try_get<flecs::Primitive>()) {
    // The kinds are not constant expressions, hence no switch.
    const flecs::meta::primitive_kind_t kind = primitive->kind;
    if (kind == flecs::Bool) {
      ImGui::Checkbox("##value", &at<bool>(field, 0));
    } else if (kind == flecs::F32) {
      if (member.unit == ecs.id<flecs::units::angle::Radians>()) {
        float degrees = glm::degrees(at<float>(field, 0));
        if (ImGui::DragFloat("##value", &degrees, AngleDragSpeed, 0.0f, 0.0f, "%.1f°")) {
          at<float>(field, 0) = glm::radians(degrees);
        }
      } else {
        ImGui::DragFloat("##value", &at<float>(field, 0), DragSpeed);
      }
    } else if (kind == flecs::F64) {
      ImGui::DragScalar("##value", ImGuiDataType_Double, &at<double>(field, 0), DragSpeed);
    } else if (kind == flecs::I32) {
      ImGui::DragInt("##value", &at<int>(field, 0));
    } else if (kind == flecs::U32) {
      ImGui::DragScalar("##value", ImGuiDataType_U32, &at<std::uint32_t>(field, 0));
    } else if (kind == flecs::I64) {
      ImGui::DragScalar("##value", ImGuiDataType_S64, &at<std::int64_t>(field, 0));
    } else if (kind == flecs::U64) {
      ImGui::DragScalar("##value", ImGuiDataType_U64, &at<std::uint64_t>(field, 0));
    } else if (kind == flecs::Entity) {
      const flecs::entity referenced{ecs, at<flecs::entity_t>(field, 0)};
      ImGui::TextDisabled("%s", referenced.is_valid() ? referenced.name().c_str() : "(none)");
    } else {
      ImGui::TextDisabled("(unsupported)");
    }
    track();
  } else if (type.has<flecs::Enum>()) {
    drawEnum(type, field);
    track();
  } else if (type.has<flecs::Struct>()) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    drawStruct(type, field);
  } else {
    ImGui::TextDisabled("(unsupported)");
  }
  ImGui::EndTable();
}

} // namespace sonnet::editor
