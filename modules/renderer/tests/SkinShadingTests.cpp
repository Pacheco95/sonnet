// Pre-integrated skin shading (ADR-0041, "Implementation split and acceptance"): the pipelines and
// batches against the null device's trace, and the lighting on a GPU (Lavapipe in CI).
#include <sonnet/renderer/Primitives.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace sonnet::renderer;
using namespace sonnet::rhi;

namespace {

std::filesystem::path shaderDir(sonnet::platform::Platform &platform) {
  return platform.basePath() / "shaders";
}

// Small everything, as RendererTests.cpp's, and no shadows: the tests compare the sun's direct light
// alone, which its shadow would only multiply.
RendererSettings skinSettings() {
  return RendererSettings{.shadows = false,
                          .bloom = false,
                          .bloomLevels = 2,
                          .antialiasing = AntiAliasing::None,
                          .occlusionCulling = false,
                          .environmentSize = 8,
                          .irradianceSize = 4,
                          .irradianceSamples = 8,
                          .prefilteredSize = 4,
                          .prefilteredLevels = 2,
                          .prefilterSamples = 8,
                          .brdfLutSize = 8,
                          .brdfLutSamples = 8};
}

std::unique_ptr<IDevice> gpuDevice(sonnet::platform::Platform &platform) {
  try {
    return createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
}

std::size_t countLines(const NullDevice &device, std::string_view text) {
  return static_cast<std::size_t>(
      std::ranges::count_if(device.trace(), [&](const std::string &line) { return line.find(text) != line.npos; }));
}

// A white, rough, dielectric Skin material: its diffuse is what the tests compare.
MaterialDesc skinMaterial(float subsurfaceRadius = MaxSubsurfaceRadius) {
  MaterialDesc desc;
  desc.metallic = 0.0f;
  desc.roughness = 1.0f;
  desc.shading = ShadingModel::Skin;
  desc.subsurface = 1.0f;
  desc.subsurfaceRadius = subsurfaceRadius;
  return desc;
}

MaterialDesc standardMaterial() {
  MaterialDesc desc = skinMaterial();
  desc.shading = ShadingModel::Standard;
  return desc;
}

// A 5 cm sphere 15 cm in front of the camera, lit by the sun from +X, at 90 degrees to the view:
// the terminator is the sphere's vertical centre line. At the largest radius, 1 cm a profile
// millimetre, the curvature puts the lookup row at 2 * 0.01 / 0.05 = 0.4.
constexpr float SphereRadius = 0.05f;

SceneView sideLitSphere(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 0.0f, 0.15f};
  view.camera.nearPlane = 0.01f;
  view.sun.direction = {-1.0f, 0.0f, 0.0f};
  view.sun.color = {1.0f, 1.0f, 1.0f};
  view.sun.intensity = 3.0f;
  view.ambient = {0.0f, 0.0f, 0.0f};
  view.draws = draws;
  return view;
}

// Draws a view into a target and reads back the forward passes' linear HDR image.
struct GpuScene {
  IDevice &device;
  Renderer &renderer;
  glm::uvec2 size;
  RenderGraph graph;
  RenderTarget target;
  BufferHandle readback;

  GpuScene(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), size(targetSize), graph(gpu), target(gpu, "viewport") {
    target.resize(size);
    readback = device.createBuffer({.size = std::uint64_t{size.x} * size.y * 8,
                                    .usage = BufferUsage::TransferDst,
                                    .memory = MemoryUsage::GpuToCpu,
                                    .debugName = "hdr readback"});
  }
  ~GpuScene() {
    device.waitIdle();
    device.destroyBuffer(readback);
  }
  GpuScene(const GpuScene &) = delete;
  GpuScene &operator=(const GpuScene &) = delete;

  // Two frames, so the second reads the lookup tables the first computed.
  std::vector<glm::vec3> render(const SceneView &view) {
    for (int frame = 0; frame < 2; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      const GraphImage color = graph.importImage(target.color());
      const GraphImage depth = graph.importImage(target.depth());
      renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
      if (frame == 1) {
        const GraphImage hdr = renderer.sceneColor();
        graph.addPass(
            "readback", [&](PassBuilder &builder) { builder.transferSrc(hdr); },
            [this, hdr](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(hdr), readback);
            });
      }
      graph.execute(commands);
      device.endFrame();
    }
    device.waitIdle();
    const auto bytes = device.mappedRange(readback);
    std::vector<glm::vec3> pixels(std::size_t{size.x} * size.y);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      std::array<std::uint16_t, 4> halves{};
      std::memcpy(halves.data(), bytes.data() + i * 8, 8);
      pixels[i] = {glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1]), glm::unpackHalf1x16(halves[2])};
    }
    return pixels;
  }
};

