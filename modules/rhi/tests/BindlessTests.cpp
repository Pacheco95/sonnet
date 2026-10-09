#include "TestDevice.h"

#include <sonnet/core/File.h>
#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sonnet::rhi;

namespace {

// Mirror of texture.slang.
struct TexturePush {
  std::uint32_t texture;
  std::uint32_t sampler;
  float lod;
  float padding{0.0f};
  glm::vec4 direction{0.0f};
};
static_assert(sizeof(TexturePush) == 32);

// Mirror of fill.slang.
struct FillPush {
  std::uint32_t image;
  std::uint32_t width;
  std::uint32_t height;
  std::uint32_t padding{0};
  glm::vec4 color;
};
static_assert(sizeof(FillPush) == 32);

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

ShaderHandle loadShader(test::TestDevice &device, const char *name) {
  const auto spirv = sonnet::core::readFile(device.platform.basePath() / "shaders" / (std::string{name} + ".spv"));
  REQUIRE(spirv.has_value());
  return device->createShader({.spirv = *spirv, .debugName = name});
}

std::vector<std::byte> rgba(std::initializer_list<std::array<std::uint8_t, 4>> pixels) {
  std::vector<std::byte> bytes;
  for (const auto &pixel : pixels) {
    for (const std::uint8_t channel : pixel) {
      bytes.push_back(std::byte{channel});
    }
  }
  return bytes;
}

// A target drawn by a full-screen pass and read back.
struct Target {
  test::TestDevice &device;
  glm::uvec2 size;
  ImageHandle color;
  BufferHandle readback;

  Target(test::TestDevice &testDevice, glm::uvec2 targetSize) : device(testDevice), size(targetSize) {
    color = device->createImage({.size = size,
                                 .format = Format::R8G8B8A8Unorm,
                                 .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSrc,
                                 .debugName = "target"});
    readback = device->createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                     .usage = BufferUsage::TransferDst,
                                     .memory = MemoryUsage::GpuToCpu,
                                     .debugName = "readback"});
  }
  ~Target() {
    device->waitIdle();
    device->destroyBuffer(readback);
    device->destroyImage(color);
  }
  Target(const Target &) = delete;
  Target &operator=(const Target &) = delete;

  void draw(ICommandList &commands, PipelineHandle pipeline, const TexturePush &push) {
    test::transition(commands, test::toColorAttachment(color));
    const ColorAttachment attachment{.image = color, .clearColor = {0.0f, 0.0f, 0.0f, 1.0f}};
    commands.beginRendering({.colors = {&attachment, 1}});
    commands.bindPipeline(pipeline);
    commands.pushConstants(std::as_bytes(std::span{&push, 1}));
    commands.draw(3);
    commands.endRendering();
    test::transition(commands, test::colorToTransferSrc(color));
    commands.copyImageToBuffer(color, readback);
  }

  Pixel pixel(unsigned x, unsigned y) {
    device->waitIdle();
    return pixelAt(device->mappedRange(readback), size, x, y);
  }
};

} // namespace

