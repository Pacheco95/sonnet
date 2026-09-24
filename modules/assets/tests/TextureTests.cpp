#include "AssetTestSupport.h"

#include <sonnet/assets/Importers.h>
#include <sonnet/renderer/Primitives.h>

#include <catch2/catch_test_macros.hpp>

// The macro is stb's name.
// NOLINTNEXTLINE(readability-identifier-naming)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/gtc/packing.hpp>

#include <ktx.h>

#include <chrono>
#include <cstring>
#include <future>
#include <thread>

using namespace sonnet;
using namespace sonnet::assets;

namespace sonnet::assets::test {

namespace {

void append(void *context, void *data, int size) {
  auto *out = static_cast<std::vector<std::byte> *>(context);
  const auto *bytes = static_cast<const std::byte *>(data);
  out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::vector<std::byte> encodePng(glm::uvec2 size, const std::vector<std::uint8_t> &rgba) {
  std::vector<std::byte> out;
  stbi_write_png_to_func(append, &out, static_cast<int>(size.x), static_cast<int>(size.y), 4, rgba.data(),
                         static_cast<int>(size.x * 4));
  return out;
}

std::vector<std::byte> encodeHdr(glm::uvec2 size, const std::vector<float> &rgb) {
  std::vector<std::byte> out;
  stbi_write_hdr_to_func(append, &out, static_cast<int>(size.x), static_cast<int>(size.y), 3, rgb.data());
  return out;
}

void writeBoxGltf(const std::filesystem::path &gltf, const std::string &imageFile) {
  const renderer::MeshData box = renderer::primitives::box();
  std::vector<std::byte> bin;
  const auto put = [&](const void *data, std::size_t bytes) {
    const auto *begin = static_cast<const std::byte *>(data);
    bin.insert(bin.end(), begin, begin + bytes);
  };
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  glm::vec3 minimum{1e9f};
  glm::vec3 maximum{-1e9f};
  for (const renderer::Vertex &vertex : box.vertices) {
    positions.insert(positions.end(), {vertex.position.x, vertex.position.y, vertex.position.z});
    normals.insert(normals.end(), {vertex.normal.x, vertex.normal.y, vertex.normal.z});
    uvs.insert(uvs.end(), {vertex.uv.x, vertex.uv.y});
    minimum = glm::min(minimum, vertex.position);
    maximum = glm::max(maximum, vertex.position);
  }
  const std::size_t positionOffset = bin.size();
  put(positions.data(), positions.size() * sizeof(float));
  const std::size_t normalOffset = bin.size();
  put(normals.data(), normals.size() * sizeof(float));
  const std::size_t uvOffset = bin.size();
  put(uvs.data(), uvs.size() * sizeof(float));
  const std::size_t indexOffset = bin.size();
  put(box.indices.data(), box.indices.size() * sizeof(std::uint32_t));
  const std::string binName = gltf.stem().string() + ".bin";
  REQUIRE(core::writeFile(gltf.parent_path() / binName, bin).has_value());

  using nlohmann::json;
  const auto view = [&](std::size_t offset, std::size_t length) {
    return json{{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
  };
  const auto count = box.vertices.size();
  json document{
      {"asset", {{"version", "2.0"}}},
      {"scene", 0},
      {"scenes", json::array({json{{"nodes", json::array({0})}}})},
      {"nodes", json::array({json{{"name", "Crate"}, {"mesh", 0}, {"translation", json::array({1.0, 2.0, 3.0})}}})},
      {"meshes",
       json::array(
           {json{{"name", "CrateMesh"},
                 {"primitives", json::array({json{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}, {"TEXCOORD_0", 2}}},
                                                  {"indices", 3},
                                                  {"material", 0}}})}}})},
      {"materials",
       json::array({json{{"name", "Wood"},
                         {"pbrMetallicRoughness",
                          {{"baseColorTexture", {{"index", 0}}}, {"metallicFactor", 0.0}, {"roughnessFactor", 0.7}}},
                         {"doubleSided", true}}})},
      {"textures", json::array({json{{"source", 0}, {"sampler", 0}}})},
      {"samplers", json::array({json{{"wrapS", 33071}, {"wrapT", 33071}}})},
      {"images", json::array({json{{"uri", imageFile}}})},
      {"buffers", json::array({json{{"uri", binName}, {"byteLength", bin.size()}}})},
      {"bufferViews",
       json::array({view(positionOffset, positions.size() * sizeof(float)),
                    view(normalOffset, normals.size() * sizeof(float)), view(uvOffset, uvs.size() * sizeof(float)),
                    view(indexOffset, box.indices.size() * sizeof(std::uint32_t))})},
      {"accessors",
       json::array(
           {json{{"bufferView", 0},
                 {"componentType", 5126},
                 {"count", count},
                 {"type", "VEC3"},
                 {"min", json::array({minimum.x, minimum.y, minimum.z})},
                 {"max", json::array({maximum.x, maximum.y, maximum.z})}},
            json{{"bufferView", 1}, {"componentType", 5126}, {"count", count}, {"type", "VEC3"}},
            json{{"bufferView", 2}, {"componentType", 5126}, {"count", count}, {"type", "VEC2"}},
            json{{"bufferView", 3}, {"componentType", 5125}, {"count", box.indices.size()}, {"type", "SCALAR"}}})},
  };
  REQUIRE(core::writeFile(gltf, document.dump(2)).has_value());
}

} // namespace sonnet::assets::test

TEST_CASE("a PNG imports to RGBA8 with a mip chain in the requested colour space", "[assets][texture]") {
  const std::vector<std::byte> png = test::encodePng({2, 2}, test::quadPixels());
  const auto srgb = importImage(png, TextureSettings{});
  REQUIRE(srgb.has_value());
  REQUIRE(srgb->size == glm::uvec2{2, 2});
  REQUIRE(srgb->format == rhi::Format::R8G8B8A8Srgb);
  REQUIRE(srgb->mipLevels == 2);
  REQUIRE(std::to_integer<int>(srgb->level(0)[0]) == 255);
  REQUIRE(std::to_integer<int>(srgb->level(0)[5]) == 255); // green of the second texel

  const auto linear = importImage(png, TextureSettings{.srgb = false, .mipmaps = false});
  REQUIRE(linear.has_value());
  REQUIRE(linear->format == rhi::Format::R8G8B8A8Unorm);
  REQUIRE(linear->mipLevels == 1);

  const std::array<std::byte, 8> garbage{};
  REQUIRE(!importImage(garbage, TextureSettings{}).has_value());
}

TEST_CASE("an HDR file imports to RGBA16F", "[assets][texture]") {
  const std::vector<float> rgb{0.5f, 2.0f, 8.0f, 0.5f, 2.0f, 8.0f};
  const std::vector<std::byte> hdr = test::encodeHdr({2, 1}, rgb);
  const auto imported = importHdr(hdr);
  REQUIRE(imported.has_value());
  REQUIRE(imported->format == rhi::Format::R16G16B16A16Sfloat);
  REQUIRE(imported->size == glm::uvec2{2, 1});
  std::uint16_t half = 0;
  std::memcpy(&half, imported->data.data() + 2, sizeof(half)); // the green channel of the first texel
  REQUIRE(glm::unpackHalf1x16(half) > 1.9f);
  REQUIRE(glm::unpackHalf1x16(half) < 2.1f);
}

TEST_CASE("a texture cooks into KTX2 and reads back compressed or not", "[assets][texture][ktx]") {
  auto texture =
      importImage(test::encodePng({8, 8}, std::vector<std::uint8_t>(std::size_t{8} * 8 * 4, 200)), TextureSettings{});
  REQUIRE(texture.has_value());
  REQUIRE(texture->mipLevels == 4);

  const auto uncompressed = cookKtx2(*texture, false);
  REQUIRE(uncompressed.has_value());
  const auto plain = readKtx2(*uncompressed, true);
  REQUIRE(plain.has_value());
  REQUIRE(plain->format == rhi::Format::R8G8B8A8Srgb);
  REQUIRE(plain->mipLevels == 4);
  REQUIRE(plain->data == texture->data);

  const auto compressed = cookKtx2(*texture, true);
  REQUIRE(compressed.has_value());
  const auto bc7 = readKtx2(*compressed, true);
  REQUIRE(bc7.has_value());
  REQUIRE(bc7->format == rhi::Format::BC7Srgb);
  REQUIRE(bc7->mipLevels == 4);
  REQUIRE(bc7->data.size() == bc7->expectedSize());
  REQUIRE(bc7->level(3).size() == 16); // one 4x4 block for the 1x1 level
  const auto fallback = readKtx2(*compressed, false);
  REQUIRE(fallback.has_value());
  REQUIRE(fallback->format == rhi::Format::R8G8B8A8Srgb);
  // Grey survives the round trip closely.
  REQUIRE(std::to_integer<int>(fallback->level(0)[0]) >= 195);
  REQUIRE(std::to_integer<int>(fallback->level(0)[0]) <= 205);

  const std::array<std::byte, 16> garbage{};
  REQUIRE(!readKtx2(garbage, true).has_value());
}

// Issue #24. basisu's job pool, which libktx builds and tears down around every compression, set
// its kill flag without its mutex: a worker that had just found the flag false and was about to
// block missed the destructor's notify_all, and the destructor's join() waited for it for ever.
// With two threads, the count cookKtx2 passes on a two-core machine, each of five runs of this loop
// hung within 5000 compressions until ports/ktx/0009 locked the mutex around the flag. The race is between basisu's own
// threads, so there is nothing to order by hand; the loop runs it until it would have lost.
TEST_CASE("compressing KTX2 textures back to back never hangs basisu's job pool", "[assets][texture][ktx]") {
  constexpr int compressions = 20000;
  constexpr ktx_uint32_t vkFormatR8G8B8A8Unorm = 37;
  std::promise<ktx_error_code_e> result;
  std::future<ktx_error_code_e> finished = result.get_future();
  // A thread of its own, which owns the promise, so a hang fails this case by its deadline rather
  // than the whole binary by ctest's timeout, and the thread it leaves behind touches nothing here.
  std::thread{[result = std::move(result)]() mutable {
    const std::vector<ktx_uint8_t> pixels(std::size_t{4} * 4 * 4, 90);
    for (int i = 0; i < compressions; ++i) {
      ktxTextureCreateInfo info{};
      info.vkFormat = vkFormatR8G8B8A8Unorm;
      info.baseWidth = 4;
      info.baseHeight = 4;
      info.baseDepth = 1;
      info.numDimensions = 2;
      info.numLevels = 1;
      info.numLayers = 1;
      info.numFaces = 1;
      ktxTexture2 *texture = nullptr;
      ktx_error_code_e code = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
      // The library's ktxTexture() macro is a C-style cast to the base struct.
      auto *base = reinterpret_cast<ktxTexture *>(texture);
      if (code == KTX_SUCCESS) {
        code = ktxTexture_SetImageFromMemory(base, 0, 0, 0, pixels.data(), pixels.size());
      }
      if (code == KTX_SUCCESS) {
        ktxBasisParams params{};
        params.structSize = sizeof(params);
        params.uastc = KTX_TRUE;
        params.threadCount = 2;
        params.uastcFlags = KTX_PACK_UASTC_LEVEL_FASTER;
        code = ktxTexture2_CompressBasisEx(texture, &params);
      }
      if (texture != nullptr) {
        ktxTexture_Destroy(base);
      }
      if (code != KTX_SUCCESS) {
        result.set_value(code);
        return;
      }
    }
    result.set_value(KTX_SUCCESS);
  }}.detach();
  REQUIRE(finished.wait_for(std::chrono::seconds{60}) == std::future_status::ready);
  REQUIRE(finished.get() == KTX_SUCCESS);
}
