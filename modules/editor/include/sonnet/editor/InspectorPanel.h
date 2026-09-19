#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sonnet::editor {

// The primary selection's name and components, with widgets generated from flecs reflection
// (docs/editor.md, "Inspector"), or, with nothing selected, the asset the browser inspects: a
// material's values, a texture's import settings, a model's sub-assets. Values change live
// while a widget is used; when the widget is released one command holding the values before
// and after goes on the stack.
class InspectorPanel {
public:
  InspectorPanel(world::World &world, assets::AssetDatabase &assets, Selection &selection, CommandStack &commands);

  // `asset` is what to show when no entity is selected; nil for nothing.
  void draw(bool &open, core::Uuid asset = {});

  // How a member of a primitive type is edited, by its reflected kind and unit.
  enum class ScalarWidget : std::uint8_t {
    Checkbox,
    Float,
    Degrees, // a float in radians, shown in degrees
    Double,
    Int,
    UInt,
    Int64,
    UInt64,
    Entity,
    Unsupported, // also any member that is not a primitive
  };

  // The asset type a component member of type Uuid refers to, by the member's name; none for
  // a member the pickers do not know.
  [[nodiscard]] static std::optional<assets::AssetType> assetTypeOfMember(std::string_view member);
  [[nodiscard]] static ScalarWidget scalarWidget(const flecs::world &world, const ecs_member_t &member);

private:
  struct Edit {
    core::Uuid entity;
    flecs::entity_t component{0};
    nlohmann::json before;
  };
  struct MaterialEdit {
    core::Uuid material;
    assets::MaterialSource before;
  };

  void drawEntity(flecs::entity entity);
  void drawName(flecs::entity entity);
  void drawComponent(flecs::entity entity, const world::ComponentInfo &info);
  void drawAddComponent(flecs::entity entity);
  void drawStruct(flecs::entity type, void *data);
  void drawMember(const ecs_member_t &member, void *data);
  bool drawEnum(flecs::entity type, void *data);
  // A button naming the asset that opens a filtered list of the type, and a drop target for the
  // browser's rows; returns true when the value changed.
  bool drawAssetPicker(core::Uuid &value, std::optional<assets::AssetType> type);
  void drawAsset(core::Uuid uuid);
  void drawMaterial(const assets::AssetInfo &info);
  void drawTexture(const assets::AssetInfo &info);
  // Records activation and edits of the last widget for the pending command.
  void track();

  world::World &m_world;
  assets::AssetDatabase &m_assets;
  Selection &m_selection;
  CommandStack &m_commands;
  std::optional<Edit> m_edit;
  std::optional<MaterialEdit> m_materialEdit;
  bool m_activated{false};
  bool m_deactivatedAfterEdit{false};
  bool m_deactivated{false};
  std::string m_nameBuffer;
  core::Uuid m_nameEntity;
  std::string m_pickerFilter;
};

} // namespace sonnet::editor
