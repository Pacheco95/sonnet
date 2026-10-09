#include "AssetTestSupport.h"

#include <sonnet/assets/Importers.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Primitives.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

// The macro is stb's name.
// NOLINTNEXTLINE(readability-identifier-naming)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <ktx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <memory>
#include <span>
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

namespace {

// What readKtx2 is asked for: a desktop GPU, which has BC and no ASTC, and a phone, which has
// ASTC and here no BC.
rhi::DeviceInfo deviceWith(bool blockCompression, bool astc) {
  rhi::DeviceInfo info;
  info.blockCompressionSupported = blockCompression;
  info.astcSupported = astc;
  return info;
}

const rhi::DeviceInfo BlockCompression = deviceWith(true, false);
const rhi::DeviceInfo AstcOnly = deviceWith(false, true);

} // namespace

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
  const auto plain = readKtx2(*uncompressed, BlockCompression);
  REQUIRE(plain.has_value());
  REQUIRE(plain->format == rhi::Format::R8G8B8A8Srgb);
  REQUIRE(plain->mipLevels == 4);
  REQUIRE(plain->data == texture->data);

  const auto compressed = cookKtx2(*texture, true);
  REQUIRE(compressed.has_value());
  const auto bc7 = readKtx2(*compressed, BlockCompression);
  REQUIRE(bc7.has_value());
  REQUIRE(bc7->format == rhi::Format::BC7Srgb);
  REQUIRE(bc7->mipLevels == 4);
  REQUIRE(bc7->data.size() == bc7->expectedSize());
  REQUIRE(bc7->level(3).size() == 16); // one 4x4 block for the 1x1 level
  const auto astc = readKtx2(*compressed, AstcOnly);
  REQUIRE(astc.has_value());
  REQUIRE(astc->format == rhi::Format::ASTC4x4Srgb);
  REQUIRE(astc->mipLevels == 4);
  REQUIRE(astc->data.size() == astc->expectedSize());
  const auto fallback = readKtx2(*compressed, rhi::DeviceInfo{});
  REQUIRE(fallback.has_value());
  REQUIRE(fallback->format == rhi::Format::R8G8B8A8Srgb);
  // Grey survives the round trip closely.
  REQUIRE(std::to_integer<int>(fallback->level(0)[0]) >= 195);
  REQUIRE(std::to_integer<int>(fallback->level(0)[0]) <= 205);

  const std::array<std::byte, 16> garbage{};
  REQUIRE(!readKtx2(garbage, BlockCompression).has_value());
}

namespace {

// An image of the source tree's test data (modules/assets/tests/data, with its attribution),
// imported as the database imports a file texture.
renderer::TextureData importTestImage(const char *name, bool srgb) {
  const platform::Platform platform{{.headless = true}};
  const std::filesystem::path file = test::findInSource(platform.basePath(), "modules/assets/tests/data") / name;
  const auto bytes = core::readFile(file);
  REQUIRE(bytes.has_value());
  auto texture = importImage(*bytes, TextureSettings{.srgb = srgb});
  REQUIRE(texture.has_value());
  return std::move(*texture);
}

// PSNR over RGB, in dB, between a texture's first level and a decoded one of the same size.
double psnr(std::span<const std::byte> expected, std::span<const std::byte> actual) {
  REQUIRE(expected.size() == actual.size());
  double squared = 0.0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    if (i % 4 == 3) {
      continue;
    }
    const double difference = std::to_integer<int>(expected[i]) - std::to_integer<int>(actual[i]);
    squared += difference * difference;
    ++count;
  }
  const double mse = squared / static_cast<double>(count);
  return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

// ADR-0018, "CI": the mobile encode of each kind, decoded back on the CPU, since no GPU in CI
// samples ASTC. The images are FlightHelmet's glass-and-plastic base colour and normal map, which
// ADR-0018 measured at 2048x2048 (48.7 dB for the base colour at 6x6, which this test gives too
// at that size), downscaled to 512x512. At 512x512 they measure 45.4 dB and 48.8 dB, and each
// floor is that less 1 dB, so a change to the encoder's settings that costs quality fails here
// while the encoder's x64 and NEON variants may still differ a little.
TEST_CASE("an ASTC cook decodes back above its quality floor", "[assets][texture][ktx][astc]") {
  struct Case {
    const char *file;
    bool srgb;
    rhi::Format format;
    double floor;
  };
  const Case c = GENERATE(Case{"flight_helmet_base_color.png", true, rhi::Format::ASTC6x6Srgb, 44.4},
                          Case{"flight_helmet_normal.png", false, rhi::Format::ASTC4x4Unorm, 47.7});
  const renderer::TextureData source = importTestImage(c.file, c.srgb);
  REQUIRE(source.size == glm::uvec2{512, 512});
  const auto cooked = cookAstcKtx2(source);
  REQUIRE(cooked.has_value());

  // Uploaded as it is where the device has ASTC, and refused where it has only BC.
  const auto loaded = readKtx2(*cooked, AstcOnly);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->format == c.format);
  REQUIRE(loaded->mipLevels == source.mipLevels);
  REQUIRE(loaded->data.size() == loaded->expectedSize());
  REQUIRE(!readKtx2(*cooked, BlockCompression).has_value());

