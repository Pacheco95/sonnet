#include <sonnet/editor/AssetCommands.h>

#include <sonnet/core/Log.h>

#include <format>
#include <string>
#include <utility>

namespace sonnet::editor {

namespace {

class MaterialEditCommand final : public ICommand {
public:
  MaterialEditCommand(assets::AssetDatabase &assets, core::Uuid material, assets::MaterialSource before,
                      assets::MaterialSource after)
      : m_assets(assets), m_material(material), m_before(std::move(before)), m_after(std::move(after)) {
    const assets::AssetInfo *info = m_assets.find(material);
    m_description = std::format("edit material {}", info != nullptr ? info->name : material.toString());
  }

  void apply(world::World &) override {
    m_assets.setMaterialSource(m_material, m_after);
  }
  void revert(world::World &) override {
    m_assets.setMaterialSource(m_material, m_before);
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  assets::AssetDatabase &m_assets;
  core::Uuid m_material;
  assets::MaterialSource m_before;
  assets::MaterialSource m_after;
  std::string m_description;
};

class TextureSettingsCommand final : public ICommand {
public:
  TextureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture, assets::TextureSettings before,
                         assets::TextureSettings after)
      : m_assets(assets), m_texture(texture), m_before(before), m_after(after) {
    const assets::AssetInfo *info = m_assets.find(texture);
    m_description = std::format("import settings of {}", info != nullptr ? info->name : texture.toString());
  }

  void apply(world::World &) override {
    set(m_after);
  }
  void revert(world::World &) override {
    set(m_before);
  }
  std::string_view description() const override {
    return m_description;
  }

private:
  void set(const assets::TextureSettings &settings) {
    if (const auto result = m_assets.setTextureSettings(m_texture, settings); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }

  assets::AssetDatabase &m_assets;
  core::Uuid m_texture;
  assets::TextureSettings m_before;
  assets::TextureSettings m_after;
  std::string m_description;
};

} // namespace

std::unique_ptr<ICommand> materialEditCommand(assets::AssetDatabase &assets, core::Uuid material,
                                              assets::MaterialSource before, assets::MaterialSource after) {
  return std::make_unique<MaterialEditCommand>(assets, material, std::move(before), std::move(after));
}

std::unique_ptr<ICommand> textureSettingsCommand(assets::AssetDatabase &assets, core::Uuid texture,
                                                 assets::TextureSettings before, assets::TextureSettings after) {
  return std::make_unique<TextureSettingsCommand>(assets, texture, before, after);
}

} // namespace sonnet::editor