TEST_CASE("samplers and sampled images take slots in the bindless arrays", "[rhi][bindless]") {
  test::TestDevice device;
  const SamplerHandle linear = device->createSampler({.debugName = "linear"});
  const SamplerHandle nearest = device->createSampler({.filter = Filter::Nearest, .debugName = "nearest"});
  const SamplerHandle shadow = device->createSampler({.compare = true, .debugName = "shadow"});
  REQUIRE(device->isValid(linear));
  REQUIRE(device->samplerIndex(linear) != device->samplerIndex(nearest));
  // Comparison samplers have their own array, so the first one takes index 0 there too.
  REQUIRE(device->samplerIndex(shadow) == 0);

  const ImageHandle sampled = device->createImage(
      {.size = {4, 4}, .format = Format::R8G8B8A8Unorm, .usage = ImageUsage::Sampled, .debugName = "sampled"});
  const ImageHandle attachment = device->createImage({.size = {4, 4},
                                                      .format = Format::R8G8B8A8Unorm,
                                                      .usage = ImageUsage::ColorAttachment,
                                                      .debugName = "attachment only"});
  const ImageHandle cube = device->createImage({.size = {4, 4},
                                                .format = Format::R8G8B8A8Unorm,
                                                .usage = ImageUsage::Sampled,
                                                .cube = true,
                                                .debugName = "cube"});
  REQUIRE(device->sampledImageIndex(sampled) != InvalidBindlessIndex);
  REQUIRE(device->sampledImageIndex(attachment) == InvalidBindlessIndex);
  REQUIRE(device->sampledImageIndex(cube) != InvalidBindlessIndex);
  REQUIRE(device->storageImageIndex(sampled, 0) == InvalidBindlessIndex); // no Storage usage

  device->destroySampler(shadow);
  device->destroySampler(nearest);
  device->destroySampler(linear);
  REQUIRE(!device->isValid(linear));
  REQUIRE(device->samplerIndex(linear) == InvalidBindlessIndex);
  device->destroyImage(cube);
  device->destroyImage(attachment);
  device->destroyImage(sampled);
}

TEST_CASE("storage buffers take bindless slots, reuse freed ones and get none from a full array",
          "[rhi][bindless][gpu]") {
  test::TestDevice device;
  const auto storage = [&](std::uint32_t i) {
    return device->createBuffer(
        {.size = 16, .usage = BufferUsage::Storage, .debugName = "storage " + std::to_string(i)});
  };
  std::vector<BufferHandle> buffers;
  for (std::uint32_t i = 0; i < MaxBindlessStorageBuffers; ++i) {
    buffers.push_back(storage(i));
    REQUIRE(device->storageBufferIndex(buffers.back()) == i);
  }
  REQUIRE(device->storageBufferIndex(buffers.front()) == 0); // asked again, the same slot

  // The array is full: no slot, and no descriptor written past its end, which validation would
  // report. Asking again does not retry.
  const BufferHandle overflow = storage(MaxBindlessStorageBuffers);
  REQUIRE(device->storageBufferIndex(overflow) == InvalidBindlessIndex);
  REQUIRE(device->storageBufferIndex(overflow) == InvalidBindlessIndex);

  const BufferHandle index = device->createBuffer({.size = 16, .usage = BufferUsage::Index, .debugName = "index only"});
  REQUIRE(device->storageBufferIndex(index) == InvalidBindlessIndex);

  // A destroyed buffer's slot returns once the frames that could still read it have finished.
  const std::uint32_t freed = device->storageBufferIndex(buffers[7]);
  device->destroyBuffer(buffers[7]);
  for (std::uint32_t frame = 0; frame <= FramesInFlight; ++frame) {
    static_cast<void>(device->beginFrame());
    device->endFrame();
  }
  const BufferHandle reused = storage(MaxBindlessStorageBuffers + 1);
  REQUIRE(device->storageBufferIndex(reused) == freed);

  device->destroyBuffer(reused);
  device->destroyBuffer(index);
  device->destroyBuffer(overflow);
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    if (i != 7) {
      device->destroyBuffer(buffers[i]);
    }
  }
}