constexpr glm::uvec2 Size{128, 128};

// The image's middle row, from its right edge (the lit side) to its left.
std::vector<glm::vec3> middleRow(const std::vector<glm::vec3> &pixels) {
  std::vector<glm::vec3> row;
  const std::size_t y = Size.y / 2;
  for (std::size_t x = Size.x; x-- > 0;) {
    row.push_back(pixels[y * Size.x + x]);
  }
  return row;
}

// One sphere drawn with `desc`'s material, its sun-direct term read back.
std::vector<glm::vec3> sunDirect(IDevice &device, Renderer &renderer, const MaterialDesc &desc) {
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(SphereRadius, 96, 48), "sphere");
  const MaterialHandle material = renderer.createMaterial(desc, "material");
  const std::array draws{DrawItem{.mesh = sphere, .material = material}};
  GpuScene scene{device, renderer, Size};
  std::vector<glm::vec3> pixels = scene.render(sideLitSphere(draws));
  renderer.destroyMaterial(material);
  renderer.destroyMesh(sphere);
  return pixels;
}

} // namespace

TEST_CASE("a Skin draw takes the skin pipeline and a batch of its own", "[renderer][null][skin]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  const bool characterShading = GENERATE(true, false);
  CAPTURE(characterShading);
  RendererSettings settings = skinSettings();
  settings.characterShading = characterShading;
  Renderer renderer{*device, shaderDir(platform), settings};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MaterialHandle skin = renderer.createMaterial(skinMaterial(), "skin");
  const MaterialHandle standard = renderer.createMaterial(standardMaterial(), "standard");
  MaterialDesc noScattering = skinMaterial();
  noScattering.subsurface = 0.0f;
  const MaterialHandle weightless = renderer.createMaterial(noScattering, "weightless");
  MaterialDesc blendedDesc = skinMaterial();
  blendedDesc.alphaMode = AlphaMode::Blend;
  const MaterialHandle blended = renderer.createMaterial(blendedDesc, "blended skin");
  const std::array draws{
      DrawItem{.mesh = box, .material = standard, .transform = glm::translate(glm::mat4{1.0f}, {-1.5f, 0.0f, 0.0f})},
      DrawItem{.mesh = box, .material = skin},
      DrawItem{.mesh = box, .material = weightless, .transform = glm::translate(glm::mat4{1.0f}, {1.5f, 0.0f, 0.0f})},
      DrawItem{.mesh = box, .material = blended, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 1.5f, 0.0f})}};
  SceneView view;
  view.camera.position = {0.0f, 0.0f, 6.0f};
  view.draws = draws;

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // On, the one Skin draw with scattering is the skin pipeline's, in a batch of its own: the
  // weightless one shades as Standard and batches with the Standard box, and the blended one goes
  // through the blend pipeline. The pre-pass draws the same batches through its one pipeline.
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"forward skin\"") == (characterShading ? 1u : 0u));
  REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 1);
  // The pre-pass and the forward pass submit the opaque batches indirectly: two with the skin
  // draw apart, one with it shading as Standard among the others.
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == (characterShading ? 4u : 2u));
  REQUIRE(countLines(*device, "dispatch") > 0);

  renderer.destroyMaterial(blended);
  renderer.destroyMaterial(weightless);
  renderer.destroyMaterial(standard);
  renderer.destroyMaterial(skin);
  renderer.destroyMesh(box);
}

