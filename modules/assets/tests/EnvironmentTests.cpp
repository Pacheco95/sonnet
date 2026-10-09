#include "AssetTestSupport.h"

#include <sonnet/assets/Importers.h>
#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Primitives.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <vector>

using namespace sonnet;

namespace {

constexpr glm::uvec2 FrameSize{128, 128};

// The sky behind two spheres, one rough and white, lit by the irradiance cube, and one smooth
// metal, which reflects the prefiltered cube: all three cubes of an environment show up in one
// frame. Returns the frame's RGBA8 pixels.
std::vector<std::uint8_t> renderWith(rhi::IDevice &device, const std::filesystem::path &shaders,
                                     const renderer::TextureData &map) {
  renderer::RendererSettings settings;
  settings.shadows = false;
  settings.bloom = false;
  settings.antialiasing = renderer::AntiAliasing::None;
  settings.occlusionCulling = false;
  settings.environmentSize = 128;
  settings.irradianceSize = 16;
  settings.irradianceSamples = 128;
  settings.prefilteredSize = 64;
  settings.prefilteredLevels = 4;
  settings.prefilterSamples = 128;
  settings.brdfLutSize = 32;
  renderer::Renderer renderer{device, shaders, settings};
  const renderer::EnvironmentHandle environment = renderer.createEnvironment(map, "sky");
  REQUIRE(renderer.isValid(environment));
  const renderer::MeshHandle sphere = renderer.createMesh(renderer::primitives::sphere(0.5f, 32, 16), "sphere");
  renderer::MaterialDesc matte;
  matte.metallic = 0.0f;
  matte.roughness = 1.0f;
  renderer::MaterialDesc metal;
  metal.baseColor = {0.95f, 0.9f, 0.8f, 1.0f};
  metal.metallic = 1.0f;
  metal.roughness = 0.3f;
  const renderer::MaterialHandle matteMaterial = renderer.createMaterial(matte, "matte");
  const renderer::MaterialHandle metalMaterial = renderer.createMaterial(metal, "metal");
  const std::array draws{
      renderer::DrawItem{
          .mesh = sphere, .material = matteMaterial, .transform = glm::translate(glm::mat4{1.0f}, {-0.7f, 0.0f, 0.0f})},
      renderer::DrawItem{
          .mesh = sphere, .material = metalMaterial, .transform = glm::translate(glm::mat4{1.0f}, {0.7f, 0.0f, 0.0f})}};
  renderer::SceneView view;
  view.camera.position = {0.0f, 0.0f, 3.0f};
  view.draws = draws;
  view.hasSun = false;
  view.ambient = {0.0f, 0.0f, 0.0f};
  view.environment = environment;

  renderer::RenderGraph graph{device};
  renderer::RenderTarget target{device, "viewport"};
  target.resize(FrameSize);
  const rhi::BufferHandle readback = device.createBuffer({.size = std::uint64_t{FrameSize.x} * FrameSize.y * 4,
                                                          .usage = rhi::BufferUsage::TransferDst,
                                                          .memory = rhi::MemoryUsage::GpuToCpu,
                                                          .debugName = "readback"});
  for (int frame = 0; frame < 2; ++frame) {
    rhi::ICommandList &commands = device.beginFrame();
    graph.reset();
    const renderer::GraphImage color = graph.importImage(target.color());
    const renderer::GraphImage depth = graph.importImage(target.depth());
    renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
    if (frame == 1) {
      graph.addPass(
          "readback", [&](renderer::PassBuilder &b) { b.transferSrc(color); },
          [&](rhi::ICommandList &cmd, const renderer::PassResources &resources) {
            cmd.copyImageToBuffer(resources.image(color), readback);
          });
    }
    graph.execute(commands);
    device.endFrame();
  }
  device.waitIdle();
  const auto mapped = device.mappedRange(readback);
  std::vector<std::uint8_t> pixels(mapped.size());
  std::ranges::transform(mapped, pixels.begin(), [](std::byte b) { return static_cast<std::uint8_t>(b); });
  device.destroyBuffer(readback);
  renderer.destroyMaterial(metalMaterial);
  renderer.destroyMaterial(matteMaterial);
  renderer.destroyMesh(sphere);
  renderer.destroyEnvironment(environment);
  return pixels;
}

struct Difference {
  double mean{0.0}; // of the RGB levels, over the frame
  int worst{0};
};

Difference compare(const std::vector<std::uint8_t> &expected, const std::vector<std::uint8_t> &actual) {
  REQUIRE(expected.size() == actual.size());
  Difference result;
  std::size_t count = 0;
  for (std::size_t i = 0; i < expected.size(); i += 4) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const int d = std::abs(static_cast<int>(expected[i + channel]) - static_cast<int>(actual[i + channel]));
      result.mean += d;
      result.worst = std::max(result.worst, d);
      ++count;
    }
  }
  result.mean /= static_cast<double>(count);
  return result;
}

} // namespace