TEST_CASE("a texture uploaded with its mip chain is sampled through the bindless set", "[rhi][bindless][gpu]") {
  test::TestDevice device;
  const ShaderHandle shader = loadShader(device, "texture");
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .cullMode = CullMode::None, .debugName = "texture"});
  device->destroyShader(shader);
  const SamplerHandle nearest = device->createSampler({.filter = Filter::Nearest,
                                                       .mipFilter = Filter::Nearest,
                                                       .addressMode = AddressMode::ClampToEdge,
                                                       .debugName = "nearest"});

  // Level 0: red, green / blue, white. Level 1: one grey texel.
  const ImageHandle texture = device->createImage({.size = {2, 2},
                                                   .format = Format::R8G8B8A8Unorm,
                                                   .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                   .mipLevels = 2,
                                                   .debugName = "texture"});
  const std::vector<std::byte> level0 =
      rgba({{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}});
  const std::vector<std::byte> level1 = rgba({{128, 128, 128, 255}});
  const std::array uploads{ImageUpload{.mipLevel = 0, .data = level0}, ImageUpload{.mipLevel = 1, .data = level1}};
  device->uploadImage(texture, uploads);

  Target target{device, {4, 4}};
  const TexturePush push{
      .texture = device->sampledImageIndex(texture), .sampler = device->samplerIndex(nearest), .lod = 0.0f};
  target.draw(device->beginFrame(), pipeline, push);
  device->endFrame();
  // The 2x2 texture covers the 4x4 target two pixels per texel.
  REQUIRE(target.pixel(0, 0).r == 255);
  REQUIRE(target.pixel(0, 0).g == 0);
  REQUIRE(target.pixel(3, 0).g == 255);
  REQUIRE(target.pixel(0, 3).b == 255);
  REQUIRE(target.pixel(3, 3).r == 255);
  REQUIRE(target.pixel(3, 3).g == 255);

  const TexturePush pushLevel1{.texture = push.texture, .sampler = push.sampler, .lod = 1.0f};
  target.draw(device->beginFrame(), pipeline, pushLevel1);
  device->endFrame();
  REQUIRE(target.pixel(1, 1).r == 128);
  REQUIRE(target.pixel(2, 2).g == 128);

  device->destroyImage(texture);
  device->destroySampler(nearest);
  device->destroyPipeline(pipeline);
}

TEST_CASE("a cube map is sampled by direction", "[rhi][bindless][gpu]") {
  test::TestDevice device;
  const ShaderHandle shader = loadShader(device, "texture");
  const PipelineHandle pipeline = device->createGraphicsPipeline({.shader = shader,
                                                                  .fragmentEntry = "cubeFragmentMain",
                                                                  .colorFormats = {Format::R8G8B8A8Unorm},
                                                                  .cullMode = CullMode::None,
                                                                  .debugName = "cube"});
  device->destroyShader(shader);
  const SamplerHandle nearest =
      device->createSampler({.filter = Filter::Nearest, .mipFilter = Filter::Nearest, .debugName = "nearest"});
  const ImageHandle cube = device->createImage({.size = {1, 1},
                                                .format = Format::R8G8B8A8Unorm,
                                                .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                .cube = true,
                                                .debugName = "cube"});
  // Faces in Vulkan order: +X, -X, +Y, -Y, +Z, -Z, each one texel with its own value.
  std::array<std::vector<std::byte>, 6> faces;
  std::array<ImageUpload, 6> uploads;
  for (std::uint32_t face = 0; face < 6; ++face) {
    const auto value = static_cast<std::uint8_t>(40 * (face + 1));
    faces[face] = rgba({{value, 0, 0, 255}});
    uploads[face] = ImageUpload{.mipLevel = 0, .layer = face, .data = faces[face]};
  }
  device->uploadImage(cube, uploads);

  Target target{device, {2, 2}};
  const std::array<std::pair<glm::vec3, int>, 3> cases{{
      {{1.0f, 0.0f, 0.0f}, 40},
      {{0.0f, -1.0f, 0.0f}, 160},
      {{0.0f, 0.0f, -1.0f}, 240},
  }};
  for (const auto &[direction, expected] : cases) {
    const TexturePush push{.texture = device->sampledImageIndex(cube),
                           .sampler = device->samplerIndex(nearest),
                           .lod = 0.0f,
                           .direction = glm::vec4{direction, 0.0f}};
    target.draw(device->beginFrame(), pipeline, push);
    device->endFrame();
    REQUIRE(target.pixel(0, 0).r == expected);
  }

  device->destroyImage(cube);
  device->destroySampler(nearest);
  device->destroyPipeline(pipeline);
}

