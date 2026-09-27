#pragma once

#include <sonnet/core/File.h>
#include <sonnet/core/Math.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sonnet::assets::test {

// Encoded PNG and Radiance HDR files, written with stb_image_write (in TextureTests.cpp).
[[nodiscard]] std::vector<std::byte> encodePng(glm::uvec2 size, const std::vector<std::uint8_t> &rgba);
[[nodiscard]] std::vector<std::byte> encodeHdr(glm::uvec2 size, const std::vector<float> &rgb);

// A fresh directory under the temporary directory, emptied first.
[[nodiscard]] inline std::filesystem::path freshDirectory(const std::string &name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

// A path of the source tree, `apps/samples/basic` or `modules/assets/tests/data`, found by walking
// up from `from`, the test binary's directory, which is inside the build tree; empty when no
// ancestor has it.
[[nodiscard]] inline std::filesystem::path findInSource(const std::filesystem::path &from,
                                                        const std::filesystem::path &relative) {
  for (std::filesystem::path base = std::filesystem::absolute(from); !base.empty(); base = base.parent_path()) {
    if (std::filesystem::exists(base / relative)) {
      return base / relative;
    }
    if (base == base.root_path()) {
      break;
    }
  }
  return {};
}

// A 2x2 image: red, green, blue, white.
[[nodiscard]] inline std::vector<std::uint8_t> quadPixels() {
  return {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
}

// A glTF box: 24 vertices with normals and uvs, 36 indices, one material with a base colour
// texture from `imageFile`, under one node named "Crate". Writes the .gltf and its .bin.
void writeBoxGltf(const std::filesystem::path &gltf, const std::string &imageFile);

// A skinned glTF: nodes Rig, Rig/Root, Rig/Root/Tip and Rig/Strip, a quad strip two metres high
// whose bottom follows Root and top follows Tip, one metre up, through a skin of those two
// joints; and the clip "Bend", which turns Tip a quarter about Z over a second (linear), moves
// Root one metre along X at half a second (step) and grows Tip from 1 to 2 (cubic spline, flat
// tangents). Writes the .gltf and its .bin (in GltfTests.cpp).
void writeSkinnedGltf(const std::filesystem::path &gltf);

} // namespace sonnet::assets::test
