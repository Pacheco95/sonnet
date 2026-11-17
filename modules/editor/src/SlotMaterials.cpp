#include <sonnet/editor/SlotMaterials.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace sonnet::editor {

namespace {

// A UTF-8 file name as a path, which the narrow encoding of Windows would garble.
std::filesystem::path utf8Path(const std::string &name) {
  return std::filesystem::path{std::u8string_view{reinterpret_cast<const char8_t *>(name.data()), name.size()}};
}

} // namespace

std::vector<core::Uuid> setSlotMaterial(std::vector<core::Uuid> slots, std::size_t slot, const core::Uuid &material) {
  if (slots.size() <= slot) {
    slots.resize(slot + 1);
  }
  slots[slot] = material;
  while (!slots.empty() && slots.back().isNil()) {
    slots.pop_back();
  }
  return slots;
}

std::filesystem::path editableCopyPath(const assets::AssetInfo &material) {
  std::string name;
  for (const char character : material.name) {
    constexpr std::string_view unsafe = "<>:\"/\\|?*";
    const auto byte = static_cast<unsigned char>(character);
    name += byte < 0x20 || unsafe.find(character) != std::string_view::npos ? '_' : character;
  }
  if (name.empty() || name.find_first_not_of(". ") == std::string::npos) {
    name = "material";
  }
  const std::filesystem::path folder = material.source.parent_path();
  std::filesystem::path file = folder / utf8Path(name + ".material.json");
  for (int number = 2; std::filesystem::exists(file); ++number) {
    file = folder / utf8Path(std::format("{} {}.material.json", name, number));
  }
  return file;
}

core::Result<core::Uuid> makeEditableCopy(assets::AssetDatabase &assets, const core::Uuid &material) {
  const assets::AssetInfo *info = assets.find(material);
  if (info == nullptr || info->type != assets::AssetType::Material || info->parent.isNil()) {
    return std::unexpected(
        core::Error{std::format("{} is not a glTF material", material.toString()), core::ErrorCategory::Io});
  }
  const assets::MaterialSource *source = assets.materialSource(material);
  if (source == nullptr) {
    return std::unexpected(
        core::Error{std::format("glTF material \"{}\" does not load", info->name), core::ErrorCategory::Io});
  }
  // Copied before createMaterial registers the file, which may move the database's storage.
  const assets::MaterialSource values = *source;
  return assets.createMaterial(editableCopyPath(*info), values);
}

} // namespace sonnet::editor
