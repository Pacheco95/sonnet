#pragma once

#include <sonnet/core/File.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace sonnet::gui::test {

// The test font of the source tree (modules/gui/tests/data, with its attribution), found by
// walking up from `from`, the test binary's directory inside the build tree.
[[nodiscard]] inline std::vector<std::byte> testFontBytes(const std::filesystem::path &from) {
  for (std::filesystem::path base = std::filesystem::absolute(from); !base.empty(); base = base.parent_path()) {
    const std::filesystem::path file = base / "modules/gui/tests/data/TestFont.ttf";
    if (std::filesystem::exists(file)) {
      auto bytes = core::readFile(file);
      REQUIRE(bytes.has_value());
      return std::move(*bytes);
    }
    if (base == base.root_path()) {
      break;
    }
  }
  FAIL("modules/gui/tests/data/TestFont.ttf is not in any parent of the test binary");
  return {};
}

} // namespace sonnet::gui::test
