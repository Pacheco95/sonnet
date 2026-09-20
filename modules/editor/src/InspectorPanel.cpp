#include <sonnet/editor/InspectorPanel.h>

#include <sonnet/editor/AssetBrowserPanel.h>
#include <sonnet/editor/AssetCommands.h>
#include <sonnet/editor/EntityCommands.h>

#include <sonnet/core/Log.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
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

bool containsIgnoringCase(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  return std::ranges::search(text, needle, [&](char a, char b) {
           return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
         }).begin() != text.end();
}

constexpr std::array<const char *, 3> AlphaModeNames{"Opaque", "Mask", "Blend"};
constexpr std::array<const char *, 3> WrapNames{"Repeat", "ClampToEdge", "MirroredRepeat"};

template <typename Enum, std::size_t N>
bool drawEnumCombo(const char *label, Enum &value, const std::array<const char *, N> &names) {
  bool changed = false;
  if (ImGui::BeginCombo(label, names[static_cast<std::size_t>(value)])) {
    for (std::size_t i = 0; i < N; ++i) {
      if (ImGui::Selectable(names[i], static_cast<std::size_t>(value) == i)) {
        value = static_cast<Enum>(i);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

} // namespace

InspectorPanel::InspectorPanel(world::World &world, assets::AssetDatabase &assets, Selection &selection,
                               CommandStack &commands)
    : m_world(world), m_assets(assets), m_selection(selection), m_commands(commands) {
}

std::optional<assets::AssetType> InspectorPanel::assetTypeOfMember(std::string_view member) {
  if (member == "mesh") {
    return assets::AssetType::Mesh;
  }
  if (member == "material") {
    return assets::AssetType::Material;
  }
  if (member == "map") {
    return assets::AssetType::Environment;
  }
  if (member == "script") {
    return assets::AssetType::Script;
  }
  if (member == "sound") {
    return assets::AssetType::Sound;
  }
  if (member == "skin") {
    return assets::AssetType::Skin;
  }
  if (member == "clip") {
    return assets::AssetType::Animation;
  }
  if (member.ends_with("Texture")) {
    return assets::AssetType::Texture;
  }
  return std::nullopt;
}

InspectorPanel::ScalarWidget InspectorPanel::scalarWidget(const flecs::world &world, const ecs_member_t &member) {
  const flecs::Primitive *primitive = flecs::entity{world, member.type}.try_get<flecs::Primitive>();
  if (primitive == nullptr) {
    return ScalarWidget::Unsupported;
  }
  // Kinds, from flecs::meta, not the type entities of the same names in flecs itself. They are
  // not constant expressions, hence no switch.
  const flecs::meta::primitive_kind_t kind = primitive->kind;
  if (kind == flecs::meta::Bool) {
    return ScalarWidget::Checkbox;
  }
  if (kind == flecs::meta::F32) {
    return member.unit == world.id<flecs::units::angle::Radians>() ? ScalarWidget::Degrees : ScalarWidget::Float;
  }
  if (kind == flecs::meta::F64) {
    return ScalarWidget::Double;
  }
  if (kind == flecs::meta::I32) {
    return ScalarWidget::Int;
  }
  if (kind == flecs::meta::U32) {
    return ScalarWidget::UInt;
  }
  if (kind == flecs::meta::I64) {
    return ScalarWidget::Int64;
  }
  if (kind == flecs::meta::U64) {
    return ScalarWidget::UInt64;
  }
  if (kind == flecs::meta::Entity) {
    return ScalarWidget::Entity;
  }
  return ScalarWidget::Unsupported;
}

void InspectorPanel::drawScript(const assets::AssetInfo &info) {
  const assets::ScriptSource *source = m_assets.script(info.uuid);
  if (source == nullptr) {
    ImGui::TextDisabled("(unreadable)");
    return;
  }
  const auto lines = std::ranges::count(source->code, '\n');
  ImGui::Text("Lua, %td lines", lines);
  ImGui::TextDisabled("Changes reload into the running game.");
  if (m_open && ImGui::Button("Edit")) {
    m_open(info.source.string(), 1);
  }
}

void InspectorPanel::track() {
  m_activated = m_activated || ImGui::IsItemActivated();
  m_deactivatedAfterEdit = m_deactivatedAfterEdit || ImGui::IsItemDeactivatedAfterEdit();
  m_deactivated = m_deactivated || ImGui::IsItemDeactivated();
}

void InspectorPanel::draw(bool &open, core::Uuid asset) {
  if (!ImGui::Begin("Inspector", &open)) {
    ImGui::End();
    return;
  }
  const flecs::entity entity = m_world.find(m_selection.primary());
  if (entity) {
    if (m_selection.items().size() > 1) {
      ImGui::TextDisabled("%zu selected, showing the last", m_selection.items().size());
    }
    m_materialEdit.reset();
    drawEntity(entity);
  } else if (!m_selection.empty()) {
    ImGui::TextDisabled("The selection no longer exists");
    m_edit.reset();
  } else if (!asset.isNil()) {
    m_edit.reset();
    drawAsset(asset);
  } else {
    ImGui::TextDisabled("Nothing selected");
    m_edit.reset();
    m_materialEdit.reset();
  }
  ImGui::End();
}

bool InspectorPanel::drawAssetPicker(core::Uuid &value, std::optional<assets::AssetType> type) {
  bool changed = false;
  const std::string label = assetLabel(m_assets, value);
  if (ImGui::Button(label.c_str(), ImVec2{-1.0f, 0.0f})) {
    ImGui::OpenPopup("pick asset");
    m_pickerFilter.clear();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetDragPayload)) {
      core::Uuid::Bytes bytes{};
      std::copy_n(static_cast<const std::uint8_t *>(payload->Data), bytes.size(), bytes.begin());
      const core::Uuid dropped{bytes};
      const assets::AssetInfo *info = m_assets.find(dropped);
      if (info != nullptr && (!type || info->type == *type) && dropped != value) {
        value = dropped;
        changed = true;
      }
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::BeginPopup("pick asset")) {
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "filter", &m_pickerFilter);
    if (ImGui::Selectable("(none)", value.isNil()) && !value.isNil()) {
      value = {};
      changed = true;
    }
    for (const assets::AssetInfo *info : m_assets.assets(type)) {
      if (!containsIgnoringCase(info->name, m_pickerFilter)) {
        continue;
      }
      ImGui::PushID(info->uuid.toString().c_str());
      if (ImGui::Selectable(info->name.c_str(), info->uuid == value) && info->uuid != value) {
        value = info->uuid;
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
  return changed;
}

void InspectorPanel::drawAsset(core::Uuid uuid) {
  const assets::AssetInfo *info = m_assets.find(uuid);
  if (info == nullptr) {
    ImGui::TextDisabled("The asset no longer exists");
    m_materialEdit.reset();
    return;
  }
  ImGui::Text("%s", info->name.c_str());
  ImGui::TextDisabled("%s", assets::toString(info->type).data());
  const std::string source = info->source == "builtin"
                                 ? std::string{"built-in"}
                                 : info->source.lexically_relative(m_assets.projectRoot()).generic_string();
  ImGui::TextDisabled("%s", source.c_str());
  ImGui::TextDisabled("%s", uuid.toString().c_str());
  if (info->source != "builtin" && ImGui::Button("Reimport")) {
    if (const auto result = m_assets.reimport(uuid); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }
  ImGui::Separator();
  switch (info->type) {
  case assets::AssetType::Material:
    drawMaterial(*info);
    break;
  case assets::AssetType::Texture:
    drawTexture(*info);
    break;
  case assets::AssetType::Script:
    drawScript(*info);
    break;
  case assets::AssetType::Mesh: {
    const renderer::MeshHandle mesh = m_assets.mesh(uuid);
    if (mesh) {
      const std::span<const renderer::Submesh> submeshes = m_assets.renderer().submeshes(mesh);
      ImGui::Text("%zu submeshes", submeshes.size());
      for (std::size_t i = 0; i < info->materials.size(); ++i) {
        const std::string label = std::format("slot {}: {}", i, assetLabel(m_assets, info->materials[i]));
        ImGui::TextUnformatted(label.c_str());
      }
    } else {
      ImGui::TextDisabled("(not loaded)");
    }
    break;
  }
  case assets::AssetType::Model:
    for (const assets::AssetInfo *sub : m_assets.assets()) {
      if (sub->parent == uuid) {
        const std::string label = std::format("{} ({})", sub->name, assets::toString(sub->type));
        ImGui::TextUnformatted(label.c_str());
      }
    }
    break;
  case assets::AssetType::Environment:
    ImGui::TextDisabled(m_assets.environment(uuid) ? "loaded" : "not loaded");
    break;
  case assets::AssetType::Sound:
    ImGui::TextDisabled("(no audio device)");
    break;
  case assets::AssetType::Skin:
    if (const assets::Skin *skin = m_assets.skin(uuid)) {
      ImGui::Text("%zu joints", skin->joints.size());
      for (const std::string &joint : skin->joints) {
        ImGui::TextDisabled("%s", joint.c_str());
      }
    } else {
      ImGui::TextDisabled("(not loaded)");
    }
    break;
  case assets::AssetType::Animation:
    if (const assets::AnimationClip *clip = m_assets.animation(uuid)) {
      ImGui::Text("%.2f s, %zu channels", static_cast<double>(clip->duration), clip->channels.size());
    } else {
      ImGui::TextDisabled("(not loaded)");
    }
    break;
  }
}

void InspectorPanel::drawMaterial(const assets::AssetInfo &info) {
  const assets::MaterialSource *current = m_assets.materialSource(info.uuid);
  if (current == nullptr) {
    ImGui::TextDisabled("(failed to load)");
    return;
  }
  if (m_materialEdit && m_materialEdit->material != info.uuid) {
    m_materialEdit.reset();
  }
  // Like a component: the value before the first widget is used is what the command restores.
  const assets::MaterialSource before = m_materialEdit ? m_materialEdit->before : *current;
  assets::MaterialSource edited = *current;
  m_activated = m_deactivatedAfterEdit = m_deactivated = false;
  bool picked = false;
  ImGui::ColorEdit4("Base colour", &edited.baseColor.x);
  track();
  ImGui::ColorEdit3("Emissive", &edited.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
  track();
  ImGui::SliderFloat("Metallic", &edited.metallic, 0.0f, 1.0f);
  track();
  ImGui::SliderFloat("Roughness", &edited.roughness, 0.0f, 1.0f);
  track();
  ImGui::SliderFloat("Normal scale", &edited.normalScale, 0.0f, 2.0f);
  track();
  ImGui::SliderFloat("Occlusion", &edited.occlusionStrength, 0.0f, 1.0f);
  track();
  ImGui::SliderFloat("Alpha cutoff", &edited.alphaCutoff, 0.0f, 1.0f);
  track();
  picked = drawEnumCombo("Alpha mode", edited.alphaMode, AlphaModeNames) || picked;
  picked = drawEnumCombo("Wrap", edited.wrap, WrapNames) || picked;
  if (ImGui::Checkbox("Double sided", &edited.doubleSided)) {
    picked = true;
  }
  const auto texture = [&](const char *label, core::Uuid &value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::PushID(label);
    picked = drawAssetPicker(value, assets::AssetType::Texture) || picked;
    ImGui::PopID();
  };
  texture("Base colour map", edited.baseColorTexture);
  texture("Metallic roughness", edited.metallicRoughnessTexture);
  texture("Normal map", edited.normalTexture);
  texture("Occlusion map", edited.occlusionTexture);
  texture("Emissive map", edited.emissiveTexture);

  if (edited != *current) {
    m_assets.setMaterialSource(info.uuid, edited);
  }
  if (picked) {
    // A combo, a checkbox or a picker is one edit in itself.
    m_commands.push(materialEditCommand(m_assets, info.uuid, before, edited), m_world);
    m_materialEdit.reset();
  } else {
    if (m_activated && !m_materialEdit) {
      m_materialEdit = MaterialEdit{.material = info.uuid, .before = before};
    }
    if (m_materialEdit) {
      if (m_deactivatedAfterEdit) {
        m_commands.push(materialEditCommand(m_assets, info.uuid, m_materialEdit->before, edited), m_world);
        m_materialEdit.reset();
      } else if (m_deactivated) {
        m_materialEdit.reset();
      }
    }
  }
  ImGui::Separator();
  if (info.parent.isNil()) {
    if (ImGui::Button("Save")) {
      if (const auto saved = m_assets.saveMaterial(info.uuid); !saved) {
        SONNET_LOG_ERROR("{}", saved.error().toString());
      }
    }
  } else {
    ImGui::TextDisabled("A glTF material: edits are not written back to the file");
  }
}

void InspectorPanel::drawTexture(const assets::AssetInfo &info) {
  if (!info.parent.isNil()) {
    ImGui::TextDisabled("An image inside a glTF file: its colour space follows the materials");
    return;
  }
  const assets::TextureSettings before = m_assets.textureSettings(info.uuid);
  assets::TextureSettings settings = before;
  bool changed = ImGui::Checkbox("sRGB", &settings.srgb);
  changed = ImGui::Checkbox("Mipmaps", &settings.mipmaps) || changed;
  changed = ImGui::Checkbox("Compress", &settings.compress) || changed;
  if (changed) {
    m_commands.push(textureSettingsCommand(m_assets, info.uuid, before, settings), m_world);
  }
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
  } else if (member.type == ecs.id<core::Uuid>()) {
    // An asset reference: a pick is one edit, activated and finished in the same frame.
    if (drawAssetPicker(at<core::Uuid>(field, 0), assetTypeOfMember(name))) {
      m_activated = true;
      m_deactivatedAfterEdit = true;
    }
  } else if (type.has<flecs::Primitive>()) {
    switch (scalarWidget(ecs, member)) {
    case ScalarWidget::Checkbox:
      ImGui::Checkbox("##value", &at<bool>(field, 0));
      break;
    case ScalarWidget::Float:
      ImGui::DragFloat("##value", &at<float>(field, 0), DragSpeed);
      break;
    case ScalarWidget::Degrees: {
      float degrees = glm::degrees(at<float>(field, 0));
      if (ImGui::DragFloat("##value", &degrees, AngleDragSpeed, 0.0f, 0.0f, "%.1f°")) {
        at<float>(field, 0) = glm::radians(degrees);
      }
      break;
    }
    case ScalarWidget::Double:
      ImGui::DragScalar("##value", ImGuiDataType_Double, &at<double>(field, 0), DragSpeed);
      break;
    case ScalarWidget::Int:
      ImGui::DragInt("##value", &at<int>(field, 0));
      break;
    case ScalarWidget::UInt:
      ImGui::DragScalar("##value", ImGuiDataType_U32, &at<std::uint32_t>(field, 0));
      break;
    case ScalarWidget::Int64:
      ImGui::DragScalar("##value", ImGuiDataType_S64, &at<std::int64_t>(field, 0));
      break;
    case ScalarWidget::UInt64:
      ImGui::DragScalar("##value", ImGuiDataType_U64, &at<std::uint64_t>(field, 0));
      break;
    case ScalarWidget::Entity: {
      const flecs::entity referenced{ecs, at<flecs::entity_t>(field, 0)};
      ImGui::TextDisabled("%s", referenced.is_valid() ? referenced.name().c_str() : "(none)");
      break;
    }
    case ScalarWidget::Unsupported:
      ImGui::TextDisabled("(unsupported)");
      break;
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
