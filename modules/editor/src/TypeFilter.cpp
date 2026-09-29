#include <sonnet/editor/TypeFilter.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace sonnet::editor {

namespace {

constexpr std::array<assets::AssetType, 9> AssetTypes{
    assets::AssetType::Texture, assets::AssetType::Mesh,        assets::AssetType::Material,
    assets::AssetType::Model,   assets::AssetType::Environment, assets::AssetType::Script,
    assets::AssetType::Sound,   assets::AssetType::Skin,        assets::AssetType::Animation,
};

constexpr std::array<const char *, 9> AssetTypeNames{
    "Textures", "Meshes", "Materials", "Models", "Environments", "Scripts", "Sounds", "Skins", "Animations",
};

bool containsIgnoringCase(std::string_view text, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  return std::ranges::search(text, needle, [&](char a, char b) {
           return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
         }).begin() != text.end();
}

} // namespace

void TypeFilter::set(std::size_t index, bool value) noexcept {
  if (index >= MaxEntries) {
    return;
  }
  const std::uint32_t bit = 1U << index;
  m_bits = value ? (m_bits | bit) : (m_bits & ~bit);
}

std::string TypeFilter::summary(std::span<const char *const> names) const {
  std::size_t count = 0;
  std::size_t last = 0;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (checked(i)) {
      ++count;
      last = i;
    }
  }
  if (count == 0) {
    return "All";
  }
  return count == 1 ? std::string{names[last]} : std::format("{} types", count);
}

bool typeFilterCombo(const char *id, float width, std::span<const char *const> names, TypeFilter &filter) {
  bool changed = false;
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, filter.summary(names).c_str())) {
    if (ImGui::Selectable("Clear", false, ImGuiSelectableFlags_DontClosePopups)) {
      filter.clear();
      changed = true;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
      bool value = filter.checked(i);
      if (ImGui::Checkbox(names[i], &value)) {
        filter.set(i, value);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

std::span<const assets::AssetType> filterableAssetTypes() noexcept {
  return AssetTypes;
}

std::span<const char *const> filterableAssetTypeNames() noexcept {
  return AssetTypeNames;
}

bool assetPassesFilter(const assets::AssetInfo &info, std::string_view text, const TypeFilter &types) {
  if (!containsIgnoringCase(info.name, text)) {
    return false;
  }
  const auto found = std::ranges::find(AssetTypes, info.type);
  return found != AssetTypes.end() && types.matches(static_cast<std::size_t>(found - AssetTypes.begin()));
}

} // namespace sonnet::editor