TEST_CASE("a compute pass fills a storage image that a later pass samples", "[rhi][bindless][compute][gpu]") {
  test::TestDevice device;
  const ShaderHandle fillShader = loadShader(device, "fill");
  const PipelineHandle fill = device->createComputePipeline({.shader = fillShader, .debugName = "fill"});
  device->destroyShader(fillShader);
  const ShaderHandle textureShader = loadShader(device, "texture");
  const PipelineHandle sample = device->createGraphicsPipeline({.shader = textureShader,
                                                                .colorFormats = {Format::R8G8B8A8Unorm},
                                                                .cullMode = CullMode::None,
                                                                .debugName = "texture"});
  device->destroyShader(textureShader);
  const SamplerHandle linear = device->createSampler({.debugName = "linear"});

  // Two mip levels so the second storage view is exercised too.
  const ImageHandle image = device->createImage({.size = {16, 16},
                                                 .format = Format::R16G16B16A16Sfloat,
                                                 .usage = ImageUsage::Sampled | ImageUsage::Storage,
                                                 .mipLevels = 2,
                                                 .debugName = "storage"});
  const std::uint32_t level0 = device->storageImageIndex(image, 0);
  const std::uint32_t level1 = device->storageImageIndex(image, 1);
  REQUIRE(level0 != InvalidBindlessIndex);
  REQUIRE(level1 != InvalidBindlessIndex);
  REQUIRE(level0 != level1);
  REQUIRE(device->storageImageIndex(image, 0) == level0); // created once

  Target target{device, {4, 4}};
  ICommandList &commands = device->beginFrame();
  const ImageBarrier toGeneral{.image = image,
                               .srcStage = PipelineStage::AllCommands,
                               .srcAccess = Access::None,
                               .oldLayout = ImageLayout::Undefined,
                               .dstStage = PipelineStage::ComputeShader,
                               .dstAccess = Access::ShaderWrite,
                               .newLayout = ImageLayout::General};
  test::transition(commands, toGeneral);
  commands.bindPipeline(fill);
  const FillPush push0{.image = level0, .width = 16, .height = 16, .color = {0.0f, 0.5f, 1.0f, 1.0f}};
  commands.pushConstants(std::as_bytes(std::span{&push0, 1}));
  commands.dispatch(2, 2, 1);
  const FillPush push1{.image = level1, .width = 8, .height = 8, .color = {1.0f, 0.0f, 0.0f, 1.0f}};
  commands.pushConstants(std::as_bytes(std::span{&push1, 1}));
  commands.dispatch(1, 1, 1);
  const ImageBarrier toSampled{.image = image,
                               .srcStage = PipelineStage::ComputeShader,
                               .srcAccess = Access::ShaderWrite,
                               .oldLayout = ImageLayout::General,
                               .dstStage = PipelineStage::FragmentShader,
                               .dstAccess = Access::ShaderRead,
                               .newLayout = ImageLayout::ShaderReadOnly};
  test::transition(commands, toSampled);
  const TexturePush samplePush{
      .texture = device->sampledImageIndex(image), .sampler = device->samplerIndex(linear), .lod = 0.0f};
  target.draw(commands, sample, samplePush);
  device->endFrame();
  const Pixel centre = target.pixel(2, 2);
  REQUIRE(centre.r == 0);
  // Half a unit rounds either way between drivers.
  REQUIRE(centre.g >= 127);
  REQUIRE(centre.g <= 128);
  REQUIRE(centre.b == 255);

  const TexturePush level1Push{.texture = samplePush.texture, .sampler = samplePush.sampler, .lod = 1.0f};
  ICommandList &second = device->beginFrame();
  const ImageBarrier keep{.image = image,
                          .srcStage = PipelineStage::FragmentShader,
                          .srcAccess = Access::ShaderRead,
                          .oldLayout = ImageLayout::ShaderReadOnly,
                          .dstStage = PipelineStage::FragmentShader,
                          .dstAccess = Access::ShaderRead,
                          .newLayout = ImageLayout::ShaderReadOnly};
  test::transition(second, keep);
  target.draw(second, sample, level1Push);
  device->endFrame();
  REQUIRE(target.pixel(1, 1).r == 255);
  REQUIRE(target.pixel(1, 1).b == 0);

  device->destroyImage(image);
  device->destroySampler(linear);
  device->destroyPipeline(sample);
  device->destroyPipeline(fill);
}

