#pragma once

#include <sonnet/core/Error.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sonnet::rhi {

// Separate depth and colour shaders must agree bit for bit on rasterised positions. Slang's
// direct SPIR-V backend does not emit Invariant for `precise`, so establish that contract at
// module creation, including editor hot reload. Both standalone and block-member builtins occur
// in SPIR-V. No arithmetic or descriptor layout is changed.
inline void makePositionInvariant(std::vector<std::uint32_t> &words) {
  constexpr std::uint32_t opDecorate = 71, opMemberDecorate = 72;
  constexpr std::uint32_t builtIn = 11, position = 0, invariant = 18;
  if (words.size() < 5 || words[0] != 0x07230203u) {
    throw core::Exception{"invalid SPIR-V header", core::ErrorCategory::Shader};
  }
  std::vector<std::vector<std::uint32_t>> required;
  std::vector<std::vector<std::uint32_t>> existing;
  std::size_t insertion = words.size();
  for (std::size_t offset = 5; offset < words.size();) {
    const std::uint32_t count = words[offset] >> 16;
    const std::uint32_t op = words[offset] & 0xFFFFu;
    if (count == 0 || count > words.size() - offset) {
      throw core::Exception{"malformed SPIR-V instruction", core::ErrorCategory::Shader};
    }
    // Annotations precede types, constants and globals in SPIR-V's logical layout.
    if (insertion == words.size() && op >= 19 && op <= 39) {
      insertion = offset;
    }
    if (op == opDecorate && count == 4 && words[offset + 2] == builtIn && words[offset + 3] == position) {
      required.push_back({(3u << 16) | opDecorate, words[offset + 1], invariant});
    } else if (op == opMemberDecorate && count == 5 && words[offset + 3] == builtIn && words[offset + 4] == position) {
      required.push_back({(4u << 16) | opMemberDecorate, words[offset + 1], words[offset + 2], invariant});
    } else if ((op == opDecorate && count == 3 && words[offset + 2] == invariant) ||
               (op == opMemberDecorate && count == 4 && words[offset + 3] == invariant)) {
      existing.emplace_back(words.data() + offset, words.data() + offset + count);
    }
    offset += count;
  }
  std::vector<std::uint32_t> additions;
  for (const auto &decoration : required) {
    if (std::ranges::find(existing, decoration) == existing.end()) {
      additions.insert(additions.end(), decoration.begin(), decoration.end());
      existing.push_back(decoration);
    }
  }
  words.insert(words.begin() + static_cast<std::ptrdiff_t>(insertion), additions.begin(), additions.end());
}

} // namespace sonnet::rhi