  ktxTexture2 *raw = nullptr;
  REQUIRE(ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t *>(cooked->data()), cooked->size(),
                                       KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw) == KTX_SUCCESS);
  const std::unique_ptr<ktxTexture2, void (*)(ktxTexture2 *)> texture{
      raw, [](ktxTexture2 *t) { ktxTexture_Destroy(reinterpret_cast<ktxTexture *>(t)); }};
  REQUIRE(ktxTexture2_DecodeAstc(texture.get()) == KTX_SUCCESS);
  ktx_size_t offset = 0;
  REQUIRE(ktxTexture_GetImageOffset(reinterpret_cast<ktxTexture *>(texture.get()), 0, 0, 0, &offset) == KTX_SUCCESS);
  const std::span<const std::byte> level0 = source.level(0);
  const std::span<const std::byte> decoded{reinterpret_cast<const std::byte *>(texture->pData) + offset, level0.size()};
  const double measured = psnr(level0, decoded);
  INFO(c.file << ": " << measured << " dB");
  CHECK(measured >= c.floor);
}

namespace {

// A 256x128 equirectangular sky: a blue gradient into a warm horizon, a dark ground, speckled
// clouds and a sun a few thousand times brighter than the sky, which is what a cooked environment
// has to survive.
renderer::TextureData syntheticSky() {
  constexpr glm::uvec2 size{256, 128};
  renderer::TextureData sky{
      .size = size, .format = rhi::Format::R16G16B16A16Sfloat, .mipLevels = 1, .cube = false, .data = {}};
  sky.data.resize(static_cast<std::size_t>(sky.expectedSize()));
  for (std::uint32_t y = 0; y < size.y; ++y) {
    for (std::uint32_t x = 0; x < size.x; ++x) {
      const float latitude = (0.5f - (static_cast<float>(y) + 0.5f) / static_cast<float>(size.y)) * glm::pi<float>();
      const float longitude = (static_cast<float>(x) + 0.5f) / static_cast<float>(size.x) * glm::two_pi<float>();
      const float up = std::sin(latitude);
      glm::vec3 color = up > 0.0f ? glm::mix(glm::vec3{0.9f, 0.7f, 0.5f}, glm::vec3{0.1f, 0.25f, 0.7f}, std::sqrt(up))
                                  : glm::vec3{0.04f, 0.035f, 0.03f} * (1.0f + up);
      const std::uint32_t hash = (x * 73856093u) ^ (y * 19349663u);
      color *= 1.0f + 0.15f * (static_cast<float>(hash >> 8 & 255u) / 255.0f - 0.5f) * (up > 0.0f ? 1.0f : 0.0f);
      const float sunAngle = std::acos(
          std::clamp(std::sin(latitude) * 0.6f + std::cos(latitude) * 0.8f * std::cos(longitude - 2.0f), -1.0f, 1.0f));
      color += glm::vec3{1.0f, 0.9f, 0.7f} * 3000.0f * std::exp(-sunAngle * sunAngle / 0.0008f);
      const std::array<std::uint16_t, 4> texel{glm::packHalf1x16(color.r), glm::packHalf1x16(color.g),
                                               glm::packHalf1x16(color.b), glm::packHalf1x16(1.0f)};
      std::memcpy(sky.data.data() + (std::size_t{y} * size.x + x) * sizeof(texel), texel.data(), sizeof(texel));
    }
  }
  return sky;
}

// PSNR over RGB in dB between two RGBA16F images, after the Reinhard curve x / (1 + x): the error
// that reaches a display, since a plain PSNR of values that reach 3000 only measures the sun.
double hdrPsnr(std::span<const std::byte> expected, std::span<const std::byte> actual) {
  REQUIRE(expected.size() == actual.size());
  double squared = 0.0;
  std::size_t count = 0;
  for (std::size_t texel = 0; texel < expected.size() / 8; ++texel) {
    std::array<std::uint16_t, 4> a{};
    std::array<std::uint16_t, 4> b{};
    std::memcpy(a.data(), expected.data() + texel * 8, 8);
    std::memcpy(b.data(), actual.data() + texel * 8, 8);
    for (int channel = 0; channel < 3; ++channel) {
      const double x = glm::unpackHalf1x16(a[static_cast<std::size_t>(channel)]);
      const double y = glm::unpackHalf1x16(b[static_cast<std::size_t>(channel)]);
      const double d = x / (1.0 + x) - y / (1.0 + y);
      squared += d * d;
      ++count;
    }
  }
  return 10.0 * std::log10(1.0 / std::max(squared / static_cast<double>(count), 1.0e-12));
}

} // namespace

