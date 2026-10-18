#include <sonnet/editor/Icons.h>

namespace sonnet::editor {

const char *iconForType(std::string_view type) noexcept {
  struct Entry {
    std::string_view type;
    const char *icon;
  };
  static constexpr Entry Entries[] = {
      {"Folder", IconFolder}, {"Texture", IconImage}, {"Environment", IconGlobe}, {"Material", IconPalette},
      {"Mesh", IconBox},      {"Model", IconPackage}, {"Prefab", IconBlocks},     {"Script", IconScript},
      {"Sound", IconSound},   {"Font", IconFont},     {"Animation", IconPlay},    {"Skin", IconBone},
      {"Scene", IconFilm},
  };
  for (const Entry &entry : Entries) {
    if (entry.type == type) {
      return entry.icon;
    }
  }
  return IconFile;
}

} // namespace sonnet::editor