TEST_CASE("an upload larger than the staging ring reaches the image", "[rhi][bindless][upload][gpu]") {
  test::TestDevice device;
  const ShaderHandle shader = loadShader(device, "texture");
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .cullMode = CullMode::None, .debugName = "texture"});
  device->destroyShader(shader);
  const SamplerHandle nearest =
      device->createSampler({.filter = Filter::Nearest, .mipFilter = Filter::Nearest, .debugName = "nearest"});
  // 40 MB of RGBA8 in one level, more than the 32 MB ring, plus a second level that goes through
  // the ring after it.
  constexpr glm::uvec2 size{4096, 2560};
  const ImageHandle texture = device->createImage({.size = size,
                                                   .format = Format::R8G8B8A8Unorm,
                                                   .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                   .mipLevels = 2,
                                                   .debugName = "large"});
  std::vector<std::byte> level0(levelByteSize(Format::R8G8B8A8Unorm, size));
  for (std::size_t i = 0; i < level0.size(); i += 4) {
    level0[i] = std::byte{200};
    level0[i + 1] = std::byte{100};
    level0[i + 2] = std::byte{50};
    level0[i + 3] = std::byte{255};
  }
  std::vector<std::byte> level1(levelByteSize(Format::R8G8B8A8Unorm, mipSize(size, 1)), std::byte{255});
  const std::array uploads{ImageUpload{.mipLevel = 0, .data = level0}, ImageUpload{.mipLevel = 1, .data = level1}};
  device->uploadImage(texture, uploads);

  Target target{device, {2, 2}};
  const TexturePush push{
      .texture = device->sampledImageIndex(texture), .sampler = device->samplerIndex(nearest), .lod = 0.0f};
  target.draw(device->beginFrame(), pipeline, push);
  device->endFrame();
  REQUIRE(target.pixel(1, 1).r == 200);
  REQUIRE(target.pixel(1, 1).g == 100);
  REQUIRE(target.pixel(1, 1).b == 50);
  const TexturePush pushLevel1{.texture = push.texture, .sampler = push.sampler, .lod = 1.0f};
  target.draw(device->beginFrame(), pipeline, pushLevel1);
  device->endFrame();
  REQUIRE(target.pixel(0, 0).r == 255);
  REQUIRE(target.pixel(0, 0).b == 255);

  device->destroyImage(texture);
  device->destroySampler(nearest);
  device->destroyPipeline(pipeline);
}

