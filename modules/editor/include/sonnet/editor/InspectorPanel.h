#pragma once

#include <sonnet/editor/CommandStack.h>
#include <sonnet/editor/Selection.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/audio/AudioDevice.h>
#include <sonnet/scripting/ScriptRuntime.h>
#include <sonnet/world/World.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sonnet::editor {

// The primary selection's name and components, with widgets generated from flecs reflection
// (docs/editor.md, "Inspector"), plus, in a tab beside it, the asset the browser inspects: a
// material's values, a texture's import settings, a model's sub-assets. Values change live
// while a widget is used; when the widget is released one command holding the values before
// and after goes on the stack.
class InspectorPanel {
public:
  InspectorPanel(world::World &world, assets::AssetDatabase &assets, Selection &selection, CommandStack &commands);

  // `asset` is the inspected asset, shown in an Asset tab beside the selection (alone when nothing is
  // selected); nil for none.
  void draw(bool &open, core::Uuid asset = {});
  // Opens a file at a line in the external editor; the script asset view's Edit button uses it.
  void setOpenHandler(std::function<void(const std::string &, int)> handler) {
    m_open = std::move(handler);
  }
  // Where the script slots get the properties a class declares; without one a slot shows its script
  // and no properties.
  void setScripts(scripting::IScriptRuntime *scripts) noexcept {
    m_scripts = scripts;
  }
  // Starts an emitter's particles over, for the Restart button under a ParticleEmitter; without
  // one the button is not shown.
  void setParticleReset(std::function<void(flecs::entity)> reset) {
    m_particleReset = std::move(reset);
  }
  // What the sound asset view decodes and previews with; without one it shows the file only.
  void setAudio(audio::IAudioDevice *audio) noexcept {
    m_audio = audio;
  }

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

  void drawSelection(flecs::entity entity);
  void drawEntity(flecs::entity entity);
  void drawName(flecs::entity entity);
  void drawComponent(flecs::entity entity, const world::ComponentInfo &info);
  void drawAddComponent(flecs::entity entity);
  // The `Scripts` component: the slot list with its buttons and one widget per declared property.
  void drawScripts(flecs::entity entity);
  void drawProperty(scripting::ScriptSlot &slot, const scripting::PropertyDecl &property);
  // A drop target on the last widget for a script asset from the browser: one more slot.
  void acceptScriptDrop(flecs::entity entity);
  // A button naming the entity that opens a list of the scene's, and a drop target for the
  // hierarchy's rows; returns true when the value changed.
  bool drawEntityPicker(core::Uuid &value);
  void drawStruct(flecs::entity type, void *data);
  void drawMember(const ecs_member_t &member, void *data);
  bool drawEnum(flecs::entity type, void *data);
  // A button naming the asset that opens a filtered list of the type, and a drop target for the
  // browser's rows; returns true when the value changed.
  bool drawAssetPicker(core::Uuid &value, std::optional<assets::AssetType> type);
  void drawAsset(core::Uuid uuid);
  void drawMaterial(const assets::AssetInfo &info);
  void drawTexture(const assets::AssetInfo &info);
  void drawScript(const assets::AssetInfo &info);
  void drawSound(const assets::AssetInfo &info);
  // Records activation and edits of the last widget for the pending command.
  void track();

  world::World &m_world;
  assets::AssetDatabase &m_assets;
  Selection &m_selection;
  CommandStack &m_commands;
  std::optional<Edit> m_edit;
  std::optional<MaterialEdit> m_materialEdit;
  core::Uuid m_shownAsset; // the asset of the last frame, to bring its tab forward when it changes
  bool m_activated{false};
  bool m_deactivatedAfterEdit{false};
  bool m_deactivated{false};
  std::string m_nameBuffer;
  core::Uuid m_nameEntity;
  std::string m_pickerFilter;
  std::function<void(const std::string &, int)> m_open;
  audio::IAudioDevice *m_audio{nullptr};
  std::function<void(flecs::entity)> m_particleReset;
  scripting::IScriptRuntime *m_scripts{nullptr};
};

} // namespace sonnet::editor