// ADR-0024: the cooked environment gives the same skybox, irradiance and prefiltered cubes as the
// map it came from, within what UASTC HDR and the BC6H transcode cost. The frame shows all three;
// the tolerance is set above what each path measured, as the LDR PSNR floors are.
TEST_CASE("a cooked environment lights a scene like the map it came from on a GPU", "[assets][texture][hdr][gpu]") {
  platform::Platform platform{{.headless = true}};
  std::unique_ptr<rhi::IDevice> device;
  try {
    device = rhi::createDevice({.platform = &platform, .applicationName = "assets_tests"});
  } catch (const core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  const std::filesystem::path shaders = platform.basePath() / "shaders";
  const renderer::TextureData sky = assets::test::syntheticSky();
  const auto cooked = assets::cookHdrKtx2(sky);
  REQUIRE(cooked.has_value());
  const std::vector<std::uint8_t> reference = renderWith(*device, shaders, sky);

  // What this device samples: BC6H where it has block compression, ASTC HDR where it has that.
  const auto native = assets::readKtx2(*cooked, device->info());
  REQUIRE(native.has_value());
  const auto half = assets::readKtx2(*cooked, rhi::DeviceInfo{});
  REQUIRE(half.has_value());
  REQUIRE(half->format == rhi::Format::R16G16B16A16Sfloat);
  const Difference viaHalf = compare(reference, renderWith(*device, shaders, *half));
  const Difference viaNative = compare(reference, renderWith(*device, shaders, *native));
  // The control: a flat grey sky of the same mean brightness shows the test would notice a wrong
  // map, since the frame differs from the reference by far more than the tolerances below.
  renderer::TextureData flat = sky;
  const std::array<std::uint16_t, 4> grey{glm::packHalf1x16(0.4f), glm::packHalf1x16(0.4f), glm::packHalf1x16(0.4f),
                                          glm::packHalf1x16(1.0f)};
  for (std::size_t offset = 0; offset < flat.data.size(); offset += sizeof(grey)) {
    std::memcpy(flat.data.data() + offset, grey.data(), sizeof(grey));
  }
  const Difference control = compare(reference, renderWith(*device, shaders, flat));
  INFO(std::format("control: mean {:.2f}; RGBA16F: mean {:.3f} worst {}; {}: mean {:.3f} worst {}", control.mean,
                   viaHalf.mean, viaHalf.worst, rhi::toString(native->format), viaNative.mean, viaNative.worst));
  CHECK(control.mean > 5.0);
  // Measured on an RTX 4090 and on Lavapipe: a mean of 0.2 levels and a worst of 5 for both paths.
  CHECK(viaHalf.mean <= 1.0);
  CHECK(viaHalf.worst <= 16);
  CHECK(viaNative.mean <= 1.0);
  CHECK(viaNative.worst <= 16);
  REQUIRE(device->validationMessageCount() == 0);
}