// ADR-0024: an environment is one UASTC HDR 4x4 payload, which readKtx2 turns into BC6H where the
// device has block compression, ASTC HDR where it has that, and RGBA16F where it has neither.
TEST_CASE("an environment cooks to UASTC HDR and transcodes to the format the device samples",
          "[assets][texture][ktx][hdr]") {
  const renderer::TextureData sky = syntheticSky();
  const auto cooked = cookHdrKtx2(sky);
  REQUIRE(cooked.has_value());
  // 8 bits per texel against the 64 of RGBA16F, before zstd.
  REQUIRE(cooked->size() * 6 < sky.data.size());

  rhi::DeviceInfo bc6h;
  bc6h.bc6hSupported = true;
  rhi::DeviceInfo astcHdr;
  astcHdr.astcHdrSupported = true;
  const rhi::DeviceInfo neither;
  struct Case {
    const char *name;
    const rhi::DeviceInfo *device;
    rhi::Format format;
  };
  for (const Case &c :
       {Case{"BC6H", &bc6h, rhi::Format::BC6HUfloat}, Case{"ASTC HDR", &astcHdr, rhi::Format::ASTC4x4Sfloat},
        Case{"RGBA16F", &neither, rhi::Format::R16G16B16A16Sfloat}}) {
    CAPTURE(c.name);
    const auto loaded = readKtx2(*cooked, *c.device);
    const std::string why = loaded ? std::string{} : loaded.error().toString();
    INFO(why);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->format == c.format);
    REQUIRE(loaded->size == sky.size);
    REQUIRE(loaded->mipLevels == 1);
    REQUIRE(loaded->data.size() == loaded->expectedSize());
    if (c.format != rhi::Format::R16G16B16A16Sfloat) {
      REQUIRE(loaded->data.size() == sky.data.size() / 8); // 16 bytes a 4x4 block, 128 bits for 16 texels
    }
  }

  // What the transcode to RGBA16F gives back is the encoder's own error; BC6H adds a fraction of a
  // dB, which the GPU test on the renderer's cubes bounds.
  const auto half = readKtx2(*cooked, neither);
  REQUIRE(half.has_value());
  const double measured = hdrPsnr(sky.data, half->data);
  INFO("UASTC HDR, tone-mapped PSNR " << measured << " dB");
  CHECK(measured >= 50.0); // 53.5 dB measured
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
        params.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;
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