namespace {

// One 128-bit block of one colour, so a compressed image of any size can be filled without an
// encoder. BC7 mode 6 with both endpoints equal and every index 0 decodes to the endpoint, whose
// channels are seven bits and a shared p-bit, hence the odd values below. An ASTC void-extent
// block carries its colour as four 16-bit UNORM channels.
constexpr std::array<std::uint8_t, 4> SolidColor{201, 101, 51, 255};

std::array<std::byte, 16> solidBlock(Format format) {
  std::array<std::byte, 16> block{};
  if (isAstcFormat(format)) {
    const std::array<std::uint8_t, 8> header{0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (std::size_t i = 0; i < header.size(); ++i) {
      block[i] = std::byte{header[i]};
    }
    for (std::size_t c = 0; c < 4; ++c) {
      block[8 + c * 2] = std::byte{SolidColor[c]};
      block[8 + c * 2 + 1] = std::byte{SolidColor[c]};
    }
    return block;
  }
  std::size_t bit = 0;
  const auto put = [&](std::uint32_t value, std::size_t bits) {
    for (std::size_t i = 0; i < bits; ++i, ++bit) {
      if (((value >> i) & 1u) != 0) {
        block[bit / 8] |= std::byte{static_cast<std::uint8_t>(1u << (bit % 8))};
      }
    }
  };
  put(1u << 6, 7); // mode 6
  for (std::size_t c = 0; c < 4; ++c) {
    put(SolidColor[c] >> 1, 7);
    put(SolidColor[c] >> 1, 7);
  }
  put(1, 1); // the p-bits, which make every channel odd
  put(1, 1);
  return block; // the indices stay 0
}

} // namespace

// Uploads a solid colour into a compressed image whose size is no multiple of the block, with
// every mip level, and samples its first and last levels. BC7 runs wherever there is block
// compression; the ASTC formats need astcSupported, which no desktop GPU and no Lavapipe reports,
// so they run on Apple silicon and phones (ADR-0018, "CI").
TEST_CASE("a compressed image of any size uploads every level and samples its colour",
          "[rhi][bindless][upload][compressed][gpu]") {
  test::TestDevice device;
  const auto format = GENERATE(Format::BC7Unorm, Format::ASTC4x4Unorm, Format::ASTC6x6Unorm);
  if (!formatSupported(device->info(), format)) {
    SKIP("the device cannot sample " << toString(format));
  }
  const ShaderHandle shader = loadShader(device, "texture");
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .cullMode = CullMode::None, .debugName = "texture"});
  device->destroyShader(shader);
  const SamplerHandle nearest =
      device->createSampler({.filter = Filter::Nearest, .mipFilter = Filter::Nearest, .debugName = "nearest"});
  constexpr glm::uvec2 size{13, 7};
  const std::uint32_t levels = fullMipCount(size);
  const ImageHandle texture = device->createImage({.size = size,
                                                   .format = format,
                                                   .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                   .mipLevels = levels,
                                                   .debugName = std::string{toString(format)}});
  const std::array<std::byte, 16> block = solidBlock(format);
  std::vector<std::vector<std::byte>> data;
  std::vector<ImageUpload> uploads;
  for (std::uint32_t level = 0; level < levels; ++level) {
    auto &bytes = data.emplace_back();
    const std::uint64_t byteSize = levelByteSize(format, mipSize(size, level));
    for (std::uint64_t offset = 0; offset < byteSize; offset += block.size()) {
      bytes.insert(bytes.end(), block.begin(), block.end());
    }
  }
  uploads.reserve(levels);
  for (std::uint32_t level = 0; level < levels; ++level) {
    uploads.push_back({.mipLevel = level, .data = data[level]});
  }
  device->uploadImage(texture, uploads);

  Target target{device, {2, 2}};
  for (const float lod : {0.0f, static_cast<float>(levels - 1)}) {
    const TexturePush push{
        .texture = device->sampledImageIndex(texture), .sampler = device->samplerIndex(nearest), .lod = lod};
    target.draw(device->beginFrame(), pipeline, push);
    device->endFrame();
    const Pixel pixel = target.pixel(1, 1);
    CHECK(std::abs(pixel.r - SolidColor[0]) <= 1);
    CHECK(std::abs(pixel.g - SolidColor[1]) <= 1);
    CHECK(std::abs(pixel.b - SolidColor[2]) <= 1);
    CHECK(pixel.a == SolidColor[3]);
  }

  device->destroyImage(texture);
  device->destroySampler(nearest);
  device->destroyPipeline(pipeline);
}