TEST_CASE("a Skin sphere lit at 90 degrees has a redder, wider terminator than a Standard one on a GPU",
          "[renderer][gpu][skin]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = skinSettings();
    settings.debugView = DebugView::SunDirect;
    Renderer renderer{*device, shaderDir(platform), settings};
    const std::vector<glm::vec3> standard = middleRow(sunDirect(*device, renderer, standardMaterial()));
    const std::vector<glm::vec3> skin = middleRow(sunDirect(*device, renderer, skinMaterial()));
    REQUIRE(device->validationMessageCount() == 0);

    // The lit band along the row: how many pixels are above half a percent of the lit peak, per
    // channel. Standard's stops at the terminator, the sphere's centre column, with red and green
    // the same white; skin's red reaches past it and its green barely.
    float peak = 0.0f;
    for (const glm::vec3 &p : standard) {
      peak = std::max(peak, p.r);
    }
    const auto band = [&](const std::vector<glm::vec3> &row, int channel) {
      return std::ranges::count_if(row, [&](const glm::vec3 &p) { return p[channel] > 0.005f * peak; });
    };
    const auto standardRed = band(standard, 0);
    const auto skinRed = band(skin, 0);
    const auto skinGreen = band(skin, 1);
    CAPTURE(peak, standardRed, band(standard, 1), skinRed, skinGreen);
    REQUIRE(band(standard, 1) == standardRed);
    // Pinned on Lavapipe at a lookup row of 0.4: 39 pixels for Standard, 9 more in skin's red and 1
    // in its green.
    REQUIRE(skinRed >= standardRed + 5);
    REQUIRE(skinRed <= standardRed + 15);
    REQUIRE(skinGreen <= standardRed + 3);

    // Red over green where Standard's light has fallen below 5 % of its peak, and at the first pixel
    // past the terminator, where Standard is dark.
    const auto first = [&](float level) {
      return static_cast<std::size_t>(std::distance(
          standard.begin(), std::ranges::find_if(standard.begin() + static_cast<std::ptrdiff_t>(Size.x / 4),
                                                 standard.end(), [&](const glm::vec3 &p) { return p.r <= level; })));
    };
    const std::size_t fading = first(0.05f * peak);
    const std::size_t dark = first(0.0f);
    REQUIRE(dark < standard.size());
    const float standardRatio = standard[fading].r / standard[fading].g;
    const float fadingRatio = skin[fading].r / skin[fading].g;
    const float darkRatio = skin[dark].r / std::max(skin[dark].g, 1.0e-6f);
    CAPTURE(fading, dark, standardRatio, fadingRatio, darkRatio, skin[dark].r);
    REQUIRE(standardRatio == Catch::Approx(1.0f).margin(0.01f));
    // Pinned on Lavapipe: 1.24 and 2.2.
    REQUIRE(fadingRatio > 1.1f);
    REQUIRE(fadingRatio < 1.5f);
    REQUIRE(skin[dark].r > 0.01f * peak);
    REQUIRE(darkRatio > 1.5f);
    REQUIRE(darkRatio < 4.0f);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a Skin material draws as Standard with characterShading off on a GPU", "[renderer][gpu][skin]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = skinSettings();
    settings.characterShading = false;
    Renderer renderer{*device, shaderDir(platform), settings};
    const std::vector<glm::vec3> standard = sunDirect(*device, renderer, standardMaterial());
    const std::vector<glm::vec3> skin = sunDirect(*device, renderer, skinMaterial());
    REQUIRE(std::ranges::equal(standard, skin));
    REQUIRE(std::ranges::any_of(standard, [](const glm::vec3 &p) { return p.r > 0.1f; }));
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the skin lookup table's flat row is the clamped cosine on a GPU", "[renderer][gpu][skin]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    // A radius of 0 puts every pixel on the table's first row, a ring hundreds of millimetres
    // across, and an untextured sphere's mapped normal is its smooth one: skin's diffuse is then
    // Lambert's, so the sun-direct term matches Standard's to the table's precision.
    RendererSettings settings = skinSettings();
    settings.debugView = DebugView::SunDirect;
    Renderer renderer{*device, shaderDir(platform), settings};
    const std::vector<glm::vec3> standard = sunDirect(*device, renderer, standardMaterial());
    const std::vector<glm::vec3> skin = sunDirect(*device, renderer, skinMaterial(0.0f));
    float worst = 0.0f;
    float peak = 0.0f;
    for (std::size_t i = 0; i < standard.size(); ++i) {
      const glm::vec3 difference = glm::abs(standard[i] - skin[i]);
      worst = std::max({worst, difference.r, difference.g, difference.b});
      peak = std::max(peak, standard[i].r);
    }
    CAPTURE(worst, peak);
    REQUIRE(peak > 0.1f);
    // Half-float storage and the table's 128 columns: well under one percent of the peak.
    REQUIRE(worst < 0.01f * peak);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
