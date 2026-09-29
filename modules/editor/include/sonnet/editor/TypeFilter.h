#pragma once

#include <sonnet/assets/AssetDatabase.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sonnet::editor {

// A set of checked entries out of a fixed list of names, for the type filters of the asset browser
// and the hierarchy. Nothing checked means "All": matches() then accepts every index.
class TypeFilter {
public:
  static constexpr std::size_t MaxEntries = 32;

  [[nodiscard]] bool empty() const noexcept {
    return m_bits == 0;
  }
  [[nodiscard]] bool checked(std::size_t index) const noexcept {
    return index < MaxEntries && (m_bits >> index & 1U) != 0;
  }
  void set(std::size_t index, bool value) noexcept;
  void clear() noexcept {
    m_bits = 0;
  }

  // An entry passes when it is checked, or when nothing is.
  [[nodiscard]] bool matches(std::size_t index) const noexcept {
    return empty() || checked(index);
  }

  // "All", the entry's name when one is checked, "N types" otherwise.
  [[nodiscard]] std::string summary(std::span<const char *const> names) const;

private:
  std::uint32_t m_bits = 0;
};

// A combo with a checkbox per name and a "Clear" entry; the popup stays open while boxes are
// ticked. Returns true when the set changed.
bool typeFilterCombo(const char *id, float width, std::span<const char *const> names, TypeFilter &filter);

// The asset types the browser offers, in the order of its combo; a TypeFilter over them is indexed
// by position in this list.
[[nodiscard]] std::span<const assets::AssetType> filterableAssetTypes() noexcept;
[[nodiscard]] std::span<const char *const> filterableAssetTypeNames() noexcept;

// Whether the asset browser lists an asset: its name contains `text` (ignoring case) AND its type
// is checked in `types`.
[[nodiscard]] bool assetPassesFilter(const assets::AssetInfo &info, std::string_view text, const TypeFilter &types);

} // namespace sonnet::editor