namespace {

// One block of a constant HDR colour, 0.5, 0.25 and 0.125 with an alpha of one. BC6H has no alpha,
// and its mode 11 (ten bits per channel, one region) with equal endpoints and every index 0
// decodes to the endpoint: the ten-bit values below unquantise and scale to half-float bit
// patterns within a few units of 0x3800, 0x3400 and 0x3000. An ASTC void-extent block marked HDR
// carries four 16-bit half floats.
constexpr std::array<std::uint16_t, 4> HdrHalves{0x3800, 0x3400, 0x3000, 0x3C00};
constexpr std::array<std::uint32_t, 3> Bc6hChannels{462, 429, 396};

std::array<std::byte, 16> hdrSolidBlock(Format format) {
  std::array<std::byte, 16> block{};
  if (isAstcHdrFormat(format)) {
    const std::array<std::uint8_t, 8> header{0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (std::size_t i = 0; i < header.size(); ++i) {
      block[i] = std::byte{header[i]};
    }
    for (std::size_t c = 0; c < 4; ++c) {
      block[8 + c * 2] = std::byte{static_cast<std::uint8_t>(HdrHalves[c] & 0xFF)};
      block[8 + c * 2 + 1] = std::byte{static_cast<std::uint8_t>(HdrHalves[c] >> 8)};
    }
    return block;
  }
  std::size_t bit = 0;
  const auto put = [&](std::uint32_t value, std::size_t bits) {
    for (std::size_t i = 0; i < bits; ++i, ++bit) {
      if (((value >> i) & 1u) != 0) {
        block[bit / 8] |= std::byte{static_cast<std::uint8_t>(1u << (bit % 8))};
      }
    }
  };
  put(3, 5); // mode 11
  for (int endpoint = 0; endpoint < 2; ++endpoint) {
    for (const std::uint32_t channel : Bc6hChannels) {
      put(channel, 10);
    }
  }
  return block; // the indices stay 0
}

} // namespace

// The HDR counterpart of the test above: BC6H runs wherever there is block compression, ASTC HDR on
// the devices that report astcHdrSupported (recent phones and Apple silicon).
TEST_CASE("an HDR compressed image of any size uploads every level and samples its colour",
          "[rhi][bindless][upload][compressed][hdr][gpu]") {
  test::TestDevice device;
  const auto format = GENERATE(Format::BC6HUfloat, Format::ASTC4x4Sfloat);
  if (!formatSupported(device->info(), format)) {
    SKIP("the device cannot sample " << toString(format));
  }
  const ShaderHandle shader = loadShader(device, "texture");
  const PipelineHandle pipeline = device->createGraphicsPipeline(
      {.shader = shader, .colorFormats = {Format::R8G8B8A8Unorm}, .cullMode = CullMode::None, .debugName = "texture"});
  device->destroyShader(shader);
  const SamplerHandle nearest =
      device->createSampler({.filter = Filter::Nearest, .mipFilter = Filter::Nearest, .debugName = "nearest"});
  constexpr glm::uvec2 size{13, 7};
  const std::uint32_t levels = fullMipCount(size);
  const ImageHandle texture = device->createImage({.size = size,
                                                   .format = format,
                                                   .usage = ImageUsage::Sampled | ImageUsage::TransferDst,
                                                   .mipLevels = levels,
                                                   .debugName = std::string{toString(format)}});
  const std::array<std::byte, 16> block = hdrSolidBlock(format);
  std::vector<std::vector<std::byte>> data;
  std::vector<ImageUpload> uploads;
  for (std::uint32_t level = 0; level < levels; ++level) {
    auto &bytes = data.emplace_back();
    const std::uint64_t byteSize = levelByteSize(format, mipSize(size, level));
    for (std::uint64_t offset = 0; offset < byteSize; offset += block.size()) {
      bytes.insert(bytes.end(), block.begin(), block.end());
    }
  }
  uploads.reserve(levels);
  for (std::uint32_t level = 0; level < levels; ++level) {
    uploads.push_back({.mipLevel = level, .data = data[level]});
  }
  device->uploadImage(texture, uploads);

  Target target{device, {2, 2}};
  for (const float lod : {0.0f, static_cast<float>(levels - 1)}) {
    const TexturePush push{
        .texture = device->sampledImageIndex(texture), .sampler = device->samplerIndex(nearest), .lod = lod};
    target.draw(device->beginFrame(), pipeline, push);
    device->endFrame();
    const Pixel pixel = target.pixel(1, 1);
    CHECK(std::abs(pixel.r - 128) <= 2);
    CHECK(std::abs(pixel.g - 64) <= 2);
    CHECK(std::abs(pixel.b - 32) <= 2);
    CHECK(pixel.a == 255);
  }

  device->destroyImage(texture);
  device->destroySampler(nearest);
  device->destroyPipeline(pipeline);
}
