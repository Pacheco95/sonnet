#include <sonnet/core/JobSystem.h>
#include <sonnet/renderer/Primitives.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Error.h>
#include <sonnet/core/File.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace sonnet::renderer;
using namespace sonnet::rhi;

namespace {

std::size_t countLines(const NullDevice &device, std::string_view text) {
  return static_cast<std::size_t>(
      std::ranges::count_if(device.trace(), [&](const std::string &line) { return line.contains(text); }));
}

std::size_t lineIndex(const NullDevice &device, std::string_view text) {
  const auto it = std::ranges::find_if(device.trace(), [&](const std::string &line) { return line.contains(text); });
  REQUIRE(it != device.trace().end());
  return static_cast<std::size_t>(it - device.trace().begin());
}

bool hasPass(const RenderGraph &graph, std::string_view name) {
  return std::ranges::any_of(graph.statistics().passes, [&](const PassTiming &pass) { return pass.name == name; });
}

std::filesystem::path shaderDir(sonnet::platform::Platform &platform) {
  return platform.basePath() / "shaders";
}

// Small everything, so Lavapipe finishes in seconds; no bloom or anti-aliasing, so pixels are
// what the passes before them wrote.
RendererSettings testSettings() {
  return RendererSettings{.shadows = true,
                          .shadowMapSize = 256,
                          .shadowDistance = 30.0f,
                          .shadowBias = 0.0015f,
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

// A camera three metres back looking at the origin, and a box there.
SceneView boxScene(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 0.0f, 3.0f};
  view.draws = draws;
  return view;
}

// A camera six metres up looking straight down, screen right along +X.
SceneView topDownScene(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 6.0f, 0.0f};
  view.camera.rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f});
  view.draws = draws;
  return view;
}

// A constant-colour RGBA16F equirectangular map.
TextureData skyTexture(glm::vec3 color) {
  TextureData texture{.size = {8, 4}, .format = Format::R16G16B16A16Sfloat, .mipLevels = 1, .cube = false, .data = {}};
  texture.data.resize(static_cast<std::size_t>(texture.expectedSize()));
  const std::array<std::uint16_t, 4> texel{glm::packHalf1x16(color.r), glm::packHalf1x16(color.g),
                                           glm::packHalf1x16(color.b), glm::packHalf1x16(1.0f)};
  for (std::size_t i = 0; i < texture.data.size(); i += sizeof(texel)) {
    std::memcpy(texture.data.data() + i, texel.data(), sizeof(texel));
  }
  return texture;
}

TextureData detailedSky() {
  TextureData map{.size = {512, 256}, .format = Format::R16G16B16A16Sfloat, .data = {}};
  map.data.resize(static_cast<std::size_t>(map.expectedSize()));
  for (unsigned y = 0; y < map.size.y; ++y) {
    for (unsigned x = 0; x < map.size.x; ++x) {
      // Continuous at the longitude wrap and constant at either pole.
      const float latitude = std::sin((float(y) + 0.5f) * glm::pi<float>() / float(map.size.y));
      const float value = 0.5f + 0.4f * latitude * std::cos((float(x) + 0.5f) * glm::two_pi<float>() / 8.0f);
      const std::array<std::uint16_t, 4> texel{glm::packHalf1x16(value), glm::packHalf1x16(value),
                                               glm::packHalf1x16(value), glm::packHalf1x16(1.0f)};
      std::memcpy(map.data.data() + (std::size_t(y) * map.size.x + x) * sizeof(texel), texel.data(), sizeof(texel));
    }
  }
  return map;
}

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

// A GPU device, or a skip.
std::unique_ptr<IDevice> gpuDevice(sonnet::platform::Platform &platform) {
  try {
    return createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
}

// Draws the view into a target for `frames` frames and reads the last one back.
struct GpuScene {
  IDevice &device;
  Renderer &renderer;
  glm::uvec2 size;
  RenderGraph graph;
  RenderTarget target;
  BufferHandle readback;
  bool grid{false};
  glm::vec4 gridPlane{0, 1, 0, 0};
  glm::vec3 gridAxisU{1, 0, 0};
  std::span<const OverlayQuad> overlay; // drawn over the scene, when there are any
  // Wall-clock time of each frame's endFrame, where the frame is submitted. MoltenVK encodes the
  // recorded commands into Metal there, one Metal draw per indirect command (ADR-0016), which no
  // pass's recording time includes.
  std::vector<double> submitMilliseconds;

  GpuScene(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), size(targetSize), graph(gpu), target(gpu, "viewport") {
    target.resize(size);
    readback = device.createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                    .usage = BufferUsage::TransferDst,
                                    .memory = MemoryUsage::GpuToCpu,
                                    .debugName = "readback"});
  }
  ~GpuScene() {
    device.waitIdle();
    device.destroyBuffer(readback);
  }
  GpuScene(const GpuScene &) = delete;
  GpuScene &operator=(const GpuScene &) = delete;

  void render(const SceneView &view, int frames = 1) {
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      const GraphImage color = graph.importImage(target.color());
      const GraphImage depth = graph.importImage(target.depth());
      renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
      if (grid) {
        renderer.addGridPass(graph, view, color, depth, gridPlane, gridAxisU);
      }
      renderer.addDebugLinePass(graph, view, color, depth);
      renderer.addOverlayPass(graph, color, overlay);
      if (frame == frames - 1) {
        graph.addPass(
            "readback", [&](PassBuilder &b) { b.transferSrc(color); },
            [&](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), readback);
            });
      }
      graph.execute(commands);
      const auto submitStart = std::chrono::steady_clock::now();
      device.endFrame();
      submitMilliseconds.push_back(
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitStart).count());
    }
    device.waitIdle();
  }

  Pixel pixel(unsigned x, unsigned y) {
    return pixelAt(device.mappedRange(readback), size, x, y);
  }
};

} // namespace

TEST_CASE("the scene passes shade every item once after the depth pre-pass and the shadows", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({128, 64});
  ICommandList &commands = device->beginFrame();
  // Created inside the frame so the trace shows the uploads.
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  REQUIRE(renderer.isValid(box));
  REQUIRE(renderer.submeshes(box).size() == 1);
  REQUIRE(renderer.submeshes(box)[0].indexCount == 36);
  REQUIRE(renderer.meshBounds(box).max == glm::vec3{0.5f, 0.5f, 0.5f});
  const std::array draws{DrawItem{.mesh = box}, DrawItem{.mesh = sphere}, DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // Meshes live in device-local memory, filled through the staging ring.
  REQUIRE(countLines(*device, "uploadBuffer \"box vertices\"") == 1);
  REQUIRE(countLines(*device, "uploadBuffer \"box indices\"") == 1);
  // The lookup table is computed by the first frame, then the scene: four cascades, the depth
  // pre-pass, clustering in compute, the forward pass and tone mapping.
  REQUIRE(hasPass(graph, "brdf lut"));
  REQUIRE(hasPass(graph, "shadow cascade 0"));
  REQUIRE(hasPass(graph, "shadow cascade 3"));
  REQUIRE(hasPass(graph, "depth"));
  REQUIRE(hasPass(graph, "light clustering"));
  REQUIRE(hasPass(graph, "forward"));
  REQUIRE(hasPass(graph, "tonemap"));
  REQUIRE(!hasPass(graph, "fxaa"));
  REQUIRE(!hasPass(graph, "bloom down 0"));
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == 0);
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"shadow\"") == 4);
  REQUIRE(countLines(*device, "bindPipeline \"light clustering\"") == 1);
  REQUIRE(countLines(*device, "dispatch 4 3 6") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"tonemap\"") == 1);
  // The opaque draws are submitted from the GPU (ADR-0016): one culling pass ahead of the scene
  // empties the commands and tests the six frusta, and each drawing pass then issues one command
  // per batch, its survivors as instances. The three draws are two boxes and a sphere sorted by
  // mesh, so two batches each in the four cascades, the pre-pass and the forward pass.
  REQUIRE(hasPass(graph, "cull"));
  REQUIRE(lineIndex(*device, "bindPipeline \"cull\"") < lineIndex(*device, "bindPipeline \"shadow\""));
  REQUIRE(countLines(*device, "bindPipeline \"clear draw commands\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"cull\"") == 1);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 12);
  // The trailing space matches only the direct form, not drawIndexedIndirect.
  REQUIRE(countLines(*device, "drawIndexed ") == 0);                   // nothing direct: no blended draws
  REQUIRE(countLines(*device, "uploadBuffer \"visible draws\"") == 0); // so no direct range either
  REQUIRE(lineIndex(*device, "bindPipeline \"depth\"") < lineIndex(*device, "bindPipeline \"forward\""));
  // The statistics report what was submitted; the GPU decides what survives.
  REQUIRE(renderer.statistics().indirectCallCount == 12);
  REQUIRE(renderer.statistics().drawCount == 3);
  REQUIRE(renderer.statistics().shadowDrawCount == 12);
  REQUIRE(renderer.statistics().triangleCount == 12 * 2 + (8 * 2 * 2 + 8 * 2));

  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
  REQUIRE(!renderer.isValid(box));
}

TEST_CASE("a batch is one submesh of one mesh, drawn as the instances of one command", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({128, 64});
  // A box in two halves, each its own submesh, as a glTF mesh with two materials would be.
  MeshData halves = primitives::box();
  const auto half = static_cast<std::uint32_t>(halves.indices.size() / 2);
  halves.submeshes = {Submesh{.firstIndex = 0, .indexCount = half}, Submesh{.firstIndex = half, .indexCount = half}};
  const MeshHandle box = renderer.createMesh(halves, "halves");
  // Three draws of the first half and two of the second, interleaved, so only the sort groups them.
  const std::array draws{DrawItem{.mesh = box, .submesh = 1}, DrawItem{.mesh = box},
                         DrawItem{.mesh = box, .submesh = 1}, DrawItem{.mesh = box}, DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // Five draws, two batches, and so two commands in each of the six drawing passes, against one
  // index buffer bound once per pass.
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 12);
  REQUIRE(countLines(*device, "bindIndexBuffer \"halves indices\"") == 6);
  REQUIRE(renderer.statistics().indirectCallCount == 12);
  REQUIRE(renderer.statistics().drawCount == 5);
  renderer.destroyMesh(box);
}

TEST_CASE("a stale mesh handle draws nothing and an empty scene records no draw", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  renderer.destroyMesh(box);
  const std::array draws{DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({8, 8});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "drawIndexed") == 0);
  REQUIRE(!hasPass(graph, "shadow cascade 0")); // nothing casts, so no cascades
  REQUIRE(renderer.statistics().drawCount == 0);
}

TEST_CASE("blended materials draw after the opaque scene, farthest first", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  MaterialDesc glass;
  glass.alphaMode = AlphaMode::Blend;
  glass.baseColor = {1.0f, 1.0f, 1.0f, 0.5f};
  const MaterialHandle glassMaterial = renderer.createMaterial(glass, "glass");
  MaterialDesc cutout;
  cutout.alphaMode = AlphaMode::Mask;
  cutout.doubleSided = true;
  const MaterialHandle cutoutMaterial = renderer.createMaterial(cutout, "cutout");
  REQUIRE(renderer.material(glassMaterial).alphaMode == AlphaMode::Blend);
  // The near sphere and the far box are blended; the double-sided cutout box is opaque.
  const std::array draws{
      DrawItem{
          .mesh = sphere, .material = glassMaterial, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, 1.0f})},
      DrawItem{.mesh = box, .material = cutoutMaterial},
      DrawItem{
          .mesh = box, .material = glassMaterial, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, -2.0f})},
  };
  const SceneView view = boxScene(draws);

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // The cutout box alone goes through the pre-pass, with the double-sided pipelines; the blended
  // items through the blend pipelines, box before sphere, and also through the cascades, which
  // cast their shadows with a hashed alpha test.
  REQUIRE(countLines(*device, "bindPipeline \"depth double sided\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 0);
  REQUIRE(countLines(*device, "bindPipeline \"forward double sided\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == 1);
  // The one opaque draw is submitted indirectly by the pre-pass and the forward pass, and all
  // three draws, in three batches, by each of the four cascades; the two blended ones stay on the
  // direct path in the forward pass (ADR-0012), naming their objects through the visible list's
  // direct range, uploaded once (ADR-0016).
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 2 + 4 * 3);
  REQUIRE(countLines(*device, "uploadBuffer \"visible draws\"") == 1);
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 1); // the blended box
  REQUIRE(countLines(*device, "drawIndexed ") == 2);      // and the blended sphere
  const std::size_t blend = lineIndex(*device, "bindPipeline \"forward blend\"");
  REQUIRE(lineIndex(*device, "bindPipeline \"forward double sided\"") < blend);
  const auto &trace = device->trace();
  const auto firstBlendedDraw = std::find_if(trace.begin() + static_cast<std::ptrdiff_t>(blend), trace.end(),
                                             [](const std::string &line) { return line.starts_with("drawIndexed "); });
  REQUIRE(firstBlendedDraw != trace.end());
  REQUIRE(*firstBlendedDraw == "drawIndexed 36 x1"); // the far box, then the near sphere
  REQUIRE(renderer.statistics().drawCount == 3);
  REQUIRE(renderer.statistics().shadowDrawCount == 4 * 3);

  renderer.destroyMaterial(cutoutMaterial);
  renderer.destroyMaterial(glassMaterial);
  REQUIRE(!renderer.isValid(glassMaterial));
  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
}

TEST_CASE("textures upload every level and are reached by index, the white default for a stale handle",
          "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  TextureData data = solidTexture({200, 100, 50, 255});
  data.size = {4, 2};
  data.data.resize(std::size_t{4} * 2 * 4, std::byte{7});
  generateMipChain(data);
  REQUIRE(data.mipLevels == 3);
  REQUIRE(data.data.size() == data.expectedSize());
  REQUIRE(data.level(2).size() == 4);
  // The first level-1 texel averages the coloured texel with three grey ones; the second is grey.
  REQUIRE(std::to_integer<int>(data.level(1)[0]) == 55);
  REQUIRE(std::to_integer<int>(data.level(1)[4]) == 7);

  const TextureHandle texture = renderer.createTexture(data, "checker");
  REQUIRE(renderer.isValid(texture));
  REQUIRE(countLines(*device, "uploadImage \"checker\" level 0 layer 0 32 bytes") == 1);
  REQUIRE(countLines(*device, "uploadImage \"checker\" level 2 layer 0 4 bytes") == 1);
  const std::uint32_t white = renderer.textureIndex({});
  REQUIRE(white != InvalidBindlessIndex);
  REQUIRE(renderer.textureIndex(texture) != white);
  renderer.destroyTexture(texture);
  REQUIRE(renderer.textureIndex(texture) == white);

  TextureData wrong = solidTexture({0, 0, 0, 0});
  wrong.size = {2, 2}; // four texels promised, one given
  REQUIRE(!renderer.createTexture(wrong, "wrong").isValid());
}

// A map wider than four times the settings' cube keeps its texels: its cube is a quarter of its
// width, up to the ceiling, which the number of mip passes shows.
TEST_CASE("an environment's cube follows its map up to the ceiling", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  const auto levels = [&](std::uint32_t mapWidth, std::uint32_t ceiling) {
    RendererSettings settings = testSettings();
    settings.maxEnvironmentSize = ceiling;
    Renderer renderer{*device, shaderDir(platform), settings};
    TextureData map = skyTexture({0.2f, 0.4f, 0.8f});
    map.size = {mapWidth, mapWidth / 2};
    map.data.resize(static_cast<std::size_t>(map.expectedSize()));
    const EnvironmentHandle environment = renderer.createEnvironment(map, "sky");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
    const std::array draws{DrawItem{.mesh = sphere}};
    SceneView view = boxScene(draws);
    view.environment = environment;
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({32, 32});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    std::uint32_t count = 1;
    while (hasPass(graph, std::format("cube mip {}", count))) {
      ++count;
    }
    renderer.destroyMesh(sphere);
    renderer.destroyEnvironment(environment);
    return count;
  };
  CHECK(levels(8, 2048) == 4);   // smaller than the settings' 8: an 8 cube
  CHECK(levels(64, 2048) == 5);  // 16
  CHECK(levels(100, 2048) == 6); // 25, rounded up to 32
  CHECK(levels(256, 16) == 5);   // 64, held to 16
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("an environment is precomputed by the first scene passes after its creation", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const EnvironmentHandle environment = renderer.createEnvironment(skyTexture({0.2f, 0.4f, 0.8f}), "sky");
  REQUIRE(renderer.isValid(environment));
  REQUIRE(!renderer.isReady(environment));
  REQUIRE(countLines(*device, "uploadImage \"sky equirectangular\" level 0 layer 0 256 bytes") == 1);
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  const std::array draws{DrawItem{.mesh = sphere}};
  SceneView view = boxScene(draws);
  view.environment = environment;

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  for (int frame = 0; frame < 2; ++frame) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    if (frame == 0) {
      // An 8x8 cube has four levels: the conversion, three mips, the irradiance, two prefiltered
      // levels. The map stays alive for the background.
      REQUIRE(hasPass(graph, "equirect to cube"));
      REQUIRE(hasPass(graph, "cube mip 3"));
      REQUIRE(hasPass(graph, "irradiance"));
      REQUIRE(hasPass(graph, "prefilter 1"));
      REQUIRE(!hasPass(graph, "release equirect"));
      REQUIRE(countLines(*device, "dispatch 1 1 6") == 1 + 3 + 1 + 2);
      REQUIRE(countLines(*device, "barrier \"sky skybox\" Undefined->General") == 1);
      REQUIRE(countLines(*device, "barrier \"sky skybox\" General->General") >= 3);
      REQUIRE(renderer.isReady(environment));
      // The forward pass reads the cubes in the frame that computes them, ordered by the graph.
      REQUIRE(countLines(*device, "bindPipeline \"skybox\"") == 1);
      REQUIRE(lineIndex(*device, "dispatch") < lineIndex(*device, "bindPipeline \"skybox\""));
    } else {
      REQUIRE(!hasPass(graph, "equirect to cube"));
      REQUIRE(!hasPass(graph, "brdf lut"));
      REQUIRE(countLines(*device, "bindPipeline \"skybox\"") == 1);
    }
  }
  renderer.destroyMesh(sphere);
  renderer.destroyEnvironment(environment);
  REQUIRE(!renderer.isValid(environment));
}

TEST_CASE("a render target is recreated on resize and released at zero", "[renderer][null]") {
  const auto device = createNullDevice();
  RenderTarget target{*device, "viewport"};
  REQUIRE(!target.isValid());
  target.resize({320, 200});
  const ImageHandle first = target.color();
  REQUIRE(device->imageDesc(first).size == glm::uvec2{320, 200});
  REQUIRE(device->imageDesc(target.depth()).format == Renderer::DepthFormat);
  target.resize({320, 200});
  REQUIRE(target.color() == first); // same size, nothing recreated
  target.resize({64, 64});
  REQUIRE(!device->isValid(first));
  REQUIRE(target.size() == glm::uvec2{64, 64});
  target.resize({0, 0});
  REQUIRE(!target.isValid());
}

// The renderer does its per-frame preparation once however many passes ask for it, and it used to
// recognise "the same frame" by the view's and the graph's addresses and the graph's frame count.
// A graph built where an earlier one stood, on its first frame as the earlier one was, matched all
// three, and the renderer drew the earlier view again from the earlier frame's memory. The storage
// is reused on purpose here so the addresses do collide, rather than hoping the stack lays two
// calls out alike.
TEST_CASE("a graph built where an earlier one stood does not inherit its frame", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  std::optional<RenderGraph> graph;
  std::optional<SceneView> view;
  const auto record = [&](MeshHandle mesh) {
    const std::array draws{DrawItem{.mesh = mesh}};
    graph.emplace(*device);
    view.emplace(boxScene(draws));
    ICommandList &commands = device->beginFrame();
    graph->reset();
    renderer.addScenePasses(*graph, *view, graph->importImage(target.color()), graph->importImage(target.depth()));
    graph->execute(commands);
    device->endFrame();
    view.reset();
    graph.reset();
  };
  record(box);
  REQUIRE(countLines(*device, "bindIndexBuffer \"box indices\"") > 0);
  record(sphere);
  REQUIRE(countLines(*device, "bindIndexBuffer \"sphere indices\"") > 0);
  REQUIRE(countLines(*device, "bindIndexBuffer \"box indices\"") == 0);
  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
}

// An unobstructed receiver must not shadow itself at a split or a PCF tap.
TEST_CASE("an unoccluded plane stays lit across every shadow cascade", "[renderer][gpu][shadow]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.debugView = DebugView::ShadowFactor;
    settings.shadowMapSize = GENERATE(256u, 1024u);
    settings.farShadowMapSize = GENERATE(256u, 1024u);
    Renderer renderer{*device, shaderDir(platform), settings};
    const auto plane = renderer.createMesh(primitives::plane({400.0f, 400.0f}), "receiver");
    const std::array draws{DrawItem{.mesh = plane}};
    SceneView view;
    view.draws = draws;
    view.camera.position = {0.0f, 1.0f, 0.0f};
    view.camera.rotation = glm::angleAxis(glm::radians(-10.0f), glm::vec3{1, 0, 0});
    view.sun.direction = glm::normalize(glm::vec3{0.5f, -1.0f, -0.3f});
    GpuScene scene{*device, renderer, {256, 256}};
    scene.render(view);
    std::vector<int> shadowed;
    for (unsigned y = 0; y < 256; ++y) {
      for (unsigned x = 0; x < 256; ++x) {
        shadowed.push_back(scene.pixel(x, y).r);
      }
    }
    settings.shadows = false;
    renderer.setSettings(settings);
    scene.render(view);
    unsigned mismatches = 0;
    for (unsigned y = 0; y < 256; ++y) {
      for (unsigned x = 0; x < 256; ++x) {
        mismatches += std::abs(shadowed[y * 256 + x] - scene.pixel(x, y).r) > 1;
      }
    }
    REQUIRE(mismatches == 0);

    // Prove this camera actually exercises all four cascades, including their boundaries.
    settings.shadows = true;
    settings.debugView = DebugView::Cascade;
    renderer.setSettings(settings);
    scene.render(view);
    std::array<unsigned, 4> coverage{};
    for (unsigned y = 0; y < 256; ++y) {
      const Pixel pixel = scene.pixel(128, y);
      if (pixel.r > 200 && pixel.g < 5 && pixel.b < 5) {
        ++coverage[0];
      }
      if (pixel.g > 200 && pixel.r < 5 && pixel.b < 5) {
        ++coverage[1];
      }
      if (pixel.b > 200 && pixel.r < 5 && pixel.g < 5) {
        ++coverage[2];
      }
      if (pixel.r > 200 && pixel.g > 200 && pixel.b < 5) {
        ++coverage[3];
      }
    }
    for (unsigned rows : coverage) {
      REQUIRE(rows > 0);
    }
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

namespace {

// The mean red of a w by h block of pixels whose corner is (x, y).
float meanRed(GpuScene &scene, unsigned x, unsigned y, unsigned w, unsigned h) {
  float sum = 0.0f;
  for (unsigned j = 0; j < h; ++j) {
    for (unsigned i = 0; i < w; ++i) {
      sum += static_cast<float>(scene.pixel(x + i, y + j).r);
    }
  }
  return sum / static_cast<float>(w * h);
}

// A wall a metre left of the origin, two metres high and four wide, seen edge-on from above, with
// the sun travelling down and along +X: its shadow covers the ground from x = -1 to x = 1. The
// ground at the origin is inside the shadow, the ground at x = 2.5 is not.
struct WallShadow {
  float shadowed{0.0f};
  float lit{0.0f};
};

WallShadow wallShadow(IDevice &device, Renderer &renderer, MeshHandle plane, MeshHandle wall, MaterialHandle material,
                      float dollyOffset = 0.0f, bool shiftRange = true) {
  const glm::mat4 upright = glm::translate(glm::mat4{1.0f}, {-1.0f, 1.0f, 0.0f}) *
                            glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
  const std::array draws{DrawItem{.mesh = plane}, DrawItem{.mesh = wall, .material = material, .transform = upright}};
  SceneView view = topDownScene(draws);
  view.camera.position.y += dollyOffset;
  view.camera.fovY = 2.0f * std::atan(std::tan(view.camera.fovY * 0.5f) * 6.0f / (6.0f + dollyOffset));
  view.dollyDepthOffset = shiftRange ? dollyOffset : 0.0f;
  view.sun.direction = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});
  view.sun.intensity = 3.0f;
  view.ambient = {0.02f, 0.02f, 0.02f};
  GpuScene scene{device, renderer, {64, 64}};
  scene.render(view);
  return {.shadowed = meanRed(scene, 28, 28, 8, 8), .lit = meanRed(scene, 52, 28, 6, 8)};
}

} // namespace

TEST_CASE("a blended wall casts a shadow whose darkness follows its alpha on a GPU", "[renderer][gpu][shadow]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const MeshHandle wall = renderer.createMesh(primitives::plane({2.0f, 4.0f}), "wall");
    const auto glass = [&](float alpha) {
      MaterialDesc desc;
      desc.alphaMode = AlphaMode::Blend;
      desc.baseColor = {1.0f, 1.0f, 1.0f, alpha};
      return renderer.createMaterial(desc, "glass");
    };
    // The unshadowed ground, from a wall that casts nothing, is the lit reference.
    const MaterialHandle clear = glass(0.0f);
    const MaterialHandle quarter = glass(0.25f);
    const MaterialHandle half = glass(0.5f);
    const MaterialHandle threeQuarters = glass(0.75f);
    const MaterialHandle solid = glass(1.0f);
    const WallShadow opaque = wallShadow(*device, renderer, plane, wall, {});
    const WallShadow none = wallShadow(*device, renderer, plane, wall, clear);
    const WallShadow a25 = wallShadow(*device, renderer, plane, wall, quarter);
    const WallShadow a50 = wallShadow(*device, renderer, plane, wall, half);
    const WallShadow a75 = wallShadow(*device, renderer, plane, wall, threeQuarters);
    const WallShadow a100 = wallShadow(*device, renderer, plane, wall, solid);
    WARN(std::format("shadowed ground, alpha 0 / 0.25 / 0.5 / 0.75 / 1: {} {} {} {} {}; lit {}", none.shadowed,
                     a25.shadowed, a50.shadowed, a75.shadowed, a100.shadowed, none.lit));

    // Away from the wall's shadow nothing changes with its alpha.
    REQUIRE(none.lit > 100.0f);
    REQUIRE(std::abs(a50.lit - none.lit) < 1.0f);
    // A wall of alpha zero casts nothing, a wall of alpha one casts a full shadow, and the
    // darkness grows with alpha in between.
    REQUIRE(std::abs(none.shadowed - none.lit) < 1.0f);
    REQUIRE(a100.shadowed * 3.0f < none.lit);
    // The test never discards at alpha one: the shadow is the opaque wall's, pixel for pixel.
    REQUIRE(a100.shadowed == opaque.shadowed);
    REQUIRE(a100.lit == opaque.lit);
    REQUIRE(a25.shadowed < none.shadowed - 5.0f);
    REQUIRE(a50.shadowed < a25.shadowed - 5.0f);
    REQUIRE(a75.shadowed < a50.shadowed - 5.0f);
    REQUIRE(a100.shadowed < a75.shadowed - 5.0f);
    REQUIRE(device->validationMessageCount() == 0);

    for (const MaterialHandle material : {clear, quarter, half, threeQuarters, solid}) {
      renderer.destroyMaterial(material);
    }
    renderer.destroyMesh(wall);
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a vertex colour tints the lit surface and its alpha cuts the shadow on a GPU", "[renderer][gpu][shadow]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const auto coloured = [&](std::uint32_t color, glm::vec2 size) {
      MeshData mesh = primitives::plane(size);
      for (Vertex &vertex : mesh.vertices) {
        vertex.color = color;
      }
      return renderer.createMesh(mesh, "coloured");
    };
    // Forward: a red ground is lit red and nothing else, a white one is not.
    const MeshHandle red = coloured(0xFF0000FFu, {10.0f, 10.0f});
    const MeshHandle white = coloured(0xFFFFFFFFu, {10.0f, 10.0f});
    MaterialDesc grey;
    grey.metallic = 0.0f;
    grey.roughness = 0.6f;
    const MaterialHandle opaque = renderer.createMaterial(grey, "grey");
    const auto ground = [&](MeshHandle mesh) {
      const std::array draws{DrawItem{.mesh = mesh, .material = opaque}};
      SceneView view = topDownScene(draws);
      view.sun.direction = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});
      view.sun.intensity = 3.0f;
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      return scene.pixel(32, 32);
    };
    const Pixel tinted = ground(red);
    const Pixel plain = ground(white);
    WARN(std::format("red ground {} {} {}, white ground {} {} {}", tinted.r, tinted.g, tinted.b, plain.r, plain.g,
                     plain.b));
    // Ambient and specular light leave a little in the other channels.
    REQUIRE(plain.g > 150);
    REQUIRE(tinted.r > 150);
    REQUIRE(tinted.g * 4 < tinted.r);
    REQUIRE(tinted.b * 4 < tinted.r);

    // Depth: the vertex alpha multiplies into the alpha test and into the blended shadow's hash, so
    // a wall whose vertices are transparent casts nothing and an opaque one a full shadow.
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const auto wallOf = [&](std::uint32_t alpha, AlphaMode mode) {
      const MeshHandle wall = coloured(0x00FFFFFFu | (alpha << 24), {2.0f, 4.0f});
      MaterialDesc desc;
      desc.alphaMode = mode;
      const MaterialHandle material = renderer.createMaterial(desc, "wall");
      const WallShadow result = wallShadow(*device, renderer, plane, wall, material);
      renderer.destroyMaterial(material);
      renderer.destroyMesh(wall);
      return result;
    };
    const WallShadow masked0 = wallOf(0, AlphaMode::Mask);
    const WallShadow masked255 = wallOf(255, AlphaMode::Mask);
    const WallShadow blended0 = wallOf(0, AlphaMode::Blend);
    const WallShadow blended128 = wallOf(128, AlphaMode::Blend);
    const WallShadow blended255 = wallOf(255, AlphaMode::Blend);
    WARN(std::format("shadowed ground: masked 0 / 255: {} {}; blended 0 / 128 / 255: {} {} {}; lit {}",
                     masked0.shadowed, masked255.shadowed, blended0.shadowed, blended128.shadowed, blended255.shadowed,
                     masked0.lit));
    REQUIRE(masked0.lit > 100.0f);
    REQUIRE(std::abs(masked0.shadowed - masked0.lit) < 1.0f);
    REQUIRE(masked255.shadowed * 3.0f < masked0.lit);
    REQUIRE(std::abs(blended0.shadowed - blended0.lit) < 1.0f);
    REQUIRE(blended128.shadowed < blended0.shadowed - 5.0f);
    REQUIRE(blended255.shadowed < blended128.shadowed - 5.0f);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(opaque);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(white);
    renderer.destroyMesh(red);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a lit box renders into the viewport target on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    MaterialDesc orange;
    orange.baseColor = {1.0f, 0.5f, 0.2f, 1.0f};
    orange.metallic = 0.0f;
    orange.roughness = 0.6f;
    const MaterialHandle material = renderer.createMaterial(orange, "orange");
    const std::array draws{DrawItem{.mesh = box, .material = material}};
    const SceneView view = boxScene(draws);
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view);

    const Pixel centre = scene.pixel(32, 32);
    // The lit box face is orange-ish: red dominates, and it is not the black clear colour.
    REQUIRE(centre.r > 60);
    REQUIRE(centre.r > centre.b);
    REQUIRE(centre.a == 255);
    const Pixel corner = scene.pixel(0, 0);
    REQUIRE(corner.r + corner.g + corner.b == 0);
    REQUIRE(renderer.statistics().drawCount == 1);
    REQUIRE(renderer.statistics().shadowDrawCount == 4);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(material);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// Two views in one graph, the Scene view and the Game view of #79: the same objects through two
// cameras into two targets. A red box stands left of the origin and a green one right of it, and
// each camera looks straight at one of them. Everything the renderer keeps per frame (the orders,
// the batches, the cull jobs, the cascades, the frame constants) has to be the view's own for each
// target to show its own box (docs/decisions/0021-two-views-in-one-frame.md).
namespace {

struct TwoViewScene {
  MeshHandle box;
  MaterialHandle red;
  MaterialHandle green;
  std::array<DrawItem, 2> draws;
  std::array<SceneView, 2> views;

  explicit TwoViewScene(Renderer &renderer) {
    box = renderer.createMesh(primitives::box(), "box");
    MaterialDesc desc;
    desc.metallic = 0.0f;
    desc.roughness = 0.6f;
    desc.baseColor = {1.0f, 0.05f, 0.05f, 1.0f};
    red = renderer.createMaterial(desc, "red");
    desc.baseColor = {0.05f, 1.0f, 0.05f, 1.0f};
    green = renderer.createMaterial(desc, "green");
    draws = {
        DrawItem{.mesh = box, .material = red, .transform = glm::translate(glm::mat4{1.0f}, {-4.0f, 0.0f, 0.0f})},
        DrawItem{.mesh = box, .material = green, .transform = glm::translate(glm::mat4{1.0f}, {4.0f, 0.0f, 0.0f})}};
    for (std::size_t i = 0; i < 2; ++i) {
      views[i].draws = draws;
      views[i].camera.position = {i == 0 ? -4.0f : 4.0f, 0.0f, 3.0f};
    }
  }
  void destroy(Renderer &renderer) const {
    renderer.destroyMaterial(green);
    renderer.destroyMaterial(red);
    renderer.destroyMesh(box);
  }
};

// Declares both views into one graph, each into a target of its own, and reads both back.
struct TwoViewFrame {
  IDevice &device;
  Renderer &renderer;
  RenderGraph graph;
  std::array<RenderTarget, 2> targets;
  std::array<BufferHandle, 2> readbacks;
  glm::uvec2 size;

  TwoViewFrame(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), graph(gpu),
        targets{RenderTarget{gpu, "view a"}, RenderTarget{gpu, "view b"}}, size(targetSize) {
    for (std::size_t i = 0; i < 2; ++i) {
      targets[i].resize(size);
      readbacks[i] = device.createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                          .usage = BufferUsage::TransferDst,
                                          .memory = MemoryUsage::GpuToCpu,
                                          .debugName = "readback"});
    }
  }
  ~TwoViewFrame() {
    device.waitIdle();
    for (const BufferHandle readback : readbacks) {
      device.destroyBuffer(readback);
    }
  }
  TwoViewFrame(const TwoViewFrame &) = delete;
  TwoViewFrame &operator=(const TwoViewFrame &) = delete;

  void render(const TwoViewScene &scene, int frames = 1) {
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      std::array<GraphImage, 2> colors;
      for (std::size_t i = 0; i < 2; ++i) {
        colors[i] = graph.importImage(targets[i].color());
        const GraphImage depth = graph.importImage(targets[i].depth());
        renderer.addScenePasses(graph, scene.views[i], colors[i], depth, {0.0f, 0.0f, 0.0f, 1.0f});
      }
      if (frame == frames - 1) {
        for (std::size_t i = 0; i < 2; ++i) {
          graph.addPass(
              "readback", [&](PassBuilder &b) { b.transferSrc(colors[i]); },
              [this, i, color = colors[i]](ICommandList &cmd, const PassResources &resources) {
                cmd.copyImageToBuffer(resources.image(color), readbacks[i]);
              });
        }
      }
      graph.execute(commands);
      device.endFrame();
    }
    device.waitIdle();
  }

  Pixel pixel(std::size_t view, unsigned x, unsigned y) {
    return pixelAt(device.mappedRange(readbacks[view]), size, x, y);
  }
};

} // namespace

TEST_CASE("two views in one graph each draw their own camera's box on a GPU", "[renderer][views][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const TwoViewScene scene{renderer};
    TwoViewFrame frame{*device, renderer, {64, 64}};
    frame.render(scene, 3); // the steady state: the second frame reuses what the first sized

    const Pixel a = frame.pixel(0, 32, 32);
    const Pixel b = frame.pixel(1, 32, 32);
    CAPTURE(a.r, a.g, a.b, b.r, b.g, b.b);
    // Each camera faces one box, which fills the centre of its target and is lit its own colour.
    REQUIRE(a.r > 40);
    REQUIRE(a.r > 4 * a.g);
    REQUIRE(b.g > 40);
    REQUIRE(b.g > 4 * b.r);
    // Neither camera sees the other's box: the far corner is the clear colour in both.
    REQUIRE(frame.pixel(0, 0, 0).r + frame.pixel(0, 0, 0).g + frame.pixel(0, 0, 0).b == 0);
    REQUIRE(frame.pixel(1, 0, 0).r + frame.pixel(1, 0, 0).g + frame.pixel(1, 0, 0).b == 0);
    REQUIRE(device->validationMessageCount() == 0);
    scene.destroy(renderer);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// The first frame of an environment computes its cubes, and the first of all computes the lookup
// table, in passes the first view declares. The second view has to sample what those passes write
// rather than import the same images again as if they were finished.
TEST_CASE("two views share the first frame's precomputed lookup table and environment on a GPU",
          "[renderer][views][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    TwoViewScene scene{renderer};
    const EnvironmentHandle sky = renderer.createEnvironment(skyTexture({0.4f, 0.5f, 0.8f}), "sky");
    for (SceneView &view : scene.views) {
      view.environment = sky;
    }
    TwoViewFrame frame{*device, renderer, {64, 64}};
    frame.render(scene, 1); // the very first frame: nothing is computed yet
    for (std::size_t i = 0; i < 2; ++i) {
      const Pixel corner = frame.pixel(i, 0, 0);
      CAPTURE(i, corner.r, corner.g, corner.b);
      REQUIRE(corner.b > 40);
      REQUIRE(corner.b > corner.r);
    }
    REQUIRE(renderer.isReady(sky));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyEnvironment(sky);
    scene.destroy(renderer);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("each view of a graph keeps its own orders, batches, jobs and slice of the cull buffers",
          "[renderer][views][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget first{*device, "first"};
  RenderTarget second{*device, "second"};
  first.resize({64, 64});
  second.resize({32, 32}); // another size: the views do not share a target's
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  // One draw in the first view, three over two meshes in the second: the second needs the larger
  // slice, and the buffers grow while it is being declared, after the first was.
  const std::array firstDraws{DrawItem{.mesh = box}};
  const std::array secondDraws{DrawItem{.mesh = box}, DrawItem{.mesh = sphere}, DrawItem{.mesh = box}};
  const SceneView firstView = boxScene(firstDraws);
  const SceneView secondView = boxScene(secondDraws);
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, firstView, graph.importImage(first.color()), graph.importImage(first.depth()));
  renderer.addScenePasses(graph, secondView, graph.importImage(second.color()), graph.importImage(second.depth()));
  graph.execute(commands);
  device->endFrame();

  // Each view has its own statistics: what its passes submitted, not the last declared view's.
  REQUIRE(renderer.statistics(0).drawCount == 1);
  REQUIRE(renderer.statistics(1).drawCount == 3);
  REQUIRE(renderer.statistics(0).indirectCallCount == 1 * (Renderer::CascadeCount + Renderer::CullJobsOpaque));
  REQUIRE(renderer.statistics(1).indirectCallCount == 2 * (Renderer::CascadeCount + Renderer::CullJobsOpaque));
  REQUIRE(renderer.statistics(2).drawCount == 0); // no third view
  REQUIRE(&renderer.statistics() == &renderer.statistics(0));
  // Each view culls once against its own six frusta, and the second's clear and cull come after
  // the first view's draws in the graph, so its slice of the buffers is not the first's.
  REQUIRE(countLines(*device, "bindPipeline \"clear draw commands\"") == 2);
  REQUIRE(countLines(*device, "bindPipeline \"cull\"") == 2);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 18);
  // Nothing is blended, so neither view takes the direct path.
  REQUIRE(countLines(*device, "drawIndexed ") == 0);
  // Two views, two of everything a view owns.
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 2);
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 2);
  REQUIRE(countLines(*device, "bindPipeline \"shadow\"") == 8);
  REQUIRE(countLines(*device, "bindPipeline \"tonemap\"") == 2);

  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
}

// The vertex shader derives each normal from the model matrix rather than reading a normal matrix
// (sonnet.slang, transformNormal). What would get that wrong is a non-uniform scale, where the
// model matrix itself tilts normals the wrong way, so draw a squashed, turned sphere twice: once
// through its transform, and once with the transform baked into the vertices on the CPU through
// glm's own inverse transpose and drawn with none. The two must shade alike.
TEST_CASE("a non-uniformly scaled draw shades as its inverse transpose says on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::mat4 transform = glm::rotate(glm::mat4{1.0f}, glm::radians(35.0f), glm::normalize(glm::vec3{1, 1, 0})) *
                                glm::scale(glm::mat4{1.0f}, {2.0f, 0.5f, 1.0f});
    const MeshData sphere = primitives::sphere(0.5f, 32, 16);
    MeshData baked = sphere;
    const glm::mat3 linear{transform};
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    for (Vertex &vertex : baked.vertices) {
      vertex.position = glm::vec3{transform * glm::vec4{vertex.position, 1.0f}};
      vertex.normal = glm::normalize(normalMatrix * vertex.normal);
      vertex.tangent = glm::vec4{glm::normalize(linear * glm::vec3{vertex.tangent}), vertex.tangent.w};
    }
    const MeshHandle transformed = renderer.createMesh(sphere, "transformed");
    const MeshHandle reference = renderer.createMesh(baked, "baked");

    const auto render = [&](const DrawItem &draw) {
      const std::array draws{draw};
      SceneView view = boxScene(draws);
      view.sun.direction = glm::normalize(glm::vec3{-0.6f, -0.5f, -1.0f});
      view.sun.intensity = 3.0f;
      view.ambient = {0.03f, 0.03f, 0.03f};
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      std::vector<Pixel> pixels;
      for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
          pixels.push_back(scene.pixel(x, y));
        }
      }
      return pixels;
    };
    const std::vector<Pixel> derived = render({.mesh = transformed, .transform = transform});
    const std::vector<Pixel> expected = render({.mesh = reference});

    // Rasterising T * p on the GPU and on the CPU can disagree by a rounding at the silhouette, so
    // count the pixels that differ rather than demanding none do.
    int covered = 0;
    int differing = 0;
    for (std::size_t i = 0; i < derived.size(); ++i) {
      const Pixel &a = derived[i];
      const Pixel &b = expected[i];
      if (b.r + b.g + b.b > 0) {
        ++covered;
      }
      if (std::abs(a.r - b.r) > 3 || std::abs(a.g - b.g) > 3 || std::abs(a.b - b.b) > 3) {
        ++differing;
      }
    }
    UNSCOPED_INFO(std::format("{} of {} covered pixels differ", differing, covered));
    REQUIRE(covered > 200);
    REQUIRE(differing * 50 < covered);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMesh(reference);
    renderer.destroyMesh(transformed);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// A transform that mirrors reverses a triangle's winding on screen, so a mirrored single-sided
// draw has to be drawn with a clockwise front face or it is culled inside out (roadmap.md, "A
// mirrored single-sided draw renders inside out").
TEST_CASE("mirrored draws are batched apart and drawn with a clockwise front face", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const glm::mat4 mirror = glm::scale(glm::mat4{1.0f}, {-1.0f, 1.0f, 1.0f});
  // The mirrored draw first, so only the sort can put it after the other.
  const std::array draws{DrawItem{.mesh = box, .transform = mirror}, DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // One mesh, but two batches in each of the four cascades, the pre-pass and the forward pass, the
  // mirrored one last and preceded by the front face it needs.
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 12);
  REQUIRE(countLines(*device, "setFrontFace Clockwise") == 6);
  const auto &trace = device->trace();
  for (std::size_t i = 0; i < trace.size(); ++i) {
    if (trace[i] == "setFrontFace Clockwise") {
      REQUIRE(i > 0);
      REQUIRE(trace[i - 1].starts_with("drawIndexedIndirect"));
      REQUIRE(trace[i + 1].starts_with("drawIndexedIndirect")); // the same mesh's index buffer stays bound
    }
  }
  REQUIRE(countLines(*device, "setFrontFace CounterClockwise") == 0); // a bind resets it
  renderer.destroyMesh(box);
}

// A mirrored, turned box with a normal map, drawn twice: once through its transform, and once with
// the transform baked into the vertices on the CPU, the triangles rewound so they face out, and the
// bitangent sign flipped as a mirror flips it, drawn with none. Before the front face followed the
// transform, the first was culled inside out; before the bitangent took the mirror's sign, the
// normal map's green channel read upside down on it.
TEST_CASE("a mirrored single-sided draw renders as its baked mirror image on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::mat4 transform = glm::rotate(glm::mat4{1.0f}, glm::radians(35.0f), glm::normalize(glm::vec3{1, 1, 0})) *
                                glm::scale(glm::mat4{1.0f}, {-1.0f, 1.0f, 1.0f});
    const MeshData box = primitives::box();
    MeshData baked = box;
    const glm::mat3 linear{transform};
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    for (Vertex &vertex : baked.vertices) {
      vertex.position = glm::vec3{transform * glm::vec4{vertex.position, 1.0f}};
      vertex.normal = glm::normalize(normalMatrix * vertex.normal);
      vertex.tangent = glm::vec4{glm::normalize(linear * glm::vec3{vertex.tangent}), -vertex.tangent.w};
    }
    for (std::size_t i = 0; i + 2 < baked.indices.size(); i += 3) {
      std::swap(baked.indices[i + 1], baked.indices[i + 2]);
    }
    const MeshHandle mirrored = renderer.createMesh(box, "mirrored");
    const MeshHandle reference = renderer.createMesh(baked, "baked");
    // Tilted towards the bitangent alone, so a flipped bitangent tilts every face the other way.
    const TextureHandle normals = renderer.createTexture(solidTexture({128, 220, 180, 255}), "tilted normals");
    MaterialDesc desc;
    desc.metallic = 0.0f;
    desc.roughness = 0.7f;
    desc.normalTexture = normals;
    const MaterialHandle material = renderer.createMaterial(desc, "tilted");

    const auto render = [&](const DrawItem &draw) {
      const std::array draws{draw};
      SceneView view = boxScene(draws);
      view.sun.direction = glm::normalize(glm::vec3{-0.6f, -0.5f, -1.0f});
      view.sun.intensity = 3.0f;
      view.ambient = {0.03f, 0.03f, 0.03f};
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      std::vector<Pixel> pixels;
      for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
          pixels.push_back(scene.pixel(x, y));
        }
      }
      return pixels;
    };
    const std::vector<Pixel> drawn = render({.mesh = mirrored, .material = material, .transform = transform});
    const std::vector<Pixel> expected = render({.mesh = reference, .material = material});

    int covered = 0;
    int differing = 0;
    for (std::size_t i = 0; i < drawn.size(); ++i) {
      const Pixel &a = drawn[i];
      const Pixel &b = expected[i];
      if (b.r + b.g + b.b > 0) {
        ++covered;
      }
      if (std::abs(a.r - b.r) > 3 || std::abs(a.g - b.g) > 3 || std::abs(a.b - b.b) > 3) {
        ++differing;
      }
    }
    UNSCOPED_INFO(std::format("{} of {} covered pixels differ", differing, covered));
    REQUIRE(covered > 200);
    REQUIRE(differing * 50 < covered);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(material);
    renderer.destroyTexture(normals);
    renderer.destroyMesh(reference);
    renderer.destroyMesh(mirrored);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// A box whose every vertex follows joint 0: what a skinned draw moves as one piece.
MeshData skinnedBox() {
  MeshData box = primitives::box();
  box.skin.assign(box.vertices.size(), SkinWeights{.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}});
  return box;
}

TEST_CASE("skinned draws are deformed once per instance before the passes that draw them", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const MeshHandle skinned = renderer.createMesh(skinnedBox(), "skinned box");
  const MeshHandle plain = renderer.createMesh(primitives::box(), "plain box");
  const std::array joints{glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f})};
  // Two draws of one instance, as two submeshes would be, a plain draw, and a draw that asks for
  // skinning of a mesh without weights, which draws it as it is.
  std::vector<DrawItem> draws{DrawItem{.mesh = skinned, .firstJoint = 0, .jointCount = 1, .skinInstance = 7},
                              DrawItem{.mesh = skinned, .firstJoint = 0, .jointCount = 1, .skinInstance = 7},
                              DrawItem{.mesh = plain},
                              DrawItem{.mesh = plain, .firstJoint = 0, .jointCount = 1, .skinInstance = 8}};
  SceneView view = boxScene(draws);
  view.joints = joints;
  const auto frame = [&](bool idPass) {
    view.draws = draws;
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    const GraphImage depth = graph.importImage(target.depth());
    if (idPass) {
      renderer.addIdPass(
          graph, view, graph.createImage({.size = {32, 32}, .format = Renderer::IdFormat, .debugName = "ids"}), depth);
    } else {
      renderer.addScenePasses(graph, view, color, depth);
    }
    graph.execute(commands);
    device->endFrame();
  };

  frame(false);
  REQUIRE(hasPass(graph, "skinning"));
  REQUIRE(graph.statistics().passes[1].name == "skinning");               // after the lookup table, before the shadows
  REQUIRE(countLines(*device, "uploadBuffer \"skinned box skin\"") == 0); // uploaded before the frame
  REQUIRE(countLines(*device, "bindPipeline \"skinning\"") == 1);
  // One dispatch for the instance, 24 vertices in one group of 64, between the barriers that
  // order it after last frame's draws and before this frame's.
  const std::size_t skinning = lineIndex(*device, "bindPipeline \"skinning\"");
  REQUIRE(device->trace()[skinning - 1] == "memoryBarrier");
  REQUIRE(device->trace()[skinning + 1] == "pushConstants 64 bytes");
  REQUIRE(device->trace()[skinning + 2] == "dispatch 1 1 1");
  REQUIRE(device->trace()[skinning + 3] == "memoryBarrier");
  REQUIRE(lineIndex(*device, "bindPipeline \"skinning\"") < lineIndex(*device, "bindPipeline \"shadow\""));
  REQUIRE(renderer.statistics().skinnedInstanceCount == 1);
  REQUIRE(renderer.statistics().skinnedVertexCount == 24);
  REQUIRE(renderer.statistics().drawCount == 4);
  const std::size_t buffers = device->bufferCount();

  // The next frame reuses the instance's buffer; the id pass alone skins too.
  frame(true);
  REQUIRE(hasPass(graph, "skinning"));
  REQUIRE(graph.statistics().passes.front().name == "skinning");
  REQUIRE(device->bufferCount() == buffers);

  // An instance that stops being drawn keeps its buffer for a few frames, then releases it.
  draws.resize(3);
  draws[0].jointCount = 0;
  draws[1].jointCount = 0;
  frame(false);
  REQUIRE_FALSE(hasPass(graph, "skinning"));
  REQUIRE(device->bufferCount() == buffers);
  for (int i = 0; i < 10; ++i) {
    frame(false);
  }
  REQUIRE(device->bufferCount() == buffers - 1);

  renderer.destroyMesh(plain);
  renderer.destroyMesh(skinned);
}

TEST_CASE("a skinned box follows its joint on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(skinnedBox(), "skinned box");
    // The joint moves the box a metre and a half to the right, out of the centre of the view.
    const std::array joints{glm::translate(glm::mat4{1.0f}, glm::vec3{1.5f, 0.0f, 0.0f})};
    const std::array draws{DrawItem{.mesh = box, .firstJoint = 0, .jointCount = 1, .skinInstance = 1}};
    SceneView view = boxScene(draws);
    view.joints = joints;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2);

    // The camera is 3 m back with a 60 degree field of view: the box's centre, 1.5 m right,
    // lands near pixel 32 + 1.5 / 1.732 * 32 = 60.
    const Pixel centre = scene.pixel(32, 32);
    REQUIRE(centre.r + centre.g + centre.b == 0);
    const Pixel moved = scene.pixel(56, 32);
    REQUIRE(moved.r + moved.g + moved.b > 60);
    REQUIRE(renderer.statistics().skinnedInstanceCount == 1);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// The bind pose is twenty metres to the right, far outside the view; only the joint's matrix brings
// the box in front of the camera. Culling by the bind-pose bounds would cull it (ADR-0024).
TEST_CASE("a skinned box whose bind pose is out of view is not culled", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    MeshData data = skinnedBox();
    for (Vertex &vertex : data.vertices) {
      vertex.position.x += 20.0f;
    }
    const MeshHandle box = renderer.createMesh(data, "far skinned box");
    const std::array joints{glm::translate(glm::mat4{1.0f}, glm::vec3{-20.0f, 0.0f, 0.0f})};
    const std::array draws{DrawItem{.mesh = box, .firstJoint = 0, .jointCount = 1, .skinInstance = 1}};
    SceneView view = boxScene(draws);
    view.joints = joints;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2);
    const Pixel centre = scene.pixel(32, 32);
    REQUIRE(centre.r + centre.g + centre.b > 60);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a morphed box whose bind pose is out of view is not culled", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    MeshData data = primitives::box();
    for (Vertex &vertex : data.vertices) {
      vertex.position.x += 20.0f;
    }
    data.morphTargetCount = 1;
    data.morphDeltas.assign(data.vertices.size(), MorphDelta{.position = {-20.0f, 0.0f, 0.0f}});
    const MeshHandle box = renderer.createMesh(data, "far morphed box");
    const std::array<float, 1> weights{1.0f};
    const std::array draws{DrawItem{.mesh = box, .skinInstance = 1, .firstMorphWeight = 0, .morphWeightCount = 1}};
    SceneView view = boxScene(draws);
    view.morphWeights = weights;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2);
    const Pixel centre = scene.pixel(32, 32);
    REQUIRE(centre.r + centre.g + centre.b > 60);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// A wall in front of boxes it hides, and a camera that slides along it until the boxes peek out
// past its edge. Occlusion culling must change which draws are submitted and never a pixel
// (ADR-0024), so the same frames drawn with and without it are compared byte for byte.
TEST_CASE("occlusion culling draws the same pixels as frustum culling while the camera moves",
          "[renderer][occlusion][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    // Under TAA the depth is rasterised with a jittered matrix, and the pyramid and the tests
    // against it have to agree with it: the same pixels, frame by frame, either way.
    const AntiAliasing mode = GENERATE(AntiAliasing::None, AntiAliasing::Taa);
    RendererSettings off = testSettings();
    off.antialiasing = mode;
    RendererSettings on = off;
    on.occlusionCulling = true;
    Renderer frustumOnly{*device, shaderDir(platform), off};
    Renderer occluding{*device, shaderDir(platform), on};
    const MeshHandle boxA = frustumOnly.createMesh(primitives::box(), "box");
    const MeshHandle boxB = occluding.createMesh(primitives::box(), "box");
    const auto scene = [](MeshHandle box, std::vector<DrawItem> &draws) {
      draws.clear();
      draws.push_back(DrawItem{.mesh = box,
                               .transform = glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}),
                                                       glm::vec3{4.0f, 4.0f, 0.2f}),
                               .color = {0.8f, 0.2f, 0.2f, 1.0f}});
      for (int i = -3; i <= 3; ++i) {
        draws.push_back(
            DrawItem{.mesh = box,
                     .transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.5f * static_cast<float>(i), 0.0f, -3.0f}),
                     .color = {0.2f, 0.7f, 0.3f, 1.0f}});
      }
    };
    std::vector<DrawItem> drawsA;
    std::vector<DrawItem> drawsB;
    scene(boxA, drawsA);
    scene(boxB, drawsB);
    GpuScene sceneA{*device, frustumOnly, {64, 64}};
    GpuScene sceneB{*device, occluding, {64, 64}};
    for (int frame = 0; frame < 10; ++frame) {
      SceneView viewA = boxScene(drawsA);
      SceneView viewB = boxScene(drawsB);
      viewA.camera.position.x = viewB.camera.position.x = 0.5f * static_cast<float>(frame);
      sceneA.render(viewA, 1);
      sceneB.render(viewB, 1);
      const auto a = device->mappedRange(sceneA.readback);
      const auto b = device->mappedRange(sceneB.readback);
      REQUIRE(a.size() == b.size());
      REQUIRE(std::equal(a.begin(), a.end(), b.begin()));
      REQUIRE(device->validationMessageCount() == 0);
    }
    // A camera that stays behind the wall: the counts read back are those of a frame
    // FramesInFlight earlier, and the boxes behind the wall were not drawn, where the
    // frustum-only run drew them.
    SceneView stillA = boxScene(drawsA);
    SceneView stillB = boxScene(drawsB);
    for (int frame = 0; frame < 8; ++frame) {
      sceneA.render(stillA, 1);
      sceneB.render(stillB, 1);
    }
    REQUIRE(occluding.statistics().visibleCountsKnown);
    REQUIRE(frustumOnly.statistics().visibleCountsKnown);
    INFO("frustum " << frustumOnly.statistics().visibleDrawCount << " occlusion "
                    << occluding.statistics().visibleDrawCount);
    REQUIRE(occluding.statistics().visibleDrawCount + 2 <= frustumOnly.statistics().visibleDrawCount);
    REQUIRE(occluding.statistics().visibleTriangleCount < frustumOnly.statistics().visibleTriangleCount);
    frustumOnly.destroyMesh(boxA);
    occluding.destroyMesh(boxB);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("temporal anti-aliasing jitters the projection through eight samples", "[renderer][taa][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  const auto jitters = [&](AntiAliasing mode, DebugView debug, int frames) {
    RendererSettings settings = testSettings();
    settings.antialiasing = mode;
    settings.debugView = debug;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = box}};
    const SceneView view = boxScene(draws);
    std::vector<glm::vec2> result;
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device->beginFrame();
      graph.reset();
      renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
      result.push_back(renderer.jitter());
      graph.execute(commands);
      device->endFrame();
    }
    renderer.destroyMesh(box);
    return result;
  };
  const std::vector<glm::vec2> taa = jitters(AntiAliasing::Taa, DebugView::Final, 17);
  for (std::size_t i = 0; i < taa.size(); ++i) {
    REQUIRE(std::abs(taa[i].x) <= 0.5f);
    REQUIRE(std::abs(taa[i].y) <= 0.5f);
    if (i >= 8) {
      REQUIRE(taa[i] == taa[i - 8]);
    }
  }
  // Eight different offsets, and the first is Halton's second term: (1/4, 1/3) less the centre.
  for (std::size_t i = 0; i < 8; ++i) {
    for (std::size_t j = i + 1; j < 8; ++j) {
      REQUIRE(taa[i] != taa[j]);
    }
  }
  REQUIRE(taa[0].x == Catch::Approx(0.5f - 0.5f));
  REQUIRE(taa[0].y == Catch::Approx(1.0f / 3.0f - 0.5f));
  // Nothing else jitters: not FXAA, not no anti-aliasing, not a debug view of the unresolved frame.
  for (const auto &[mode, debug] :
       {std::pair{AntiAliasing::None, DebugView::Final}, std::pair{AntiAliasing::Fxaa, DebugView::Final},
        std::pair{AntiAliasing::Taa, DebugView::Albedo}}) {
    for (const glm::vec2 offset : jitters(mode, debug, 3)) {
      REQUIRE(offset == glm::vec2{0.0f});
    }
  }
}

// Renders frames of a scene under TAA and reads back the motion image of the last one.
struct MotionScene {
  IDevice &device;
  Renderer &renderer;
  glm::uvec2 size;
  RenderGraph graph;
  RenderTarget target;
  BufferHandle readback;

  MotionScene(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), size(targetSize), graph(gpu), target(gpu, "viewport") {
    target.resize(size);
    readback = device.createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                    .usage = BufferUsage::TransferDst,
                                    .memory = MemoryUsage::GpuToCpu,
                                    .debugName = "motion readback"});
  }
  ~MotionScene() {
    device.waitIdle();
    device.destroyBuffer(readback);
  }
  MotionScene(const MotionScene &) = delete;
  MotionScene &operator=(const MotionScene &) = delete;

  void render(const SceneView &view) {
    ICommandList &commands = device.beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    const GraphImage motion = renderer.motion();
    REQUIRE(motion.isValid());
    graph.addPass(
        "motion readback", [&](PassBuilder &b) { b.transferSrc(motion); },
        [&, motion](ICommandList &cmd, const PassResources &resources) {
          cmd.copyImageToBuffer(resources.image(motion), readback);
        });
    graph.execute(commands);
    device.endFrame();
    device.waitIdle();
  }

  glm::vec2 motionAt(unsigned x, unsigned y) {
    const auto bytes = device.mappedRange(readback);
    std::array<std::uint16_t, 2> halves{};
    std::memcpy(halves.data(), bytes.data() + (std::size_t{y} * size.x + x) * 4, sizeof(halves));
    return {glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1])};
  }
};

RendererSettings taaSettings() {
  RendererSettings settings = testSettings();
  settings.antialiasing = AntiAliasing::Taa;
  return settings;
}

TEST_CASE("a reference jitter is applied without history, whatever the anti-aliasing setting",
          "[renderer][taa][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  for (const AntiAliasing mode : {AntiAliasing::None, AntiAliasing::Taa}) {
    RendererSettings settings = testSettings();
    settings.antialiasing = mode;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = box}};
    SceneView view = boxScene(draws);
    view.referenceJitter = glm::vec2{0.25f, -0.125f};
    for (int frame = 0; frame < 3; ++frame) {
      ICommandList &commands = device->beginFrame();
      graph.reset();
      renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
      REQUIRE(renderer.jitter() == glm::vec2{0.25f, -0.125f});
      // Neither the resolve nor the motion it reads: no temporal accumulation.
      REQUIRE_FALSE(renderer.resolved().isValid());
      REQUIRE_FALSE(renderer.motion().isValid());
      REQUIRE(renderer.sceneColor().isValid());
      graph.execute(commands);
      device->endFrame();
    }
    renderer.destroyMesh(box);
  }
}

TEST_CASE("blended and transmissive draws leave the HDR scene's alpha at 0, the rest at 1", "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 16, 8), "sphere");
    MaterialDesc blended;
    blended.alphaMode = AlphaMode::Blend;
    blended.baseColor = {1.0f, 1.0f, 1.0f, 0.5f};
    MaterialDesc transmissive;
    transmissive.metallic = 0.0f; // a metal lets nothing through, and stays on the opaque path
    transmissive.transmission = 1.0f;
    const MaterialHandle blendedMaterial = renderer.createMaterial(blended, "blended");
    const MaterialHandle transmissiveMaterial = renderer.createMaterial(transmissive, "transmissive");
    // A wall behind two spheres, which land about 15 pixels either side of the middle.
    const std::array draws{DrawItem{.mesh = box,
                                    .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, -1.0f}) *
                                                 glm::scale(glm::mat4{1.0f}, {8.0f, 8.0f, 0.2f})},
                           DrawItem{.mesh = sphere,
                                    .material = blendedMaterial,
                                    .transform = glm::translate(glm::mat4{1.0f}, {-0.8f, 0.0f, 0.0f})},
                           DrawItem{.mesh = sphere,
                                    .material = transmissiveMaterial,
                                    .transform = glm::translate(glm::mat4{1.0f}, {0.8f, 0.0f, 0.0f})}};
    const SceneView view = boxScene(draws);
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    const BufferHandle readback = device->createBuffer({.size = 64ull * 64 * 8,
                                                        .usage = BufferUsage::TransferDst,
                                                        .memory = MemoryUsage::GpuToCpu,
                                                        .debugName = "hdr readback"});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    const GraphImage hdr = renderer.sceneColor();
    graph.addPass(
        "hdr readback", [&](PassBuilder &b) { b.transferSrc(hdr); },
        [&](ICommandList &cmd, const PassResources &resources) {
          cmd.copyImageToBuffer(resources.image(hdr), readback);
        });
    graph.execute(commands);
    device->endFrame();
    device->waitIdle();
    const auto alphaAt = [&](unsigned x, unsigned y) {
      std::uint16_t half = 0;
      std::memcpy(&half, device->mappedRange(readback).data() + (std::size_t{y} * 64 + x) * 8 + 6, sizeof(half));
      return glm::unpackHalf1x16(half);
    };
    REQUIRE(alphaAt(17, 32) == 0.0f); // the blended sphere
    REQUIRE(alphaAt(47, 32) == 0.0f); // the transmissive one
    REQUIRE(alphaAt(32, 32) == 1.0f); // the wall between them
    REQUIRE(alphaAt(2, 2) == 1.0f);
    device->destroyBuffer(readback);
    renderer.destroyMesh(box);
    renderer.destroyMesh(sphere);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the pre-pass writes the motion of a moving box and of a moving camera", "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), taaSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    MotionScene scene{*device, renderer, {64, 64}};
    // The camera is 3 m back with a 60 degree field of view, so the box's front face, 2.5 m away,
    // lands a unit of x at 1 / (tan 30 * 2.5) of NDC, half that in UV.
    const float uvPerMetre = 1.0f / (std::tan(glm::radians(30.0f)) * 2.5f) * 0.5f;

    // The box slid a quarter of a metre to the right since last frame.
    std::array draws{DrawItem{.mesh = box, .previousTransform = glm::translate(glm::mat4{1.0f}, {-0.25f, 0.0f, 0.0f})}};
    SceneView view = boxScene(draws);
    scene.render(view);
    const glm::vec2 moved = scene.motionAt(32, 32);
    REQUIRE(moved.x == Catch::Approx(0.25f * uvPerMetre).margin(0.004));
    REQUIRE(moved.y == Catch::Approx(0.0f).margin(0.004));
    // The background is cleared: nothing there to move.
    REQUIRE(scene.motionAt(2, 2) == glm::vec2{0.0f});

    // A box that did not move, seen by a camera that slid 0.2 m to the right, moves left on screen.
    draws[0].previousTransform.reset();
    view.camera.position.x = 0.0f;
    scene.render(view);
    REQUIRE(scene.motionAt(32, 32) == glm::vec2{0.0f});
    view.camera.position.x = 0.2f;
    scene.render(view);
    const glm::vec2 panned = scene.motionAt(32, 32);
    REQUIRE(panned.x == Catch::Approx(-0.2f * uvPerMetre).margin(0.004));
    REQUIRE(panned.y == Catch::Approx(0.0f).margin(0.004));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a skinned box's motion comes from the vertices it was deformed to the frame before",
          "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), taaSettings()};
    const MeshHandle box = renderer.createMesh(skinnedBox(), "skinned box");
    MotionScene scene{*device, renderer, {64, 64}};
    const float uvPerMetre = 1.0f / (std::tan(glm::radians(30.0f)) * 2.5f) * 0.5f;
    const std::array draws{DrawItem{.mesh = box, .firstJoint = 0, .jointCount = 1, .skinInstance = 1}};
    SceneView view = boxScene(draws);
    // The joint holds the box at the centre, then slides it 0.25 m to the right, then holds it.
    std::array<glm::mat4, 1> joints{glm::mat4{1.0f}};
    view.joints = joints;
    scene.render(view);
    REQUIRE(scene.motionAt(32, 32) == glm::vec2{0.0f}); // first sighting: no previous pose
    joints[0] = glm::translate(glm::mat4{1.0f}, glm::vec3{0.25f, 0.0f, 0.0f});
    scene.render(view);
    REQUIRE(scene.motionAt(32, 32).x == Catch::Approx(0.25f * uvPerMetre).margin(0.004));
    scene.render(view);
    REQUIRE(scene.motionAt(32, 32).x == Catch::Approx(0.0f).margin(0.004));
    joints[0] = glm::mat4{1.0f};
    scene.render(view);
    REQUIRE(scene.motionAt(32, 32).x == Catch::Approx(-0.25f * uvPerMetre).margin(0.004));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the resolve sits between the forward pass and bloom, and only under TAA", "[renderer][taa][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  const auto passes = [&](AntiAliasing mode, bool bloom) {
    RendererSettings settings = testSettings();
    settings.antialiasing = mode;
    settings.bloom = bloom;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = box}};
    const SceneView view = boxScene(draws);
    std::vector<std::string> names;
    for (int frame = 0; frame < 2; ++frame) {
      ICommandList &commands = device->beginFrame();
      graph.reset();
      renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
      graph.execute(commands);
      device->endFrame();
      names.clear();
      for (const PassTiming &pass : graph.statistics().passes) {
        names.push_back(pass.name);
      }
    }
    renderer.destroyMesh(box);
    return names;
  };
  const auto position = [](const std::vector<std::string> &names, std::string_view name) {
    return std::ranges::find(names, name) - names.begin();
  };
  const std::vector<std::string> taa = passes(AntiAliasing::Taa, true);
  REQUIRE(position(taa, "forward") < position(taa, "taa"));
  REQUIRE(position(taa, "taa") < position(taa, "bloom down 0"));
  REQUIRE(position(taa, "bloom up 0") < position(taa, "tonemap"));
  REQUIRE(position(taa, "tonemap") == static_cast<std::ptrdiff_t>(taa.size()) - 1); // no FXAA after it
  REQUIRE(std::ranges::count(taa, "taa") == 1);
  const std::vector<std::string> noBloom = passes(AntiAliasing::Taa, false);
  REQUIRE(position(noBloom, "taa") < position(noBloom, "tonemap"));
  for (const AntiAliasing mode : {AntiAliasing::None, AntiAliasing::Fxaa}) {
    REQUIRE(std::ranges::count(passes(mode, true), "taa") == 0);
  }
  REQUIRE(std::ranges::count(passes(AntiAliasing::Fxaa, true), "fxaa") == 1);
}

TEST_CASE("the history starts over on a first frame, a cut, a resize, a debug view and a skipped frame",
          "[renderer][taa][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  RenderTarget other{*device, "other"};
  other.resize({64, 64});
  Renderer renderer{*device, shaderDir(platform), taaSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  SceneView first = boxScene(draws);
  SceneView second = boxScene(draws);
  // Declares the views asked for, in one graph frame, and returns whether each reused its history.
  const auto frame = [&](std::initializer_list<SceneView *> views, RenderTarget &viewport) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    for (SceneView *view : views) {
      renderer.addScenePasses(graph, *view, graph.importImage(viewport.color()), graph.importImage(viewport.depth()));
    }
    graph.execute(commands);
    device->endFrame();
    std::string reused; // one character a view: 1 when it reused its history
    for (std::size_t i = 0; i < views.size(); ++i) {
      reused += renderer.statistics(i).temporalHistoryUsed ? '1' : '0';
    }
    return reused;
  };
  REQUIRE(frame({&first}, target) == "0");
  REQUIRE(frame({&first}, target) == "1");
  REQUIRE(frame({&first}, target) == "1");

  first.camera.projectionKind = ProjectionKind::Orthographic;
  REQUIRE(frame({&first}, target) == "0");
  REQUIRE(frame({&first}, target) == "1");
  first.camera.projectionKind = ProjectionKind::Perspective;
  REQUIRE(frame({&first}, target) == "0");
  REQUIRE(frame({&first}, target) == "1");

  // A cut: the owner says the last frame is no guide.
  first.resetHistory = true;
  REQUIRE(frame({&first}, target) == "0");
  first.resetHistory = false;
  REQUIRE(frame({&first}, target) == "1");

  // A resize recreates the images.
  target.resize({48, 48});
  REQUIRE(frame({&first}, target) == "0");
  REQUIRE(frame({&first}, target) == "1");

  // A frame that does not resolve (a debug view shows the unresolved frame) breaks the chain.
  RendererSettings settings = renderer.settings();
  settings.debugView = DebugView::Albedo;
  renderer.setSettings(settings);
  REQUIRE(frame({&first}, target) == "0");
  settings.debugView = DebugView::Final;
  renderer.setSettings(settings);
  REQUIRE(frame({&first}, target) == "0");
  REQUIRE(frame({&first}, target) == "1");

  // A view that was not drawn for a frame has no previous frame to reproject.
  REQUIRE(frame({&second}, other) == "0");
  REQUIRE(frame({&first}, target) == "0"); // second was drawn instead
  REQUIRE(frame({&first}, target) == "1");

  // Two views in one frame each keep a history of their own.
  REQUIRE(frame({&first, &second}, target) == "10");
  REQUIRE(frame({&first, &second}, target) == "11");
  REQUIRE(frame({&second}, target) == "1");
  renderer.destroyMesh(box);
}

// The whole readback of a GpuScene, as bytes.
std::vector<std::byte> imageOf(GpuScene &scene) {
  const auto bytes = scene.device.mappedRange(scene.readback);
  return {bytes.begin(), bytes.end()};
}

// The largest and the mean absolute difference between two RGBA8 images, over the colour channels.
struct ImageDifference {
  int maximum{0};
  double mean{0.0};
};

ImageDifference differenceOf(std::span<const std::byte> a, std::span<const std::byte> b) {
  REQUIRE(a.size() == b.size());
  ImageDifference result;
  std::size_t count = 0;
  double sum = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (i % 4 == 3) {
      continue;
    }
    const int difference = std::abs(std::to_integer<int>(a[i]) - std::to_integer<int>(b[i]));
    result.maximum = std::max(result.maximum, difference);
    sum += difference;
    ++count;
  }
  result.mean = sum / static_cast<double>(count);
  return result;
}

TEST_CASE("a still scene resolves to an anti-aliased image and stops changing", "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), taaSettings()};
    Renderer reference{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle referenceBox = reference.createMesh(primitives::box(), "box");
    // A box turned 30 degrees about Z, so its silhouette is diagonal and every edge pixel is a
    // fraction covered.
    const glm::mat4 turned = glm::rotate(glm::mat4{1.0f}, glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    const std::array draws{DrawItem{.mesh = box, .transform = turned}};
    const std::array referenceDraws{DrawItem{.mesh = referenceBox, .transform = turned}};
    SceneView view = boxScene(draws);
    SceneView referenceView = boxScene(referenceDraws);
    view.camera.projectionKind = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    view.camera.orthoHeight = 5.0f * std::tan(view.camera.fovY * 0.5f);
    referenceView.camera = view.camera;
    GpuScene scene{*device, renderer, {64, 64}};
    GpuScene unresolved{*device, reference, {64, 64}};
    unresolved.render(referenceView);
    const std::vector<std::byte> aliased = imageOf(unresolved);

    std::vector<std::vector<std::byte>> frames;
    for (int frame = 0; frame < 24; ++frame) {
      scene.render(view);
      frames.push_back(imageOf(scene));
      REQUIRE(device->validationMessageCount() == 0);
    }
    // Moments after the first frames the resolve is still settling; by the last ones it is not.
    for (std::size_t i = 16; i < frames.size(); ++i) {
      const ImageDifference step = differenceOf(frames[i], frames[i - 1]);
      INFO("frame " << i << " max " << step.maximum << " mean " << step.mean);
      REQUIRE(step.maximum <= 40); // an edge pixel: a tenth of the contrast, once a frame
      REQUIRE(step.mean < 0.3);
    }
    // The interior and the background are what the unresolved frame drew, within a few levels, and
    // the edge now holds values the unresolved frame did not: fractions of coverage.
    const ImageDifference against = differenceOf(frames.back(), aliased);
    INFO("against the aliased frame: max " << against.maximum << " mean " << against.mean);
    REQUIRE(against.mean < 3.0);
    REQUIRE(against.maximum > 12);
    renderer.destroyMesh(box);
    reference.destroyMesh(referenceBox);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a history that starts over averages its first samples evenly", "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), taaSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const glm::mat4 turned = glm::rotate(glm::mat4{1.0f}, glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    const std::array draws{DrawItem{.mesh = box, .transform = turned}};
    SceneView view = boxScene(draws);
    GpuScene scene{*device, renderer, {64, 64}};
    for (int frame = 0; frame < 32; ++frame) {
      scene.render(view);
    }
    const std::vector<std::byte> settled = imageOf(scene);

    // Cut the history: the first frame is a single jittered sample, and the edge pixels, which are
    // a fraction covered, differ from the settled image by up to a jitter's worth of coverage.
    view.resetHistory = true;
    scene.render(view);
    view.resetHistory = false;
    const ImageDifference single = differenceOf(imageOf(scene), settled);
    for (int frame = 0; frame < 3; ++frame) {
      scene.render(view);
    }
    // Four frames in, a fixed blend of 0.1 still weighs the first sample at 0.66 (mean 0.62 against
    // this 0.40); averaging the four evenly puts each at a quarter.
    const ImageDifference four = differenceOf(imageOf(scene), settled);
    INFO("one sample: max " << single.maximum << " mean " << single.mean << "; four: max " << four.maximum << " mean "
                            << four.mean);
    REQUIRE(single.maximum > 0);
    REQUIRE(four.mean * 100 < single.mean * 50);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a bright box crossing a dark background leaves no trail", "[renderer][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), taaSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    GpuScene scene{*device, renderer, {64, 64}};
    // The front face is 2.5 m from the camera, which sees 1.443 m either side of the axis there
    // across 32 pixels: a metre is 22 pixels.
    const auto pixelOf = [](float x) { return static_cast<unsigned>(32.0f + x / 1.443f * 32.0f); };
    const auto brightness = [](Pixel p) { return p.r + p.g + p.b; };
    std::array draws{DrawItem{.mesh = box}};
    const SceneView view = boxScene(draws);
    // A box 0.4 m across, nine pixels, so that half a metre a frame, eleven pixels, clears itself.
    const auto place = [&](float x, float previousX) {
      draws[0].transform = glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{x, 0.0f, 0.0f}), glm::vec3{0.4f});
      draws[0].previousTransform =
          glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{previousX, 0.0f, 0.0f}), glm::vec3{0.4f});
    };

    // At rest on the left for long enough that the history there is the box.
    for (int frame = 0; frame < 10; ++frame) {
      place(-1.2f, -1.2f);
      scene.render(view);
    }
    const unsigned probe = pixelOf(-1.2f);
    const int lit = brightness(scene.pixel(probe, 32));
    REQUIRE(lit > 150);

    // Then it crosses at half a metre a frame, which leaves the probe on the first step.
    float x = -1.2f;
    for (int frame = 1; frame <= 5; ++frame) {
      place(x + 0.5f, x);
      x += 0.5f;
      scene.render(view);
      const int here = brightness(scene.pixel(pixelOf(x), 32));
      const int trail = brightness(scene.pixel(probe, 32));
      INFO("frame " << frame << ": box " << here << ", probe " << trail << ", at rest " << lit);
      // The box itself keeps its brightness while it moves: the history is reprojected, not smeared.
      REQUIRE(here > lit * 85 / 100);
      // And the pixels it left go dark at once, not by a tenth a frame.
      REQUIRE(trail < lit / 20 + 6);
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("each view resolves against its own history", "[renderer][taa][views][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    // The same two cameras drawn by one renderer in one graph, and by one renderer each alone.
    const auto makeScene = [](MeshHandle box, std::vector<DrawItem> &draws) {
      draws = {
          DrawItem{.mesh = box, .transform = glm::rotate(glm::mat4{1.0f}, glm::radians(30.0f), {0.0f, 0.0f, 1.0f})}};
    };
    Renderer together{*device, shaderDir(platform), taaSettings()};
    Renderer aloneA{*device, shaderDir(platform), taaSettings()};
    Renderer aloneB{*device, shaderDir(platform), taaSettings()};
    std::vector<DrawItem> drawsT;
    std::vector<DrawItem> drawsA;
    std::vector<DrawItem> drawsB;
    const MeshHandle boxT = together.createMesh(primitives::box(), "box");
    const MeshHandle boxA = aloneA.createMesh(primitives::box(), "box");
    const MeshHandle boxB = aloneB.createMesh(primitives::box(), "box");
    makeScene(boxT, drawsT);
    makeScene(boxA, drawsA);
    makeScene(boxB, drawsB);
    const auto cameraFor = [](int frame, bool second) {
      SceneView view;
      view.camera.position = {second ? 0.3f * static_cast<float>(frame) : 0.0f, 0.0f, second ? 2.5f : 3.0f};
      return view;
    };
    GpuScene sceneA{*device, aloneA, {64, 64}};
    GpuScene sceneB{*device, aloneB, {64, 64}};
    RenderGraph graph{*device};
    RenderTarget targetFirst{*device, "first"};
    RenderTarget targetSecond{*device, "second"};
    targetFirst.resize({64, 64});
    targetSecond.resize({64, 64});
    std::array<BufferHandle, 2> readbacks;
    for (BufferHandle &readback : readbacks) {
      readback = device->createBuffer({.size = std::uint64_t{64} * 64 * 4,
                                       .usage = BufferUsage::TransferDst,
                                       .memory = MemoryUsage::GpuToCpu,
                                       .debugName = "view readback"});
    }
    SceneView first;
    SceneView second;
    for (int frame = 0; frame < 6; ++frame) {
      first = cameraFor(frame, false);
      first.draws = drawsT;
      second = cameraFor(frame, true);
      second.draws = drawsT;
      ICommandList &commands = device->beginFrame();
      graph.reset();
      const GraphImage colorFirst = graph.importImage(targetFirst.color());
      const GraphImage colorSecond = graph.importImage(targetSecond.color());
      // GpuScene clears to black, so these do too.
      together.addScenePasses(graph, first, colorFirst, graph.importImage(targetFirst.depth()),
                              {0.0f, 0.0f, 0.0f, 1.0f});
      together.addScenePasses(graph, second, colorSecond, graph.importImage(targetSecond.depth()),
                              {0.0f, 0.0f, 0.0f, 1.0f});
      // Captured by value: the passes run in execute, after this block's locals are gone.
      const std::array colors{colorFirst, colorSecond};
      for (std::size_t i = 0; i < 2; ++i) {
        graph.addPass(
            std::format("readback {}", i), [&, i](PassBuilder &b) { b.transferSrc(colors[i]); },
            [&, color = colors[i], i](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), readbacks[i]);
            });
      }
      graph.execute(commands);
      device->endFrame();
      SceneView lone = cameraFor(frame, false);
      lone.draws = drawsA;
      sceneA.render(lone);
      lone = cameraFor(frame, true);
      lone.draws = drawsB;
      sceneB.render(lone);
      device->waitIdle();
      INFO("frame " << frame);
      REQUIRE(differenceOf(device->mappedRange(readbacks[0]), imageOf(sceneA)).maximum == 0);
      REQUIRE(differenceOf(device->mappedRange(readbacks[1]), imageOf(sceneB)).maximum == 0);
    }
    REQUIRE(device->validationMessageCount() == 0);
    for (const BufferHandle readback : readbacks) {
      device->destroyBuffer(readback);
    }
    together.destroyMesh(boxT);
    aloneA.destroyMesh(boxA);
    aloneB.destroyMesh(boxB);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("occlusion culling adds the pyramid and the second phase to the depth passes",
          "[renderer][occlusion][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RendererSettings settings = testSettings();
  settings.occlusionCulling = true;
  Renderer renderer{*device, shaderDir(platform), settings};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  std::vector<std::string> names;
  for (const PassTiming &pass : graph.statistics().passes) {
    names.push_back(pass.name);
  }
  const auto position = [&](std::string_view name) { return std::ranges::find(names, name) - names.begin(); };
  REQUIRE(hasPass(graph, "depth pyramid"));
  REQUIRE(position("depth") < position("depth pyramid"));
  REQUIRE(position("depth pyramid") < position("occlusion cull"));
  REQUIRE(position("occlusion cull") < position("depth late"));
  REQUIRE(position("depth late") < position("forward"));
  renderer.destroyMesh(box);
}

// A box with one morph target that slides every vertex 1.5 m to the right at weight 1.
MeshData morphedBox() {
  MeshData box = primitives::box();
  box.morphTargetCount = 1;
  box.morphDeltas.assign(box.vertices.size(), MorphDelta{.position = {1.5f, 0.0f, 0.0f}});
  return box;
}

TEST_CASE("morphed draws are deformed in the skinning pass, with or without a skin", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const MeshHandle morphed = renderer.createMesh(morphedBox(), "morphed box");
  const MeshHandle plain = renderer.createMesh(primitives::box(), "plain box");
  const std::array<float, 2> weights{0.0f, 1.0f};
  // A morphed instance; a plain mesh asking for morphing, which draws as it is; and a morphed
  // draw with a weight range past the view's, which does too.
  std::vector<DrawItem> draws{
      DrawItem{.mesh = morphed, .skinInstance = 3, .firstMorphWeight = 1, .morphWeightCount = 1},
      DrawItem{.mesh = plain, .skinInstance = 4, .firstMorphWeight = 0, .morphWeightCount = 1},
      DrawItem{.mesh = morphed, .skinInstance = 5, .firstMorphWeight = 2, .morphWeightCount = 1}};
  SceneView view = boxScene(draws);
  view.morphWeights = weights;
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  REQUIRE(hasPass(graph, "skinning"));
  REQUIRE(countLines(*device, "bindPipeline \"skinning\"") == 1);
  REQUIRE(countLines(*device, "dispatch 1 1 1") >= 1);
  REQUIRE(renderer.statistics().skinnedInstanceCount == 1); // only the first draw is deformed
  REQUIRE(renderer.statistics().skinnedVertexCount == 24);
  renderer.destroyMesh(plain);
  renderer.destroyMesh(morphed);
}

TEST_CASE("a morphed box follows its weight on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(morphedBox(), "morphed box");
    const std::array<float, 1> weights{1.0f};
    const std::array draws{DrawItem{.mesh = box, .skinInstance = 1, .firstMorphWeight = 0, .morphWeightCount = 1}};
    SceneView view = boxScene(draws);
    view.morphWeights = weights;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2);

    // As the skinned box: its centre moved out of the middle of the view to near pixel 60.
    const Pixel centre = scene.pixel(32, 32);
    REQUIRE(centre.r + centre.g + centre.b == 0);
    const Pixel moved = scene.pixel(56, 32);
    REQUIRE(moved.r + moved.g + moved.b > 60);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// Bright white additive discs a metre across at the origin, born in place and staying there.
ParticleEmitterItem glowEmitter() {
  return {.key = 5,
          .maxParticles = 1000,
          .rate = 100.0f,
          .lifetime = {10.0f, 10.0f},
          .speed = {0.0f, 0.0f},
          .gravity = {0.0f, 0.0f, 0.0f},
          .sizeStart = 1.0f,
          .sizeEnd = 1.0f,
          .colorStart = {1.0f, 1.0f, 1.0f, 1.0f},
          .colorEnd = {1.0f, 1.0f, 1.0f, 1.0f},
          .blend = ParticleBlend::Additive};
}

TEST_CASE("an emitter is simulated once a frame and drawn with one indirect call", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const std::array emitters{glowEmitter()};
  SceneView view = boxScene({});
  view.particles = emitters;
  view.deltaTime = 0.1f;
  const auto frame = [&] {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
  };
  const std::size_t buffers = device->bufferCount();
  frame();
  REQUIRE(hasPass(graph, "particles"));
  REQUIRE(countLines(*device, "bindPipeline \"particle reset\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"particle simulation\"") == 1);
  REQUIRE(countLines(*device, "dispatch 16 1 1") == 1); // 1000 slots in groups of 64
  REQUIRE(countLines(*device, "bindPipeline \"particles additive\"") == 1);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"particles command\" count 1") == 1);
  REQUIRE(device->bufferCount() == buffers + 3); // the ring, the alive list and the command
  REQUIRE(renderer.statistics().particleEmitterCount == 1);
  REQUIRE(renderer.statistics().particleSlotCount == 1000);
  REQUIRE(lineIndex(*device, "bindPipeline \"particle simulation\"") <
          lineIndex(*device, "bindPipeline \"particles additive\""));

  // The same emitter next frame reuses its buffers; one that stops being drawn gives them back
  // after a few frames.
  frame();
  REQUIRE(device->bufferCount() == buffers + 3);
  view.particles = {};
  for (int i = 0; i < 12; ++i) {
    frame();
  }
  REQUIRE(device->bufferCount() == buffers);
}

TEST_CASE("a second view of the same frame draws an emitter without advancing it again", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget first{*device, "first"};
  RenderTarget second{*device, "second"};
  first.resize({32, 32});
  second.resize({32, 32});
  const std::array emitters{glowEmitter()};
  SceneView view = boxScene({});
  view.particles = emitters;
  view.deltaTime = 0.1f;
  SceneView other = view;
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(first.color()), graph.importImage(first.depth()));
  renderer.addScenePasses(graph, other, graph.importImage(second.color()), graph.importImage(second.depth()));
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "bindPipeline \"particle simulation\"") == 1);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"particles command\" count 1") == 2);
}

TEST_CASE("particles glow where they were born and repeat from run to run on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    std::array<std::vector<Pixel>, 2> runs;
    for (std::vector<Pixel> &run : runs) {
      Renderer renderer{*device, shaderDir(platform), testSettings()};
      // A jet with a random cone and speed, so repeating means the draws matched too.
      ParticleEmitterItem jet = glowEmitter();
      jet.speed = {0.2f, 1.0f};
      jet.lifetime = {0.5f, 2.0f};
      jet.coneAngle = 1.0f;
      jet.sizeStart = 0.3f;
      jet.sizeEnd = 0.1f;
      const std::array emitters{jet};
      SceneView view = boxScene({});
      view.particles = emitters;
      view.deltaTime = 0.1f;
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view, 8);
      for (unsigned y = 0; y < 64; y += 3) {
        for (unsigned x = 0; x < 64; x += 3) {
          run.push_back(scene.pixel(x, y));
        }
      }
      REQUIRE(device->validationMessageCount() == 0);
    }
    REQUIRE(std::ranges::equal(runs[0], runs[1],
                               [](const Pixel &a, const Pixel &b) { return a.r == b.r && a.g == b.g && a.b == b.b; }));
    const bool lit = std::ranges::any_of(runs[0], [](const Pixel &p) { return p.r + p.g + p.b > 60; });
    REQUIRE(lit);

    // Born in place and staying there, a stack of soft discs makes the centre bright and the
    // corner dark.
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const std::array emitters{glowEmitter()};
    SceneView view = boxScene({});
    view.particles = emitters;
    view.deltaTime = 0.1f;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 4);
    const Pixel centre = scene.pixel(32, 32);
    const Pixel corner = scene.pixel(2, 2);
    REQUIRE(centre.r + centre.g + centre.b > 300);
    REQUIRE(corner.r + corner.g + corner.b == 0);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the renderer destroys every pipeline it creates", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    REQUIRE(device->pipelineCount() > 0);
  }
  REQUIRE(device->pipelineCount() == 0);
}

TEST_CASE("the present pass copies the scene into a target of another format", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RendererSettings settings = testSettings();
  // What a swapchain usually hands out, which is not the renderer's own colour format.
  settings.presentFormat = Format::B8G8R8A8Unorm;
  Renderer renderer{*device, shaderDir(platform), settings};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const ImageHandle backbuffer = device->createImage(
      {.size = {32, 32}, .format = Format::B8G8R8A8Unorm, .usage = ImageUsage::None, .debugName = "backbuffer"});

  graph.reset();
  renderer.addPresentPass(graph, graph.importImage(target.color()), graph.importImage(backbuffer));
  ICommandList &commands = device->beginFrame();
  graph.execute(commands);
  device->endFrame();

  REQUIRE(hasPass(graph, "present"));
  REQUIRE(countLines(*device, "bindPipeline") >= 1);
  REQUIRE(countLines(*device, "draw 3 x1") == 1); // the full-screen triangle
  device->destroyImage(backbuffer);
}

TEST_CASE("without a present format there is no present pass", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});

  graph.reset();
  renderer.addPresentPass(graph, graph.importImage(target.color()), graph.importImage(target.color()));
  ICommandList &commands = device->beginFrame();
  graph.execute(commands);
  device->endFrame();
  REQUIRE(!hasPass(graph, "present"));
}

TEST_CASE("debug lines draw over the scene where nothing hides them", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const std::array lines{DebugLine{.from = {0.0f, 0.0f, 0.0f}, .to = {1.0f, 0.0f, 0.0f}, .color = {1, 0, 0, 1}},
                         DebugLine{.from = {0.0f, 0.0f, 0.0f}, .to = {0.0f, 1.0f, 0.0f}, .color = {0, 1, 0, 1}}};
  SceneView view = boxScene({});
  const auto frame = [&] {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    const GraphImage depth = graph.importImage(target.depth());
    renderer.addScenePasses(graph, view, color, depth);
    renderer.addDebugLinePass(graph, view, color, depth);
    graph.execute(commands);
    device->endFrame();
  };

  frame();
  REQUIRE_FALSE(hasPass(graph, "debug lines"));

  view.debugLines = lines;
  frame();
  REQUIRE(hasPass(graph, "debug lines"));
  REQUIRE(countLines(*device, "bindPipeline \"debug lines\"") == 1);
  REQUIRE(countLines(*device, "draw 4 x1") == 1);
  REQUIRE(graph.statistics().passes.back().name == "debug lines");
}

TEST_CASE("debug lines are depth-tested against the scene on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = box}};
    // The camera is 3 m back with a 60 degree field of view over 64 pixels: 5 m away a pixel is
    // 2.887 / 32 m high, 2 m away 1.155 / 32 m. Each line sits on a pixel row's centre, row 31
    // behind the box and row 40 in front of it.
    const std::array lines{
        DebugLine{.from = {-2.0f, 0.045f, -2.0f}, .to = {2.0f, 0.045f, -2.0f}, .color = {0.0f, 1.0f, 0.0f, 1.0f}},
        DebugLine{.from = {-0.6f, -0.3067f, 1.0f}, .to = {0.6f, -0.3067f, 1.0f}, .color = {1.0f, 0.0f, 0.0f, 1.0f}}};
    SceneView view = boxScene(draws);
    view.debugLines = lines;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view);

    const auto near = [&](unsigned x, unsigned y, auto predicate) {
      return predicate(scene.pixel(x, y - 1)) || predicate(scene.pixel(x, y)) || predicate(scene.pixel(x, y + 1));
    };
    const auto green = [](Pixel p) { return p.g > 200 && p.r < 60 && p.b < 60; };
    const auto red = [](Pixel p) { return p.r > 200 && p.g < 60 && p.b < 60; };
    // Beside the box the far line shows, behind it the box hides it.
    REQUIRE(near(14, 31, green));
    REQUIRE_FALSE(near(32, 31, green));
    // The near line crosses in front of the box.
    REQUIRE(near(32, 40, red));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("overlay quads are one draw after the scene, and none add no pass", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const std::array quads{OverlayQuad{.min = {0.0f, 0.0f}, .max = {8.0f, 8.0f}},
                         OverlayQuad{.min = {8.0f, 8.0f}, .max = {16.0f, 16.0f}, .color = {1.0f, 0.0f, 0.0f, 0.5f}}};
  const auto frame = [&](std::span<const OverlayQuad> overlay) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    const GraphImage color = graph.importImage(target.color());
    renderer.addOverlayPass(graph, color, overlay);
    graph.execute(commands);
    device->endFrame();
  };

  frame({});
  REQUIRE_FALSE(hasPass(graph, "overlay"));
  frame(quads);
  REQUIRE(hasPass(graph, "overlay"));
  REQUIRE(countLines(*device, "bindPipeline \"overlay\"") == 1);
  REQUIRE(countLines(*device, "draw 12 x1") == 1); // six vertices a quad
}

TEST_CASE("overlay quads are placed from the top-left, blended and textured on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    // Two texels side by side, red and blue, sRGB like a UI image is imported.
    TextureData data;
    data.size = {2, 1};
    data.data = {std::byte{255}, std::byte{0}, std::byte{0},   std::byte{255},
                 std::byte{0},   std::byte{0}, std::byte{255}, std::byte{255}};
    const TextureHandle texture = renderer.createTexture(data, "two texels");
    REQUIRE(renderer.textureSize(texture) == glm::uvec2{2, 1});
    REQUIRE(renderer.textureSize(TextureHandle{}) == glm::uvec2{0, 0});

    const std::array quads{// Opaque red in the top-left corner.
                           OverlayQuad{.min = {0.0f, 0.0f}, .max = {16.0f, 16.0f}, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
                           // Half-transparent green over the black scene, lower right.
                           OverlayQuad{.min = {32.0f, 32.0f}, .max = {48.0f, 48.0f}, .color = {0.0f, 1.0f, 0.0f, 0.5f}},
                           // The texture across the lower left: red to the left, blue to the right.
                           OverlayQuad{.min = {0.0f, 48.0f}, .max = {32.0f, 56.0f}, .texture = texture}};
    GpuScene scene{*device, renderer, {64, 64}};
    scene.overlay = quads;
    scene.render(boxScene({}));

    const Pixel corner = scene.pixel(8, 8);
    REQUIRE((corner.r > 250 && corner.g < 5 && corner.b < 5));
    REQUIRE(scene.pixel(40, 8).r < 5); // nothing top-right: y runs down
    const Pixel half = scene.pixel(40, 40);
    REQUIRE((half.g > 120 && half.g < 136 && half.r < 8));
    const Pixel left = scene.pixel(2, 52);
    const Pixel right = scene.pixel(29, 52);
    REQUIRE((left.r > 240 && left.b < 15));
    REQUIRE((right.b > 240 && right.r < 15));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyTexture(texture);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("glyph and image quads in order are still one overlay draw", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  // The kind is per quad and the shader branches on it, so images and glyphs interleave in the
  // order given without a draw or a pipeline switch between them.
  const std::array quads{OverlayQuad{.min = {0.0f, 0.0f}, .max = {8.0f, 8.0f}},
                         OverlayQuad{.min = {8.0f, 8.0f}, .max = {16.0f, 16.0f}, .kind = OverlayKind::Glyph},
                         OverlayQuad{.min = {16.0f, 16.0f}, .max = {24.0f, 24.0f}},
                         OverlayQuad{.min = {0.0f, 24.0f}, .max = {8.0f, 32.0f}, .kind = OverlayKind::Glyph}};
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addOverlayPass(graph, graph.importImage(target.color()), quads);
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "bindPipeline \"overlay\"") == 1);
  REQUIRE(countLines(*device, "draw 24 x1") == 1);
}

TEST_CASE("a glyph quad draws coverage from an R8 distance field, smooth at two scales, in the colour given",
          "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    // A disc of radius 20.5 texels in a 64 by 64 field: 128 on the outline, falling 128/6 a texel
    // to zero six texels outside it, as the gui module's glyphs are.
    TextureData field;
    field.size = {64, 64};
    field.format = Format::R8Unorm;
    field.data.resize(std::size_t{64} * 64);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        const float distance = std::hypot(static_cast<float>(x) + 0.5f - 32.0f, static_cast<float>(y) + 0.5f - 32.0f);
        const float value = std::clamp(128.0f + (20.5f - distance) * (128.0f / 6.0f), 0.0f, 255.0f);
        field.data[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)] =
            static_cast<std::byte>(static_cast<unsigned char>(value));
      }
    }
    const TextureHandle texture = renderer.createTexture(field, "disc field");
    REQUIRE(renderer.isValid(texture));

    // The field at 1:1 on the left, and at half size on the right, in two colours. Mid-grey would
    // read 188 if the glyph path encoded it as an image's texel is: it is blended as it is.
    const std::array quads{OverlayQuad{.min = {0.0f, 0.0f},
                                       .max = {64.0f, 64.0f},
                                       .color = {0.5f, 0.5f, 0.5f, 1.0f},
                                       .texture = texture,
                                       .kind = OverlayKind::Glyph},
                           OverlayQuad{.min = {64.0f, 0.0f},
                                       .max = {96.0f, 32.0f},
                                       .color = {1.0f, 0.0f, 0.0f, 1.0f},
                                       .texture = texture,
                                       .kind = OverlayKind::Glyph}};
    GpuScene scene{*device, renderer, {128, 64}};
    scene.overlay = quads;
    scene.render(boxScene({}));

    const Pixel inside = scene.pixel(32, 32);
    REQUIRE((inside.r > 122 && inside.r < 134));
    REQUIRE((inside.g > 122 && inside.g < 134));
    REQUIRE(scene.pixel(1, 1).r < 3); // outside the disc, past the field's fall-off: no coverage
    // Pixel (52, 32) is centred on the outline: about half coverage, not 0 or 255.
    const Pixel edge = scene.pixel(52, 32);
    REQUIRE((edge.r > 50 && edge.r < 80));

    const Pixel small = scene.pixel(80, 16); // the centre at half size
    REQUIRE((small.r > 250 && small.g < 5 && small.b < 5));
    REQUIRE(scene.pixel(65, 1).r < 5);
    // At half size the outline is at 10.25 pixels from the centre: pixel 90 is a quarter outside.
    const Pixel smallEdge = scene.pixel(90, 16);
    REQUIRE((smallEdge.r > 20 && smallEdge.r < 235));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyTexture(texture);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("culling keeps what the frustum holds and drops the rest on a GPU", "[renderer][culling][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    constexpr glm::uvec2 size{64, 64};
    // A ground plane far wider than the view, so it straddles every frustum plane and every
    // cascade's: the case where a wrong plane extraction would cull what fills the screen.
    const MeshHandle plane = renderer.createMesh(primitives::plane({40.0f, 40.0f}), "ground");

    std::vector<DrawItem> visible{
        DrawItem{.mesh = plane, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, -1.0f, 0.0f})},
        DrawItem{.mesh = box, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
    };
    // The same scene plus boxes far behind the camera and far off to either side. None of them
    // can reach a pixel, so culling them must leave the image untouched. They share the red box's
    // batch, so its one instance has to find the red box among them in the visible list.
    std::vector<DrawItem> withOutsiders = visible;
    for (int i = 0; i < 64; ++i) {
      const float offset = 60.0f + static_cast<float>(i);
      for (const glm::vec3 place :
           {glm::vec3{offset, 0.0f, 0.0f}, glm::vec3{-offset, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, offset}}) {
        withOutsiders.push_back(DrawItem{
            .mesh = box, .transform = glm::translate(glm::mat4{1.0f}, place), .color = {0.0f, 1.0f, 0.0f, 1.0f}});
      }
    }

    const auto shoot = [&](std::span<const DrawItem> draws) {
      GpuScene scene{*device, renderer, size};
      scene.render(boxScene(draws), 2);
      std::vector<Pixel> image;
      image.reserve(std::size_t{size.x} * size.y);
      for (unsigned y = 0; y < size.y; ++y) {
        for (unsigned x = 0; x < size.x; ++x) {
          image.push_back(scene.pixel(x, y));
        }
      }
      return image;
    };
    const std::vector<Pixel> alone = shoot(visible);
    const std::vector<Pixel> crowded = shoot(withOutsiders);

    // The box is lit and red at the centre, and the ground fills the bottom of the screen: both
    // survived culling, in the forward pass and in the cascades that shadow them.
    const Pixel centre = alone[std::size_t{size.y / 2} * size.x + size.x / 2];
    REQUIRE(centre.r > centre.g);
    REQUIRE(centre.r > 40);
    const Pixel ground = alone[std::size_t{size.y - 4} * size.x + size.x / 2];
    REQUIRE(ground.r + ground.g + ground.b > 30); // not the clear colour
    // And what the frustum does not hold changes nothing at all.
    REQUIRE(crowded.size() == alone.size());
    for (std::size_t i = 0; i < alone.size(); ++i) {
      REQUIRE(crowded[i].r == alone[i].r);
      REQUIRE(crowded[i].g == alone[i].g);
      REQUIRE(crowded[i].b == alone[i].b);
    }

    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// Two boxes in one mesh, each its own submesh, so two batches over two index ranges; the left
// one drawn twice, so its batch has two instances that must each find their own object. A batch
// that ignored its submesh would draw the left box where the right one belongs, and an instance
// that read the wrong slot of the visible list would draw the wrong colour (ADR-0016).
TEST_CASE("each batch draws its submesh and each instance its own object on a GPU", "[renderer][culling][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    MeshData pair;
    for (const float x : {-0.6f, 0.6f}) {
      const MeshData half = primitives::box({0.25f, 0.25f, 0.25f});
      const auto base = static_cast<std::uint32_t>(pair.vertices.size());
      pair.submeshes.push_back(Submesh{.firstIndex = static_cast<std::uint32_t>(pair.indices.size()),
                                       .indexCount = static_cast<std::uint32_t>(half.indices.size())});
      for (Vertex vertex : half.vertices) {
        vertex.position.x += x;
        pair.vertices.push_back(vertex);
      }
      for (const std::uint32_t index : half.indices) {
        pair.indices.push_back(base + index);
      }
    }
    const MeshHandle mesh = renderer.createMesh(pair, "pair");
    const std::array draws{
        DrawItem{.mesh = mesh, .submesh = 1, .color = {0.0f, 0.0f, 1.0f, 1.0f}},
        DrawItem{.mesh = mesh, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
        DrawItem{.mesh = mesh,
                 .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.8f, 0.0f}),
                 .color = {0.0f, 1.0f, 0.0f, 1.0f}},
    };
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(boxScene(draws), 2);

    const Pixel left = scene.pixel(20, 32);
    REQUIRE(left.r > 40);
    REQUIRE(left.r > left.g + left.b);
    const Pixel above = scene.pixel(20, 16);
    REQUIRE(above.g > 40);
    REQUIRE(above.g > above.r + above.b);
    const Pixel right = scene.pixel(44, 32);
    REQUIRE(right.b > 40);
    REQUIRE(right.b > right.r + right.g);
    const Pixel between = scene.pixel(32, 32);
    REQUIRE(between.r + between.g + between.b == 0);
    REQUIRE(renderer.statistics().drawCount == 3);
    REQUIRE(renderer.statistics().indirectCallCount == 2 * (Renderer::CascadeCount + Renderer::CullJobsOpaque));

    renderer.destroyMesh(mesh);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the sun's shadow darkens the ground beside a box on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const std::array draws{DrawItem{.mesh = plane},
                           DrawItem{.mesh = box, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f})}};
    SceneView view = topDownScene(draws);
    view.camera.projectionKind = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    view.camera.orthoHeight = 12.0f * std::tan(view.camera.fovY * 0.5f);
    // Light travelling down and along +X: the box shadows the ground on its +X side.
    view.sun.direction = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});
    view.sun.intensity = 3.0f;
    view.ambient = {0.02f, 0.02f, 0.02f};
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view);

    // Six metres up with a 60 degree field of view, one metre is about nine pixels.
    const Pixel shadowed = scene.pixel(32 + 9, 32);
    const Pixel lit = scene.pixel(32 - 9, 32);
    REQUIRE(lit.r > 100);
    REQUIRE(shadowed.r * 3 < lit.r);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

namespace {

Light shadowedSpot(glm::vec3 position, bool castsShadows = true) {
  return Light{.type = LightType::Spot,
               .position = position,
               .range = 20.0f,
               .direction = {0.0f, -1.0f, 0.0f},
               .outerAngle = glm::radians(40.0f),
               .castsShadows = castsShadows};
}

} // namespace

TEST_CASE("spot lights asking for shadows get maps up to the budget, the same ones on every run",
          "[renderer][shadow][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  auto settings = testSettings();
  settings.maxLocalShadows = 3;
  Renderer renderer{*device, shaderDir(platform), settings};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  // Lights 1 and 2 do not ask: neither is a candidate. Lights 0, 3, 4 fill the budget of three;
  // 5 and 6 ask too late and light without a shadow.
  Light point = shadowedSpot({1.0f, 4.0f, 0.0f}, false);
  point.type = LightType::Point;
  const std::array lights{shadowedSpot({0.0f, 4.0f, 0.0f}),        point,
                          shadowedSpot({2.0f, 4.0f, 0.0f}, false), shadowedSpot({3.0f, 4.0f, 0.0f}),
                          shadowedSpot({4.0f, 4.0f, 0.0f}),        shadowedSpot({5.0f, 4.0f, 0.0f}),
                          shadowedSpot({6.0f, 4.0f, 0.0f})};
  SceneView view = boxScene(draws);
  view.lights = lights;
  REQUIRE(renderer.localShadowBudget() == 3);

  for (int run = 0; run < 2; ++run) {
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();

    REQUIRE(hasPass(graph, "shadow spot 0"));
    REQUIRE(hasPass(graph, "shadow spot 3"));
    REQUIRE(hasPass(graph, "shadow spot 4"));
    REQUIRE(!hasPass(graph, "shadow spot 5"));
    REQUIRE(renderer.statistics().localShadowCount == 3);
    REQUIRE(renderer.statistics().localShadowMapCount == 3);
    const auto shadows = renderer.localShadows();
    REQUIRE(shadows.size() == 3);
    REQUIRE(shadows[0].light == 0);
    REQUIRE(shadows[1].light == 3);
    REQUIRE(shadows[2].light == 4);
    REQUIRE(renderer.statistics().lightCount == 7); // every light still illuminates
    // Four cascades, the depth pre-pass and the forward pass, and the three maps: one command
    // each for the single box, and the maps' draws count with the cascades'.
    REQUIRE(renderer.statistics().indirectCallCount == 4 + 2 + 3);
    REQUIRE(renderer.statistics().shadowDrawCount == 4 + 3);
  }

  settings.maxLocalShadows = 1000;
  renderer.setSettings(settings);
  REQUIRE(renderer.localShadowBudget() == Renderer::MaxLocalShadows);
  settings.shadows = false;
  renderer.setSettings(settings);
  REQUIRE(renderer.localShadowBudget() == 0);
  renderer.destroyMesh(box);
}

TEST_CASE("blended draws are shadow candidates but stay out of the depth pre-pass", "[renderer][shadow][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  MaterialDesc glass;
  glass.alphaMode = AlphaMode::Blend;
  glass.baseColor = {1.0f, 1.0f, 1.0f, 0.5f};
  const MaterialHandle glassMaterial = renderer.createMaterial(glass, "glass");
  // A scene of nothing but glass, with a spot light asking for a map: it casts shadows all the same.
  const std::array draws{DrawItem{.mesh = box, .material = glassMaterial}};
  const std::array lights{shadowedSpot({0.0f, 4.0f, 0.0f})};
  SceneView view = boxScene(draws);
  view.lights = lights;

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  REQUIRE(hasPass(graph, "shadow cascade 0"));
  REQUIRE(hasPass(graph, "shadow cascade 3"));
  REQUIRE(hasPass(graph, "shadow spot 0"));
  // One command for the box in each of the four cascades and the spot light's map; the pre-pass and
  // the forward pass have no opaque draw, and the glass is drawn directly after them.
  REQUIRE(renderer.statistics().indirectCallCount == 4 + 1);
  REQUIRE(renderer.statistics().shadowDrawCount == 4 + 1);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 4 + 1);
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 0);
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 1);

  renderer.destroyMaterial(glassMaterial);
  renderer.destroyMesh(box);
}

namespace {

Light shadowedPoint(glm::vec3 position, bool castsShadows = true) {
  return Light{.type = LightType::Point, .position = position, .range = 20.0f, .castsShadows = castsShadows};
}

} // namespace

TEST_CASE("point lights take six maps each from the same budget as spot lights", "[renderer][shadow][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  auto settings = testSettings();
  settings.maxLocalShadows = 3;
  Renderer renderer{*device, shaderDir(platform), settings};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  // Lights 0, 1 and 2 fill the budget of three whatever their type, 13 maps in all; 3 asks too
  // late and 4 does not ask.
  const std::array lights{shadowedPoint({0.0f, 4.0f, 0.0f}), shadowedSpot({1.0f, 4.0f, 0.0f}),
                          shadowedPoint({2.0f, 4.0f, 0.0f}), shadowedPoint({3.0f, 4.0f, 0.0f}),
                          shadowedPoint({4.0f, 4.0f, 0.0f}, false)};
  SceneView view = boxScene(draws);
  view.lights = lights;

  for (int run = 0; run < 2; ++run) {
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();

    REQUIRE(renderer.statistics().localShadowCount == 3);
    REQUIRE(renderer.statistics().localShadowMapCount == 13);
    const auto shadows = renderer.localShadows();
    REQUIRE(shadows.size() == 13);
    for (std::uint32_t face = 0; face < Renderer::PointShadowFaces; ++face) {
      REQUIRE(shadows[face].light == 0);
      REQUIRE(shadows[face].face == face);
      REQUIRE(hasPass(graph, std::format("shadow point 0 face {}", face)));
      REQUIRE(hasPass(graph, std::format("shadow point 2 face {}", face)));
      REQUIRE(shadows[7 + face].light == 2);
    }
    REQUIRE(shadows[6].light == 1);
    REQUIRE(hasPass(graph, "shadow spot 1"));
    REQUIRE(!hasPass(graph, "shadow point 3 face 0"));
    REQUIRE(!hasPass(graph, "shadow point 4 face 0"));
    // Each face is a cull job and a command of its own, next to the cascades, the depth pre-pass
    // and the forward pass.
    REQUIRE(renderer.statistics().indirectCallCount == 4 + 2 + 13);
    REQUIRE(renderer.statistics().shadowDrawCount == 4 + 13);
    REQUIRE(renderer.statistics().lightCount == 5);
  }

  // Turning shadows off, or asking for none, leaves no maps.
  settings.maxLocalShadows = 0;
  renderer.setSettings(settings);
  {
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    REQUIRE(renderer.localShadows().empty());
    REQUIRE(renderer.statistics().localShadowMapCount == 0);
  }
  renderer.destroyMesh(box);
}

TEST_CASE("three views with three spot and two point shadows each fit the timestamp and barrier budgets",
          "[renderer][shadow][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  // The lighting showcase's lights: 3 + 2 * 6 = 15 maps a view. The editor's two views with its
  // extra passes recorded 75 passes, over the 64 a frame had timestamps for, and a forward pass
  // sampling that many maps declared more images than one barrier call took.
  const std::array lights{shadowedSpot({-6.0f, 5.0f, 4.0f}), shadowedSpot({0.0f, 6.0f, 8.0f}),
                          shadowedSpot({6.0f, 5.0f, 4.0f}), shadowedPoint({-1.5f, 1.6f, 3.8f}),
                          shadowedPoint({3.0f, 2.8f, -1.0f})};
  SceneView view = boxScene(draws);
  view.lights = lights;

  RenderGraph graph{*device};
  std::array<RenderTarget, 3> targets{RenderTarget{*device, "viewport"}, RenderTarget{*device, "game view"},
                                      RenderTarget{*device, "preview"}};
  ICommandList &commands = device->beginFrame();
  graph.reset();
  for (RenderTarget &target : targets) {
    target.resize({64, 64});
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  }
  graph.execute(commands);
  device->endFrame();
  REQUIRE(graph.statistics().passes.size() * 2 > 128);
  REQUIRE(graph.statistics().passes.size() * 2 <= MaxTimestamps);
  renderer.destroyMesh(box);
}

TEST_CASE("a point light that no longer fits in the depth array is passed over, not a spot light after it",
          "[renderer][shadow][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  auto settings = testSettings();
  settings.maxLocalShadows = 8;
  Renderer renderer{*device, shaderDir(platform), settings};
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  // Four point lights take 24 of the 28 maps; the fifth would need six more, the spot light one.
  std::vector<Light> lights;
  lights.reserve(6);
  for (int i = 0; i < 5; ++i) {
    lights.push_back(shadowedPoint({static_cast<float>(i), 4.0f, 0.0f}));
  }
  lights.push_back(shadowedSpot({5.0f, 4.0f, 0.0f}));
  SceneView view = boxScene(draws);
  view.lights = lights;

  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  REQUIRE(renderer.statistics().localShadowCount == 5);
  REQUIRE(renderer.statistics().localShadowMapCount == 25);
  REQUIRE(renderer.localShadows().back().light == 5);
  REQUIRE(hasPass(graph, "shadow point 3 face 5"));
  REQUIRE(!hasPass(graph, "shadow point 4 face 0"));
  REQUIRE(hasPass(graph, "shadow spot 5"));
  renderer.destroyMesh(box);
}

TEST_CASE("a point light's face maps hold the depth of what each direction sees on a GPU", "[renderer][shadow][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.localShadowMapSize = 64;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle plane = renderer.createMesh(primitives::plane({20.0f, 20.0f}), "ground");
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = plane}, DrawItem{.mesh = box}};
    // The light is four metres above the ground, a box a metre wide under it.
    const std::array lights{shadowedPoint({0.0f, 4.0f, 0.0f})};
    SceneView view = topDownScene(draws);
    view.lights = lights;
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    constexpr std::uint32_t downFace = 3;
    constexpr std::uint32_t upFace = 2;
    const std::array faces{downFace, upFace};
    std::array<BufferHandle, 2> readbacks;
    for (BufferHandle &readback : readbacks) {
      readback = device->createBuffer({.size = std::size_t{64} * 64 * sizeof(float),
                                       .usage = BufferUsage::TransferDst,
                                       .memory = MemoryUsage::GpuToCpu,
                                       .debugName = "point depth readback"});
    }
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    REQUIRE(renderer.localShadows().size() == Renderer::PointShadowFaces);
    for (std::size_t i = 0; i < faces.size(); ++i) {
      const GraphImage map = renderer.localShadows()[faces[i]].image;
      graph.addPass(
          std::format("point depth readback {}", i), [&](PassBuilder &b) { b.transferSrc(map); },
          [&, map, i](ICommandList &cmd, const PassResources &resources) {
            cmd.copyImageToBuffer(resources.image(map), readbacks[i]);
          });
    }
    graph.execute(commands);
    device->endFrame();
    device->waitIdle();

    std::array<float, std::size_t{64} * 64> downDepth{};
    std::array<float, std::size_t{64} * 64> upDepth{};
    std::memcpy(downDepth.data(), device->mappedRange(readbacks[0]).data(), sizeof(downDepth));
    std::memcpy(upDepth.data(), device->mappedRange(readbacks[1]).data(), sizeof(upDepth));
    // Reversed-Z over an infinite far plane: near / distance along the face's axis, with near a
    // hundredth of the range. Looking down the face sees the box and the ground, looking up
    // nothing, which stays at the cleared far plane.
    const float nearPlane = 0.2f;
    REQUIRE(downDepth[32 * 64 + 32] == Catch::Approx(nearPlane / 3.5f).margin(0.002)); // the box's top
    REQUIRE(downDepth[8 * 64 + 8] == Catch::Approx(nearPlane / 4.0f).margin(0.002));   // the ground
    REQUIRE(downDepth[56 * 64 + 56] == Catch::Approx(nearPlane / 4.0f).margin(0.002));
    REQUIRE(upDepth[32 * 64 + 32] == 0.0f);
    REQUIRE(device->validationMessageCount() == 0);
    for (const BufferHandle readback : readbacks) {
      device->destroyBuffer(readback);
    }
    renderer.destroyMesh(box);
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a spot light's shadow map holds the depth of what its cone sees on a GPU", "[renderer][shadow][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.localShadowMapSize = 64;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle plane = renderer.createMesh(primitives::plane({20.0f, 20.0f}), "ground");
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const std::array draws{DrawItem{.mesh = plane}, DrawItem{.mesh = box}};
    // The light is four metres above the ground looking down, a box a metre wide under it.
    const std::array lights{shadowedSpot({0.0f, 4.0f, 0.0f})};
    SceneView view = topDownScene(draws);
    view.lights = lights;
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    const BufferHandle readback = device->createBuffer({.size = std::size_t{64} * 64 * sizeof(float),
                                                        .usage = BufferUsage::TransferDst,
                                                        .memory = MemoryUsage::GpuToCpu,
                                                        .debugName = "spot depth readback"});
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    REQUIRE(renderer.localShadows().size() == 1);
    const GraphImage map = renderer.localShadows()[0].image;
    graph.addPass(
        "spot depth readback", [&](PassBuilder &b) { b.transferSrc(map); },
        [&](ICommandList &cmd, const PassResources &resources) {
          cmd.copyImageToBuffer(resources.image(map), readback);
        });
    graph.execute(commands);
    device->endFrame();
    device->waitIdle();

    std::array<float, std::size_t{64} * 64> depth{};
    std::memcpy(depth.data(), device->mappedRange(readback).data(), sizeof(depth));
    // Reversed-Z over an infinite far plane: near / distance, with near a hundredth of the range.
    const float nearPlane = 0.2f;
    REQUIRE(depth[32 * 64 + 32] == Catch::Approx(nearPlane / 3.5f).margin(0.002)); // the box's top
    REQUIRE(depth[32 * 64 + 56] == Catch::Approx(nearPlane / 4.0f).margin(0.002)); // the ground
    REQUIRE(depth[8 * 64 + 8] == Catch::Approx(nearPlane / 4.0f).margin(0.002));
    REQUIRE(device->validationMessageCount() == 0);
    device->destroyBuffer(readback);
    renderer.destroyMesh(box);
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a spot light's shadow darkens the ground behind a box and only while it is granted on a GPU",
          "[renderer][shadow][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    std::array draws{DrawItem{.mesh = plane},
                     DrawItem{.mesh = box, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f})}};
    // The light is up and to the -X side, aimed at the box: its shadow falls on the ground from
    // x = 0.5 to 2.25. Six metres up, one metre is about nine pixels.
    Light spot = shadowedSpot({-3.0f, 3.0f, 0.0f});
    spot.direction = glm::normalize(glm::vec3{3.0f, -2.5f, 0.0f});
    spot.intensity = 60.0f;
    std::array lights{spot};
    SceneView view = topDownScene(draws);
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.lights = lights;
    GpuScene scene{*device, renderer, {64, 64}};
    const unsigned shadowedX = 32 + 11; // x = 1.2
    const unsigned litX = 32 + 29;      // x = 3.2, past the shadow and inside the cone
    const unsigned nearX = 32 - 13;     // x = -1.5, on the light's side of the box

    scene.render(view);
    REQUIRE(renderer.statistics().localShadowCount == 1);
    const Pixel shadowed = scene.pixel(shadowedX, 32);
    const Pixel lit = scene.pixel(litX, 32);
    REQUIRE(lit.r > 40);
    REQUIRE(scene.pixel(nearX, 32).r > 40);
    REQUIRE(shadowed.r * 4 < lit.r);

    // Moving the occluder updates the shadow: with the box away the same ground is lit.
    draws[1].transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 6.0f});
    scene.render(view);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);

    // The light that does not ask, the one that does not fit the budget and the disabled one
    // illuminate the ground behind the box all the same.
    draws[1].transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f});
    lights[0].castsShadows = false;
    scene.render(view);
    REQUIRE(renderer.statistics().localShadowCount == 0);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);
    lights[0].castsShadows = true;
    auto settings = testSettings();
    settings.maxLocalShadows = 0;
    renderer.setSettings(settings);
    scene.render(view);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);
    // Two shadowed lights in one cluster each pick their own map: a mirror image of the first
    // light shadows the -X side, so each side loses the far light and the two read alike, and both
    // are dimmer than with the shadows off.
    settings.maxLocalShadows = 8;
    renderer.setSettings(settings);
    spot.intensity = 20.0f; // two of them would saturate the target at 60
    Light mirror = spot;
    mirror.position.x = -spot.position.x;
    mirror.direction.x = -spot.direction.x;
    const std::array pair{spot, mirror};
    view.lights = pair;
    scene.render(view);
    REQUIRE(renderer.statistics().localShadowCount == 2);
    const Pixel plus = scene.pixel(shadowedX, 32);
    const Pixel minus = scene.pixel(32 - 11, 32);
    REQUIRE(plus.r > 40);
    REQUIRE(std::abs(plus.r - minus.r) <= 4);
    std::array unshadowed{spot, mirror};
    unshadowed[0].castsShadows = false;
    unshadowed[1].castsShadows = false;
    view.lights = unshadowed;
    scene.render(view);
    REQUIRE(plus.r + 5 < scene.pixel(shadowedX, 32).r);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a spot light ten metres away shadows the ground right behind a plinth on a GPU", "[renderer][shadow][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const std::array draws{
        DrawItem{.mesh = plane},
        DrawItem{.mesh = box,
                 .transform = glm::scale(glm::translate(glm::mat4{1.0f}, {0.0f, 0.25f, 0.55f}), {2.4f, 0.5f, 2.4f})}};
    Light spot = shadowedSpot({0.0f, 6.0f, 8.0f});
    spot.direction = glm::normalize(glm::vec3{0.0f, -4.8f, -7.0f});
    spot.intensity = 900.0f;
    spot.range = 18.0f;
    spot.innerAngle = 0.18f;
    spot.outerAngle = 0.36f;
    const std::array lights{spot};
    SceneView view = topDownScene(draws);
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.lights = lights;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view);
    // Behind the plinth, away from the light, the ground is in its shadow from the plinth's edge
    // on. The depth is near / distance, so a bias constant in it was 80 centimetres at this distance
    // and left a strip of light there; the ground to the side, outside the shadow, is lit.
    REQUIRE(renderer.statistics().localShadowCount == 1);
    REQUIRE(scene.pixel(32, 20).r == 0);
    REQUIRE(scene.pixel(32, 12).r > 40);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a point light's shadow darkens the ground behind a box and only while it is granted on a GPU",
          "[renderer][shadow][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    std::array draws{DrawItem{.mesh = plane},
                     DrawItem{.mesh = box, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f})}};
    // The light is up and to the -X side of the box: its shadow falls on the ground from x = 0.5
    // to 2.25, seen through the +X face of the cube, and the ground before the box through -Y.
    Light point = shadowedPoint({-3.0f, 3.0f, 0.0f});
    point.intensity = 60.0f;
    std::array lights{point};
    SceneView view = topDownScene(draws);
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.lights = lights;
    GpuScene scene{*device, renderer, {64, 64}};
    const unsigned shadowedX = 32 + 11; // x = 1.2
    const unsigned litX = 32 + 29;      // x = 3.2, past the shadow
    const unsigned nearX = 32 - 13;     // x = -1.5, on the light's side of the box
    const unsigned sideY = 32 + 17;     // z = 2.8, off to the side of the shadow, through the +Z face

    scene.render(view);
    REQUIRE(renderer.statistics().localShadowCount == 1);
    REQUIRE(renderer.statistics().localShadowMapCount == Renderer::PointShadowFaces);
    const Pixel shadowed = scene.pixel(shadowedX, 32);
    const Pixel lit = scene.pixel(litX, 32);
    REQUIRE(lit.r > 40);
    REQUIRE(scene.pixel(nearX, 32).r > 40);
    REQUIRE(scene.pixel(shadowedX, sideY).r > 40);
    REQUIRE(shadowed.r * 4 < lit.r);

    // Moving the occluder updates the shadow: with the box away the same ground is lit.
    draws[1].transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 6.0f});
    scene.render(view);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);

    // The light that does not ask and the one that does not fit the budget illuminate the ground
    // behind the box all the same.
    draws[1].transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f});
    lights[0].castsShadows = false;
    scene.render(view);
    REQUIRE(renderer.statistics().localShadowCount == 0);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);
    lights[0].castsShadows = true;
    auto settings = testSettings();
    settings.maxLocalShadows = 0;
    renderer.setSettings(settings);
    scene.render(view);
    REQUIRE(scene.pixel(shadowedX, 32).r > 40);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a clustered point light reaches only the ground within its range on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const std::array draws{DrawItem{.mesh = plane}};
    const std::array lights{Light{.type = LightType::Point,
                                  .position = {-1.5f, 0.5f, 0.0f},
                                  .color = {1.0f, 0.9f, 0.8f},
                                  .intensity = 4.0f,
                                  .range = 1.5f}};
    SceneView view = topDownScene(draws);
    view.camera.projectionKind = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    view.camera.orthoHeight = 12.0f * std::tan(view.camera.fovY * 0.5f);
    if (view.camera.projectionKind == ProjectionKind::Perspective) {
      view.dollyDepthOffset = GENERATE(0.0f, 2000.0f);
      view.camera.position.y += view.dollyDepthOffset;
      view.camera.fovY = 2.0f * std::atan(std::tan(view.camera.fovY * 0.5f) * 6.0f / (6.0f + view.dollyDepthOffset));
    }
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.lights = lights;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view);

    const Pixel under = scene.pixel(32 - 13, 32);
    const Pixel beyond = scene.pixel(32 + 18, 32);
    REQUIRE(under.r > 80);
    REQUIRE(beyond.r + beyond.g + beyond.b == 0);
    REQUIRE(renderer.statistics().lightCount == 1);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("HDR mip levels average radiance without clamping it", "[renderer][texture]") {
  auto map = skyTexture({4.0f, 8.0f, 16.0f});
  const std::uint16_t bright = glm::packHalf1x16(12.0f);
  std::memcpy(map.data.data(), &bright, sizeof(bright));
  generateMipChain(map);
  REQUIRE(map.mipLevels == 4);
  REQUIRE(map.data.size() == map.expectedSize());
  std::uint16_t half;
  std::memcpy(&half, map.level(1).data(), sizeof(half));
  CHECK(glm::unpackHalf1x16(half) == 6.0f);
  std::memcpy(&half, map.level(3).data(), sizeof(half));
  CHECK(glm::unpackHalf1x16(half) == 4.25f);
}

TEST_CASE("sky detail is independent of the lighting cube resolution", "[renderer][gpu][skybox]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  const auto map = detailedSky();
  std::vector<std::byte> reference;
  for (const auto cubeSize : {8u, 128u}) {
    auto settings = testSettings();
    settings.environmentSize = cubeSize;
    settings.maxEnvironmentSize = cubeSize;
    Renderer renderer{*device, shaderDir(platform), settings};
    const auto sky = renderer.createEnvironment(map, "detailed sky");
    SceneView view = boxScene({});
    view.environment = sky;
    // Looking through the longitude seam; the map must survive the first frame as well.
    view.camera.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0, 1, 0});
    GpuScene scene{*device, renderer, {128, 128}};
    scene.render(view, 2);
    auto pixels = device->mappedRange(scene.readback);
    if (reference.empty()) {
      reference.assign(pixels.begin(), pixels.end());
      int darkest = 255, brightest = 0;
      for (unsigned x = 0; x < 128; ++x) {
        const auto pixel = scene.pixel(x, 64);
        darkest = std::min(darkest, pixel.r);
        brightest = std::max(brightest, pixel.r);
      }
      CHECK(brightest - darkest > 50);
      // The wrap must preserve the same detail and brightness as its neighbours.
      CHECK(std::abs(scene.pixel(63, 64).r - scene.pixel(64, 64).r) < 10);
    } else {
      CHECK(std::equal(reference.begin(), reference.end(), pixels.begin(), pixels.end()));
    }
    renderer.destroyEnvironment(sky);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("sky rays and mip selection ignore camera distance", "[renderer][gpu][skybox]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  for (const auto antialiasing : {AntiAliasing::None, AntiAliasing::Taa}) {
    auto settings = testSettings();
    settings.antialiasing = antialiasing;
    Renderer renderer{*device, shaderDir(platform), settings};
    const auto sky = renderer.createEnvironment(detailedSky(), "distant sky");
    GpuScene scene{*device, renderer, {64, 64}};
    SceneView view = boxScene({});
    view.environment = sky;
    view.camera.rotation = glm::angleAxis(0.37f, glm::normalize(glm::vec3{1, 2, 3}));
    for (const auto projection : {ProjectionKind::Perspective, ProjectionKind::Orthographic}) {
      view.camera.projectionKind = projection;
      view.camera.position = glm::vec3{0};
      scene.render(view, 24);
      const auto bytes = device->mappedRange(scene.readback);
      const std::vector<std::byte> reference{bytes.begin(), bytes.end()};
      for (const float distance : {10000.0f, -10000.0f, 1000000.0f, -1000000.0f}) {
        CAPTURE(antialiasing, projection, distance);
        view.camera.position = glm::vec3{distance, distance * 0.5f, -distance};
        scene.render(view, 24);
        CHECK(std::ranges::equal(reference, device->mappedRange(scene.readback)));
      }
    }
    renderer.destroyEnvironment(sky);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a minified sky filters across the longitude seam and both poles", "[renderer][gpu][skybox]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  TextureData map{.size = {512, 256}, .format = Format::R16G16B16A16Sfloat, .data = {}};
  map.data.resize(static_cast<std::size_t>(map.expectedSize()));
  for (unsigned y = 0; y < map.size.y; ++y) {
    for (unsigned x = 0; x < map.size.x; ++x) {
      const auto value = glm::packHalf1x16((x + y) % 2 ? 1.0f : 0.0f);
      const std::array<std::uint16_t, 4> texel{value, value, value, glm::packHalf1x16(1.0f)};
      std::memcpy(map.data.data() + (std::size_t(y) * map.size.x + x) * sizeof(texel), texel.data(), sizeof(texel));
    }
  }
  const auto checker = renderer.createEnvironment(map, "checker sky");
  const auto flat = renderer.createEnvironment(skyTexture({0.5f, 0.5f, 0.5f}), "flat sky");
  GpuScene scene{*device, renderer, {16, 16}};
  SceneView view = boxScene({});
  view.camera.fovY = glm::radians(120.0f);
  for (const auto direction : {glm::vec3{-1, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{0, -1, 0}}) {
    CAPTURE(direction.x, direction.y, direction.z);
    view.camera.rotation = glm::quatLookAt(direction, direction.y == 0 ? glm::vec3{0, 1, 0} : glm::vec3{0, 0, 1});
    view.environment = flat;
    scene.render(view);
    const Pixel expected = scene.pixel(8, 8);
    view.environment = checker;
    scene.render(view, 2);
    for (unsigned y = 0; y < 16; ++y) {
      for (unsigned x = 0; x < 16; ++x) {
        CHECK(std::abs(scene.pixel(x, y).r - expected.r) <= 1);
      }
    }
  }
  renderer.destroyEnvironment(checker);
  renderer.destroyEnvironment(flat);
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("an environment fills the background and lights a sphere on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const EnvironmentHandle environment = renderer.createEnvironment(skyTexture({0.1f, 0.3f, 0.9f}), "sky");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 16, 8), "sphere");
    MaterialDesc white;
    white.metallic = 0.0f;
    white.roughness = 0.8f;
    const MaterialHandle material = renderer.createMaterial(white, "white");
    const std::array draws{DrawItem{.mesh = sphere, .material = material}};
    SceneView view = boxScene(draws);
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.environment = environment;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2); // the first frame computes the cubes, the second uses them

    const Pixel sky = scene.pixel(2, 2);
    REQUIRE(sky.b > sky.r + 50);
    const Pixel body = scene.pixel(32, 32);
    // Lit by the sky alone: blue, dimmer than the sky.
    REQUIRE(body.b > 30);
    REQUIRE(body.b > body.r);
    REQUIRE(body.b < sky.b);
    REQUIRE(renderer.isReady(environment));
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(material);
    renderer.destroyMesh(sphere);
    renderer.destroyEnvironment(environment);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// The irradiance of a speckled sky is smooth. Each sample stands for far more sky than a texel of
// the level it reads, so with the settings' level alone neighbouring normals saw different
// handfuls of speckles and a smooth sphere came out blotchy.
TEST_CASE("a speckled sky lights a sphere smoothly on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.environmentSize = 256;
    settings.irradianceSize = 32;
    settings.irradianceSamples = 64;
    Renderer renderer{*device, shaderDir(platform), settings};
    TextureData sky{
        .size = {256, 128}, .format = Format::R16G16B16A16Sfloat, .mipLevels = 1, .cube = false, .data = {}};
    sky.data.resize(static_cast<std::size_t>(sky.expectedSize()));
    for (std::size_t texel = 0; texel < sky.data.size() / 8; ++texel) {
      const float value = (texel * 2654435761u >> 7) % 7 == 0 ? 4.0f : 0.05f;
      const std::array<std::uint16_t, 4> rgba{glm::packHalf1x16(value), glm::packHalf1x16(value),
                                              glm::packHalf1x16(value), glm::packHalf1x16(1.0f)};
      std::memcpy(sky.data.data() + texel * 8, rgba.data(), sizeof(rgba));
    }
    const EnvironmentHandle environment = renderer.createEnvironment(sky, "speckled");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 32, 16), "sphere");
    MaterialDesc white;
    white.metallic = 0.0f;
    white.roughness = 1.0f;
    const MaterialHandle material = renderer.createMaterial(white, "white");
    const std::array draws{DrawItem{.mesh = sphere, .material = material}};
    SceneView view = boxScene(draws);
    view.hasSun = false;
    view.ambient = {0.0f, 0.0f, 0.0f};
    view.environment = environment;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2);

    // Neighbouring pixels of the sphere's middle differ by a few levels at most.
    int largest = 0;
    for (unsigned y = 26; y < 38; ++y) {
      for (unsigned x = 26; x < 37; ++x) {
        largest = std::max(largest, std::abs(scene.pixel(x, y).r - scene.pixel(x + 1, y).r));
      }
    }
    INFO("largest step between neighbours " << largest);
    REQUIRE(scene.pixel(32, 32).r > 20);
    REQUIRE(largest <= 4);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMaterial(material);
    renderer.destroyMesh(sphere);
    renderer.destroyEnvironment(environment);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// Every colour channel of a material texture reaches the shading, with the shadow comparison in
// the same shader. On MoltenVK a comparison through the colour array made SPIRV-Cross type it as
// Metal depth textures, and every colour read returned its red alone (ADR-0017).
TEST_CASE("a texture's green reaches the albedo beside the shadow comparison on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = testSettings();
    settings.debugView = DebugView::Albedo;
    Renderer renderer{*device, shaderDir(platform), settings};
    const TextureHandle green = renderer.createTexture(solidTexture({0, 255, 0, 255}), "green");
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    MaterialDesc desc;
    desc.metallic = 0.0f;
    desc.baseColorTexture = green;
    const MaterialHandle material = renderer.createMaterial(desc, "green");
    const std::array draws{DrawItem{.mesh = box, .material = material}};
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(boxScene(draws));

    const Pixel centre = scene.pixel(32, 32);
    CAPTURE(centre.r, centre.g, centre.b);
    REQUIRE(centre.g > 150);
    REQUIRE(centre.r < 40);
    REQUIRE(centre.b < 40);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(material);
    renderer.destroyMesh(box);
    renderer.destroyTexture(green);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// KHR_texture_transform on a texture red on its left half and green on its right. A zero scale
// samples the offset alone; a quarter turn with scale (0, 0.5) samples u = 0.5 - 0.5 v, which is
// red at the face's centre (v = 0.5), where a turn the other way would read green.
TEST_CASE("a texture transform moves where the base colour is sampled on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = testSettings();
    settings.debugView = DebugView::Albedo;
    Renderer renderer{*device, shaderDir(platform), settings};
    TextureData halves = solidTexture({255, 0, 0, 255});
    halves.size = {2, 1};
    for (const int byte : {0, 255, 0, 255}) {
      halves.data.push_back(static_cast<std::byte>(byte));
    }
    const TextureHandle texture = renderer.createTexture(halves, "halves");
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const auto centreOf = [&](const TextureTransform &transform) {
      MaterialDesc desc;
      desc.metallic = 0.0f;
      desc.baseColorTexture = texture;
      desc.baseColorTransform = transform;
      const MaterialHandle material = renderer.createMaterial(desc, "transformed");
      const std::array draws{DrawItem{.mesh = box, .material = material}};
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(boxScene(draws));
      const Pixel centre = scene.pixel(32, 32);
      renderer.destroyMaterial(material);
      return centre;
    };
    const auto isRed = [](const Pixel &pixel) { return pixel.r > 150 && pixel.g < 40; };
    const auto isGreen = [](const Pixel &pixel) { return pixel.g > 150 && pixel.r < 40; };

    const Pixel left = centreOf({.offset = {0.25f, 0.5f}, .rotation = 0.0f, .scale = {0.0f, 0.0f}});
    const Pixel right = centreOf({.offset = {0.75f, 0.5f}, .rotation = 0.0f, .scale = {0.0f, 0.0f}});
    const Pixel turned = centreOf({.offset = {0.5f, 0.5f}, .rotation = glm::half_pi<float>(), .scale = {0.0f, 0.5f}});
    CAPTURE(left.r, left.g, right.r, right.g, turned.r, turned.g);
    REQUIRE(isRed(left));
    REQUIRE(isGreen(right));
    REQUIRE(isRed(turned));
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMesh(box);
    renderer.destroyTexture(texture);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// The split-sum table at n.v near one: the scale near one and the bias near zero, in red and
// green. MoltenVK read the bias as the scale before ADR-0017, whitening every surface.
TEST_CASE("the BRDF lookup table reads a large scale and a small bias on a GPU", "[renderer][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = testSettings();
    settings.brdfLutSize = 32;
    settings.brdfLutSamples = 256;
    settings.debugView = DebugView::BrdfLut;
    Renderer renderer{*device, shaderDir(platform), settings};
    const EnvironmentHandle environment = renderer.createEnvironment(skyTexture({0.1f, 0.3f, 0.9f}), "sky");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 16, 8), "sphere");
    MaterialDesc desc;
    desc.metallic = 0.0f;
    desc.roughness = 0.5f;
    const MaterialHandle material = renderer.createMaterial(desc, "rough");
    const std::array draws{DrawItem{.mesh = sphere, .material = material}};
    SceneView view = boxScene(draws);
    view.hasSun = false;
    view.environment = environment;
    GpuScene scene{*device, renderer, {64, 64}};
    scene.render(view, 2); // the first frame computes the table, the second reads it

    // Tone-mapped and display-encoded: a scale near 0.9 reads above 200, a bias near 0 below 40.
    const Pixel centre = scene.pixel(32, 32);
    CAPTURE(centre.r, centre.g, centre.b);
    REQUIRE(centre.r > 180);
    REQUIRE(centre.g < 40);
    REQUIRE(device->validationMessageCount() == 0);

    renderer.destroyMaterial(material);
    renderer.destroyMesh(sphere);
    renderer.destroyEnvironment(environment);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("reloading a shader rebuilds its pipelines and keeps them on a rejected module", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  REQUIRE(std::ranges::find(Renderer::shaderNames(), "forward") != Renderer::shaderNames().end());
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({16, 16});

  const auto spirv = sonnet::core::readFile(shaderDir(platform) / "forward.spv");
  REQUIRE(spirv.has_value());
  REQUIRE(renderer.reloadShader("forward", *spirv).has_value());
  REQUIRE(!renderer.reloadShader("nonsense", *spirv).has_value());
  const std::array<std::byte, 8> garbage{};
  // The null device accepts any bytes; a real one rejects garbage and the old pipelines stay.
  REQUIRE(renderer.reloadShader("post", garbage).has_value());

  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"tonemap\"") == 1);
  renderer.destroyMesh(box);
}

// The README's performance target, measured rather than asserted: hidden from the default run,
// `renderer_tests "[benchmark]"` prints the GPU time per pass for ten thousand draws and a
// hundred lights at 1080p on whatever device is present.
TEST_CASE("ten thousand draws and a hundred lights at 1080p", "[.][benchmark][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  // SONNET_BENCH_WORKERS=0 measures the per-frame fill on one thread, for the comparison
  // ADR-0013 asks for; unset is the machine's pool, which is what the editor and player run.
  sonnet::core::JobSystem jobs{[] {
    sonnet::core::JobSystemDesc desc;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
    const char *workers = std::getenv("SONNET_BENCH_WORKERS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (workers != nullptr) {
      desc.workerCount = static_cast<std::uint32_t>(std::atoi(workers));
    }
    return desc;
  }()};
  // One scene, measured on the device given; the renderer and everything it made are gone after.
  // `occluded` puts a wall between the camera and most of the grid; `occlusion` turns the
  // depth-pyramid culling (ADR-0024) on or off.
  const auto measure = [&](IDevice &device, bool occlusion, bool occluded) {
    {
      // SONNET_BENCH_AA=none|fxaa measures the other anti-aliasing modes; unset is TAA, the default.
      RendererSettings settings{.occlusionCulling = occlusion, .jobs = &jobs};
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
      const char *mode = std::getenv("SONNET_BENCH_AA");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
      if (mode != nullptr) {
        settings.antialiasing = std::string_view{mode} == "none"   ? AntiAliasing::None
                                : std::string_view{mode} == "fxaa" ? AntiAliasing::Fxaa
                                                                   : AntiAliasing::Taa;
      }
      Renderer renderer{device, shaderDir(platform), settings};
      const MeshHandle box = renderer.createMesh(primitives::box(), "box");
      const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 16, 8), "sphere");
      MaterialDesc rough;
      rough.metallic = 0.0f;
      rough.roughness = 0.7f;
      const MaterialHandle material = renderer.createMaterial(rough, "rough");
      const EnvironmentHandle environment = renderer.createEnvironment(skyTexture({0.4f, 0.5f, 0.8f}), "sky");
      std::vector<DrawItem> draws;
      std::vector<Light> lights;
      constexpr int side = 100;
      for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
          const glm::vec3 position{(static_cast<float>(x) - static_cast<float>(side) / 2.0f) * 1.5f, 0.5f,
                                   (static_cast<float>(z) - static_cast<float>(side) / 2.0f) * 1.5f};
          draws.push_back({.mesh = (x + z) % 2 == 0 ? box : sphere,
                           .material = material,
                           .transform = glm::translate(glm::mat4{1.0f}, position),
                           .id = static_cast<std::uint32_t>(draws.size() + 1)});
        }
      }
      for (int i = 0; i < 100; ++i) {
        const float angle = static_cast<float>(i) * 0.37f;
        lights.push_back({.type = LightType::Point,
                          .position = {std::cos(angle) * (5.0f + static_cast<float>(i) * 0.5f), 1.5f,
                                       std::sin(angle) * (5.0f + static_cast<float>(i) * 0.5f)},
                          .color = {1.0f, 0.8f, 0.6f},
                          .intensity = 8.0f,
                          .range = 6.0f});
      }
      SceneView view;
      view.camera.position = {0.0f, 12.0f, 40.0f};
      view.camera.rotation = glm::angleAxis(glm::radians(-18.0f), glm::vec3{1.0f, 0.0f, 0.0f});
      if (occluded) {
        // Level, low and behind a wall as wide and tall as the grid's near edge.
        draws.push_back({.mesh = box,
                         .material = material,
                         .transform = glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 6.0f, 60.0f}),
                                                 glm::vec3{200.0f, 14.0f, 1.0f}),
                         .id = static_cast<std::uint32_t>(draws.size() + 1)});
        view.camera.position = {0.0f, 2.0f, 70.0f};
        view.camera.rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
      }
      view.draws = draws;
      view.lights = lights;
      view.environment = environment;
      GpuScene scene{device, renderer, {1920, 1080}};
      constexpr int frames = 30;
      scene.render(view, frames);
      // Per-pass timestamps are a sample of one frame, and MoltenVK builds them from encoder
      // boundaries, so also time a long run end to end: frames submitted back to back and one
      // wait at the end, which no timestamp enters into.
      constexpr int timedFrames = 120;
      const auto timedStart = std::chrono::steady_clock::now();
      scene.render(view, timedFrames);
      const double wallMilliseconds =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - timedStart).count();
      WARN(std::format("wall clock: {:.3f} ms per frame over {} frames submitted back to back",
                       wallMilliseconds / timedFrames, timedFrames));
      // GPU timings describe one earlier completed slot, not an average of the timed run.
      // In particular, the final readback did not exist in that slot. Missing queries are not
      // measured zeroes, and a partial sum must not masquerade as a complete frame time.
      float total = 0.0f;
      std::size_t availablePasses = 0;
      const auto &passes = scene.graph.statistics().passes;
      for (const PassTiming &pass : passes) {
        if (pass.gpuAvailable) {
          WARN(std::format("{:<20} {:8.3f} ms GPU {:8.3f} ms CPU", pass.name, pass.gpuMilliseconds,
                           pass.cpuMilliseconds));
          total += pass.gpuMilliseconds;
          ++availablePasses;
        } else {
          WARN(std::format("{:<20} unavailable GPU {:8.3f} ms CPU", pass.name, pass.cpuMilliseconds));
        }
      }
      WARN(std::format("{} draws, {} lights, {} triangles: {:.3f} ms sampled GPU pass sum ({}/{} passes "
                       "available, one earlier frame) on {}",
                       renderer.statistics().drawCount, renderer.statistics().lightCount,
                       renderer.statistics().triangleCount, total, availablePasses, passes.size(),
                       device.info().deviceName));
      WARN(std::format("submitted from the GPU in {} indirect calls over {} passes",
                       renderer.statistics().indirectCallCount, Renderer::CascadeCount + Renderer::CullJobsOpaque));
      WARN(std::format("{} of {} draws survived culling ({} triangles), {} shadow draws, occlusion {}, wall {}",
                       renderer.statistics().visibleDrawCount, renderer.statistics().drawCount,
                       renderer.statistics().visibleTriangleCount, renderer.statistics().visibleShadowDrawCount,
                       occlusion ? "on" : "off", occluded ? "yes" : "no"));
      // The first frames compile pipelines and upload the scene; the rest are the steady state.
      std::vector<double> submits{scene.submitMilliseconds.begin() + 5, scene.submitMilliseconds.end()};
      std::ranges::sort(submits);
      const float recording = [&] {
        float sum = 0.0f;
        for (const PassTiming &pass : scene.graph.statistics().passes) {
          sum += pass.cpuMilliseconds;
        }
        return sum;
      }();
      WARN(std::format("CPU per frame: {:.3f} ms recording the passes, {:.3f} ms submitting (median of {}, "
                       "worst {:.3f} ms)",
                       recording, submits[submits.size() / 2], submits.size(), submits.back()));
      REQUIRE(renderer.statistics().drawCount == draws.size());
      // Two meshes, so two batches, and one instanced command each in the four cascades, the
      // pre-pass and the forward pass: what used to be sixty thousand draw calls (ADR-0012), and
      // sixty thousand commands after them (ADR-0016). The occlusion pass adds the pre-pass's late
      // command. The wall shares the box's batch.
      REQUIRE(renderer.statistics().indirectCallCount ==
              2 * ((Renderer::CascadeCount + Renderer::CullJobsOpaque) + (occlusion ? 1u : 0u)));
      REQUIRE(device.validationMessageCount() == 0);
      renderer.destroyEnvironment(environment);
      renderer.destroyMaterial(material);
      renderer.destroyMesh(sphere);
      renderer.destroyMesh(box);
    }
    REQUIRE(device.validationMessageCount() == 0);
  };
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  measure(*device, false, false);
  measure(*device, true, false);
  measure(*device, false, true);
  measure(*device, true, true);
}

// What particles cost (ADR-0023): `renderer_tests "[particles]"` fills ten emitters up to about a
// hundred and seventy thousand live particles at 1080p and prints the GPU time of the passes that simulate
// and draw them, and the CPU time of the frame.
TEST_CASE("ten emitters of twenty thousand particles at 1080p", "[.][benchmark][particles][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), {}};
    std::vector<ParticleEmitterItem> emitters;
    for (std::uint64_t i = 0; i < 10; ++i) {
      ParticleEmitterItem item = glowEmitter();
      item.key = i + 1;
      item.transform = glm::translate(glm::mat4{1.0f}, glm::vec3{static_cast<float>(i) - 5.0f, 0.0f, 0.0f});
      item.maxParticles = 20000;
      item.rate = 10000.0f; // a second of life and a half at that rate leaves about 17 500 alive of 20 000
      item.lifetime = {1.5f, 2.0f};
      item.speed = {0.5f, 2.5f};
      item.coneAngle = 1.2f;
      item.sizeStart = 0.05f;
      item.sizeEnd = 0.02f;
      item.colorStart = {0.4f, 0.2f, 0.1f, 0.5f};
      item.blend = i % 2 == 0 ? ParticleBlend::Additive : ParticleBlend::Alpha;
      emitters.push_back(item);
    }
    SceneView view;
    view.camera.position = {0.0f, 1.0f, 9.0f};
    view.particles = emitters;
    view.deltaTime = 1.0f / 60.0f;
    GpuScene scene{*device, renderer, {1920, 1080}};
    scene.render(view, 150); // two and a half seconds, so every ring has filled
    for (const PassTiming &pass : scene.graph.statistics().passes) {
      if (pass.name == "particles" || pass.name == "forward") {
        WARN(
            std::format("{:<12} {:8.3f} ms GPU {:8.3f} ms CPU", pass.name, pass.gpuMilliseconds, pass.cpuMilliseconds));
      }
    }
    WARN(std::format("{} emitters, {} slots on {}", renderer.statistics().particleEmitterCount,
                     renderer.statistics().particleSlotCount, device->info().deviceName));
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// What a second view costs (docs/decisions/0021-two-views-in-one-frame.md): the same objects and
// lights through two cameras into two 1080p targets, against one. The showcase sample has about
// 440 mesh renderers and 67 lights; the second scale is the benchmark above.
TEST_CASE("the cost of a second view at the showcase's scale and at ten thousand draws", "[.][benchmark][views][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto measure = [&](IDevice &device, int side, int lightCount, int viewCount) {
    Renderer renderer{device, shaderDir(platform), {}};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 16, 8), "sphere");
    MaterialDesc rough;
    rough.metallic = 0.0f;
    rough.roughness = 0.7f;
    const MaterialHandle material = renderer.createMaterial(rough, "rough");
    const EnvironmentHandle environment = renderer.createEnvironment(skyTexture({0.4f, 0.5f, 0.8f}), "sky");
    std::vector<DrawItem> draws;
    std::vector<Light> lights;
    for (int z = 0; z < side; ++z) {
      for (int x = 0; x < side; ++x) {
        const glm::vec3 position{(static_cast<float>(x) - static_cast<float>(side) / 2.0f) * 1.5f, 0.5f,
                                 (static_cast<float>(z) - static_cast<float>(side) / 2.0f) * 1.5f};
        draws.push_back({.mesh = (x + z) % 2 == 0 ? box : sphere,
                         .material = material,
                         .transform = glm::translate(glm::mat4{1.0f}, position),
                         .id = static_cast<std::uint32_t>(draws.size() + 1)});
      }
    }
    for (int i = 0; i < lightCount; ++i) {
      const float angle = static_cast<float>(i) * 0.37f;
      lights.push_back({.type = LightType::Point,
                        .position = {std::cos(angle) * (5.0f + static_cast<float>(i) * 0.5f), 1.5f,
                                     std::sin(angle) * (5.0f + static_cast<float>(i) * 0.5f)},
                        .color = {1.0f, 0.8f, 0.6f},
                        .intensity = 8.0f,
                        .range = 6.0f});
    }
    std::vector<SceneView> views(static_cast<std::size_t>(viewCount));
    for (std::size_t i = 0; i < views.size(); ++i) {
      views[i].draws = draws;
      views[i].lights = lights;
      views[i].environment = environment;
      views[i].camera.position = {i == 0 ? 0.0f : 20.0f, 12.0f, 40.0f};
      views[i].camera.rotation = glm::angleAxis(glm::radians(-18.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    }
    std::deque<RenderTarget> targets; // a target cannot move, and a deque never moves its elements
    for (std::size_t i = 0; i < views.size(); ++i) {
      targets.emplace_back(device, "view");
      targets.back().resize({1920, 1080});
    }
    RenderGraph graph{device};
    std::vector<double> declare;
    std::vector<double> submit;
    constexpr int frames = 40;
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      const auto start = std::chrono::steady_clock::now();
      for (std::size_t i = 0; i < views.size(); ++i) {
        renderer.addScenePasses(graph, views[i], graph.importImage(targets[i].color()),
                                graph.importImage(targets[i].depth()));
      }
      declare.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
      graph.execute(commands);
      const auto submitStart = std::chrono::steady_clock::now();
      device.endFrame();
      submit.push_back(
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitStart).count());
    }
    device.waitIdle();
    // The last frame's timings are those of the frame two before it, complete by now.
    float gpu = 0.0f;
    float recording = 0.0f;
    for (const PassTiming &pass : graph.statistics().passes) {
      gpu += pass.gpuMilliseconds;
      recording += pass.cpuMilliseconds;
    }
    const auto median = [](std::vector<double> values) {
      values.erase(values.begin(), values.begin() + 10); // pipelines compile and buffers grow first
      std::ranges::sort(values);
      return values[values.size() / 2];
    };
    WARN(std::format("{:>5} draws {:>3} lights {} view(s): declare {:.3f} ms, recording {:.3f} ms, submit {:.3f} ms, "
                     "GPU {:.3f} ms, graph images {:.1f} MiB, {} indirect calls, {}",
                     draws.size(), lights.size(), viewCount, median(declare), recording, median(submit), gpu,
                     static_cast<double>(graph.statistics().transientImageBytes) / (1024.0 * 1024.0),
                     renderer.statistics(0).indirectCallCount + renderer.statistics(1).indirectCallCount,
                     device.info().deviceName));
    REQUIRE(device.validationMessageCount() == 0);
    renderer.destroyEnvironment(environment);
    renderer.destroyMaterial(material);
    renderer.destroyMesh(sphere);
    renderer.destroyMesh(box);
  };
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  for (const auto &[side, lights] : {std::pair{21, 67}, std::pair{100, 100}}) {
    for (int views = 1; views <= 2; ++views) {
      measure(*device, side, lights, views);
    }
  }
}

// A far cascade draws the coarse range of a caster whose coarse surface is within a fraction of a
// texel (ADR-0025): the same casters survive culling, with fewer triangles, and a caster too big
// for the tolerance keeps its fine range.
TEST_CASE("a far shadow cascade draws the coarse range of a small caster", "[renderer][gpu][shadow][lod]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    RendererSettings fine = testSettings();
    RendererSettings lod = fine;
    lod.shadowLodCascade = 3u;
    Renderer fineRenderer{*device, shaderDir(platform), fine};
    Renderer lodRenderer{*device, shaderDir(platform), lod};
    MeshData sphere = primitives::icosphere(0.5f, 3);
    buildShadowRange(sphere);
    REQUIRE(sphere.submeshes[0].shadowIndexCount > 0);
    const MeshHandle fineMesh = fineRenderer.createMesh(sphere, "sphere");
    const MeshHandle lodMesh = lodRenderer.createMesh(sphere, "sphere");
    const float scale = GENERATE(1.0f, 400.0f); // a metre across, and a kilometre
    const auto scene = [&](MeshHandle mesh, std::vector<DrawItem> &draws) {
      draws.clear();
      for (int i = -3; i <= 3; ++i) {
        draws.push_back(DrawItem{
            .mesh = mesh,
            .transform =
                glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f * static_cast<float>(i), 0.0f, -6.0f}),
                           glm::vec3{scale}),
            .color = {0.7f, 0.7f, 0.7f, 1.0f}});
      }
    };
    std::vector<DrawItem> fineDraws;
    std::vector<DrawItem> lodDraws;
    scene(fineMesh, fineDraws);
    scene(lodMesh, lodDraws);
    GpuScene fineScene{*device, fineRenderer, {64, 64}};
    GpuScene lodScene{*device, lodRenderer, {64, 64}};
    SceneView fineView = boxScene(fineDraws);
    SceneView lodView = boxScene(lodDraws);
    fineView.sun.direction = lodView.sun.direction = glm::normalize(glm::vec3{0.3f, -1.0f, -0.2f});
    for (int frame = 0; frame < 8; ++frame) {
      fineScene.render(fineView, 1);
      lodScene.render(lodView, 1);
    }
    REQUIRE(fineRenderer.statistics().visibleCountsKnown);
    REQUIRE(lodRenderer.statistics().visibleCountsKnown);
    REQUIRE(lodRenderer.statistics().visibleShadowDrawCount == fineRenderer.statistics().visibleShadowDrawCount);
    INFO("fine " << fineRenderer.statistics().visibleShadowTriangleCount << " lod "
                 << lodRenderer.statistics().visibleShadowTriangleCount);
    if (scale == 1.0f) {
      REQUIRE(lodRenderer.statistics().visibleShadowTriangleCount <
              fineRenderer.statistics().visibleShadowTriangleCount);
    } else {
      REQUIRE(lodRenderer.statistics().visibleShadowTriangleCount ==
              fineRenderer.statistics().visibleShadowTriangleCount);
    }
    REQUIRE(device->validationMessageCount() == 0);
    fineRenderer.destroyMesh(fineMesh);
    lodRenderer.destroyMesh(lodMesh);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("floor grid is stable under TAA and hidden by ground", "[renderer][gpu][grid]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.antialiasing = AntiAliasing::Taa;
    Renderer renderer{*device, shaderDir(platform), settings};
    GpuScene scene{*device, renderer, {128, 128}};
    SceneView view = topDownScene({});
    scene.grid = true;
    scene.render(view, 10);
    const auto bytes = device->mappedRange(scene.readback);
    const std::vector<std::byte> first{bytes.begin(), bytes.end()};
    const Pixel red = scene.pixel(90, 63);
    const Pixel blue = scene.pixel(63, 90);
    REQUIRE(red.r > red.b * 2);
    REQUIRE(blue.b > blue.r * 2);
    for (int frame = 0; frame < 8; ++frame) {
      scene.render(view);
      REQUIRE(std::ranges::equal(first, device->mappedRange(scene.readback)));
    }
    const MeshHandle ground = renderer.createMesh(primitives::plane(), "grid occluder");
    const std::array draws{DrawItem{.mesh = ground, .transform = glm::scale(glm::mat4{1}, glm::vec3{100})}};
    view.draws = draws;
    // Tilted ground exercises the jittered depth buffer rather than constant top-down depth.
    view.camera.position = {0, 4, 6};
    view.camera.rotation = glm::angleAxis(glm::radians(-35.0f), glm::vec3{1, 0, 0});
    std::array<std::vector<std::byte>, 8> without;
    scene.grid = false;
    for (std::size_t frame = 0; frame < without.size(); ++frame) {
      view.resetHistory = frame == 0;
      scene.render(view);
      const auto plain = device->mappedRange(scene.readback);
      without[frame].assign(plain.begin(), plain.end());
    }
    scene.grid = true;
    for (std::size_t frame = 0; frame < without.size(); ++frame) {
      view.resetHistory = frame == 0;
      scene.render(view);
      REQUIRE(std::ranges::equal(without[frame], device->mappedRange(scene.readback)));
    }
    renderer.destroyMesh(ground);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("orthographic grids use parallel rays and all three axis colours", "[renderer][gpu][grid]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    GpuScene scene{*device, renderer, {128, 128}};
    scene.grid = true;
    SceneView view;
    view.camera.projectionKind = ProjectionKind::Orthographic;
    view.camera.orthoHeight = 8.0f;
    for (const glm::vec3 normal : {glm::vec3{0, 1, 0}, glm::vec3{0, 0, 1}, glm::vec3{1, 0, 0}}) {
      scene.gridPlane = glm::vec4{normal, 0};
      scene.gridAxisU = normal.x == 1 ? glm::vec3{0, 0, 1} : glm::vec3{1, 0, 0};
      for (const float sign : {1.0f, -1.0f}) {
        CAPTURE(normal.x, normal.y, normal.z, sign);
        view.camera.position = normal * sign * 10.0f;
        const auto up = normal.y == 1 ? glm::vec3{0, 0, -sign} : glm::vec3{0, 1, 0};
        view.camera.rotation = glm::quatLookAt(-normal * sign, up);
        scene.render(view);
        int red = 0, green = 0, blue = 0;
        for (std::uint32_t y = 0; y < 128; ++y) {
          for (std::uint32_t x = 0; x < 128; ++x) {
            const auto p = scene.pixel(x, y);
            red += p.r > p.g * 2 && p.r > p.b * 2;
            green += p.g > p.r * 2 && p.g > p.b * 2;
            blue += p.b > p.r * 2 && p.b > p.g * 2;
          }
        }
        REQUIRE((red > 100) == (normal.x == 0));
        REQUIRE((green > 100) == (normal.y == 0));
        REQUIRE((blue > 100) == (normal.z == 0));
        const auto bytes = device->mappedRange(scene.readback);
        const std::vector<std::byte> first{bytes.begin(), bytes.end()};
        // Orthographic spacing and opacity depend on height, never camera distance.
        view.camera.position *= 20.0f;
        scene.render(view);
        REQUIRE(std::ranges::equal(first, device->mappedRange(scene.readback)));
        view.camera.orthoHeight = 80.0f;
        scene.render(view);
        REQUIRE_FALSE(std::ranges::equal(first, device->mappedRange(scene.readback)));
        view.camera.orthoHeight = 8.0f;
      }
    }
    // A coplanar surface must own its depth for orthographic projection too.
    const auto ground = renderer.createMesh(primitives::plane(), "orthographic grid occluder");
    const std::array draws{DrawItem{.mesh = ground, .transform = glm::scale(glm::mat4{1}, glm::vec3{100})}};
    view.draws = draws;
    view.camera.position = {0, 10, 0};
    view.camera.rotation = glm::quatLookAt(glm::vec3{0, -1, 0}, glm::vec3{0, 0, -1});
    scene.gridPlane = {0, 1, 0, 0};
    scene.gridAxisU = {1, 0, 0};
    scene.grid = false;
    scene.render(view);
    const auto bytes = device->mappedRange(scene.readback);
    const std::vector<std::byte> without{bytes.begin(), bytes.end()};
    scene.grid = true;
    scene.render(view);
    REQUIRE(std::ranges::equal(without, device->mappedRange(scene.readback)));
    renderer.destroyMesh(ground);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("orbiting the grid under TAA matches unjittered rendering", "[renderer][gpu][grid]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    auto settings = testSettings();
    Renderer reference{*device, shaderDir(platform), settings};
    settings.antialiasing = AntiAliasing::Taa;
    Renderer temporal{*device, shaderDir(platform), settings};
    GpuScene plain{*device, reference, {128, 128}};
    GpuScene taa{*device, temporal, {128, 128}};
    plain.grid = taa.grid = true;
    SceneView view;
    for (int frame = 0; frame < 16; ++frame) {
      const float angle = static_cast<float>(frame) * 0.15f;
      view.camera.position = {std::sin(angle) * 6.0f, 4.0f, std::cos(angle) * 6.0f};
      view.camera.rotation = glm::angleAxis(angle, glm::vec3{0, 1, 0}) * glm::angleAxis(-0.55f, glm::vec3{1, 0, 0});
      plain.render(view);
      taa.render(view);
      REQUIRE(std::ranges::equal(device->mappedRange(plain.readback), device->mappedRange(taa.readback)));
    }
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("zero camera bloom strength matches disabled bloom pixels", "[renderer][bloom][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  auto settings = testSettings();
  settings.bloom = true;
  Renderer renderer{*device, shaderDir(platform), settings};
  const auto box = renderer.createMesh(primitives::box(), "box");
  const std::array draws{DrawItem{.mesh = box}};
  auto view = boxScene(draws);
  view.exposure = 0.7f;
  view.bloomStrength = 0.0f;
  GpuScene scene{*device, renderer, {64, 64}};
  scene.render(view);
  REQUIRE_FALSE(hasPass(scene.graph, "bloom down 0"));
  const auto pixels = device->mappedRange(scene.readback);
  const std::vector<std::byte> zeroStrength(pixels.begin(), pixels.end());
  settings.bloom = false;
  renderer.setSettings(settings);
  view.bloomStrength = 0.8f;
  scene.render(view);
  const auto disabled = device->mappedRange(scene.readback);
  REQUIRE(std::ranges::equal(zeroStrength, disabled));
  settings.bloom = true;
  renderer.setSettings(settings);
  scene.render(view);
  REQUIRE(hasPass(scene.graph, "bloom down 0"));
  REQUIRE_FALSE(std::ranges::equal(zeroStrength, device->mappedRange(scene.readback)));
  renderer.destroyMesh(box);
}

TEST_CASE("material sampler cache shares descriptions and falls back at capacity", "[renderer][null][sampler]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  MaterialDesc desc;
  desc.baseColorSampler.magFilter = TextureFilter::Nearest;
  const auto first = renderer.createMaterial(desc, "first");
  const auto probe = device->createSampler({});
  const auto before = device->samplerIndex(probe);
  device->destroySampler(probe);
  desc.normalSampler = desc.baseColorSampler;
  const auto second = renderer.createMaterial(desc, "second");
  renderer.updateMaterial(first, desc);
  auto next = device->createSampler({});
  REQUIRE(device->samplerIndex(next) == before + 1);
  device->destroySampler(next);
  for (int s = 0; s < 3; ++s)
    for (int t = 0; t < 3; ++t)
      for (int mag = 0; mag < 2; ++mag)
        for (int min = 0; min < 2; ++min)
          for (int mip = 0; mip < 3; ++mip) {
            desc.baseColorSampler = {static_cast<TextureWrap>(s), static_cast<TextureWrap>(t),
                                     static_cast<TextureFilter>(mag), static_cast<TextureFilter>(min),
                                     static_cast<TextureMipFilter>(mip)};
            renderer.updateMaterial(first, desc);
          }
  next = device->createSampler({});
  // 63 cached material samplers, one linear clamp, and two probes consumed indices.
  REQUIRE(device->samplerIndex(next) == MaxBindlessSamplers + 2);
  device->destroySampler(next);
  renderer.destroyMaterial(second);
  renderer.destroyMaterial(first);
}

TEST_CASE("nearest material sampling preserves hard edges of an eight texel checker", "[renderer][gpu][sampler]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = gpuDevice(platform);
  auto settings = testSettings();
  settings.shadows = false;
  settings.debugView = DebugView::Albedo;
  Renderer renderer{*device, shaderDir(platform), settings};
  TextureData texture{.size = {8, 8}, .format = Format::R8G8B8A8Unorm, .data = {}};
  texture.data.resize(static_cast<std::size_t>(texture.expectedSize()));
  for (unsigned y = 0; y < 8; ++y)
    for (unsigned x = 0; x < 8; ++x) {
      const auto offset = (y * 8 + x) * 4;
      const std::byte value{static_cast<unsigned char>((x + y) % 2 ? 255 : 0)};
      texture.data[offset] = texture.data[offset + 1] = texture.data[offset + 2] = value;
      texture.data[offset + 3] = std::byte{255};
    }
  const auto image = renderer.createTexture(texture, "checker");
  // A generated, front-facing quad, independent of third-party model files.
  MeshData quad;
  quad.vertices = {{{-1, -1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {0, 0}},
                   {{1, -1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {1, 0}},
                   {{-1, 1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {0, 1}},
                   {{1, 1, 0}, {0, 0, 1}, {1, 0, 0, 1}, {1, 1}}};
  quad.indices = {0, 1, 2, 2, 1, 3};
  const auto mesh = renderer.createMesh(quad, "checker quad");
  MaterialDesc desc;
  desc.baseColorTexture = image;
  desc.baseColorSampler = {
      .magFilter = TextureFilter::Nearest, .minFilter = TextureFilter::Nearest, .mipFilter = TextureMipFilter::None};
  const auto material = renderer.createMaterial(desc, "nearest checker");
  const std::array draws{DrawItem{.mesh = mesh, .material = material}};
  GpuScene scene{*device, renderer, {128, 128}};
  scene.render(boxScene(draws), 3);
  int black = 0, white = 0;
  for (unsigned y = 40; y < 88; ++y)
    for (unsigned x = 40; x < 88; ++x) {
      const auto pixel = scene.pixel(x, y);
      // ACES followed by display encoding maps unit white to about 231.
      REQUIRE((pixel.r <= 2 || (pixel.r >= 229 && pixel.r <= 232)));
      if (pixel.r <= 2)
        ++black;
      else
        ++white;
    }
  REQUIRE(black > 500);
  REQUIRE(white > 500);
  desc.baseColorSampler = {};
  renderer.updateMaterial(material, desc);
  scene.render(boxScene(draws));
  int interpolated = 0;
  for (unsigned y = 40; y < 88; ++y)
    for (unsigned x = 40; x < 88; ++x) {
      const auto p = scene.pixel(x, y);
      if (p.r > 2 && p.r < 229)
        ++interpolated;
    }
  REQUIRE(interpolated > 500);
  settings.debugView = DebugView::Final;
  renderer.setSettings(settings);
  desc.baseColor = glm::vec4{0.0f};
  desc.baseColorTexture = {};
  desc.unlit = true;
  desc.emissive = glm::vec3{1.0f};
  desc.emissiveTexture = image;
  desc.emissiveSampler = {
      .magFilter = TextureFilter::Nearest, .minFilter = TextureFilter::Nearest, .mipFilter = TextureMipFilter::None};
  renderer.updateMaterial(material, desc);
  scene.render(boxScene(draws));
  for (unsigned y = 40; y < 88; ++y)
    for (unsigned x = 40; x < 88; ++x) {
      const auto p = scene.pixel(x, y);
      REQUIRE((p.r <= 2 || (p.r >= 229 && p.r <= 232)));
    }
  // Fill the cache, then ask for a description beyond its capacity. It must sample
  // with the default linear sampler, including on the real fixed-size bindless array.
  for (int s = 0; s < 3; ++s)
    for (int t = 0; t < 3; ++t)
      for (int mag = 0; mag < 2; ++mag)
        for (int min = 0; min < 2; ++min)
          for (int mip = 0; mip < 3; ++mip) {
            desc.emissiveSampler = {static_cast<TextureWrap>(s), static_cast<TextureWrap>(t),
                                    static_cast<TextureFilter>(mag), static_cast<TextureFilter>(min),
                                    static_cast<TextureMipFilter>(mip)};
            renderer.updateMaterial(material, desc);
          }
  desc.emissiveSampler = {.wrapS = TextureWrap::MirroredRepeat,
                          .wrapT = TextureWrap::MirroredRepeat,
                          .magFilter = TextureFilter::Nearest,
                          .minFilter = TextureFilter::Nearest,
                          .mipFilter = TextureMipFilter::None};
  renderer.updateMaterial(material, desc);
  scene.render(boxScene(draws));
  interpolated = 0;
  for (unsigned y = 40; y < 88; ++y)
    for (unsigned x = 40; x < 88; ++x) {
      const auto p = scene.pixel(x, y);
      if (p.r > 2 && p.r < 229)
        ++interpolated;
    }
  REQUIRE(interpolated > 500);
  renderer.destroyMaterial(material);
  renderer.destroyMesh(mesh);
  renderer.destroyTexture(image);
}

TEST_CASE("dolly zoom keeps the sun shadow range at the scene", "[renderer][gpu][shadow][smooth-view]") {
  sonnet::platform::Platform platform{{.headless = true}};
  auto device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const auto plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const auto wall = renderer.createMesh(primitives::plane({2.0f, 4.0f}), "wall");
    const auto missing = wallShadow(*device, renderer, plane, wall, {}, 200.0f, false);
    const auto shifted = wallShadow(*device, renderer, plane, wall, {}, 200.0f, true);
    REQUIRE(missing.shadowed > shifted.shadowed + 30.0f);
    REQUIRE(shifted.lit > shifted.shadowed + 30.0f);
    REQUIRE(std::abs(shifted.lit - missing.lit) < 5.0f);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(wall);
  }
  device->waitIdle();
  REQUIRE(device->validationMessageCount() == 0);
}

// ---- Transmission, part 1 (ADR-0033): the material model and the blended path ----

namespace {

// A front-facing two-metre quad at depth `z`, its second UV set the first one's.
MeshData facingQuad(float z) {
  MeshData quad;
  quad.vertices = {{{-1, -1, z}, {0, 0, 1}, {1, 0, 0, 1}, {0, 0}, {0, 0}},
                   {{1, -1, z}, {0, 0, 1}, {1, 0, 0, 1}, {1, 0}, {1, 0}},
                   {{-1, 1, z}, {0, 0, 1}, {1, 0, 0, 1}, {0, 1}, {0, 1}},
                   {{1, 1, z}, {0, 0, 1}, {1, 0, 0, 1}, {1, 1}, {1, 1}}};
  quad.indices = {0, 1, 2, 2, 1, 3};
  return quad;
}

// A lit grey dielectric, roughness 1 so the highlight is small.
MaterialDesc dielectric() {
  MaterialDesc desc;
  desc.baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
  desc.metallic = 0.0f;
  desc.roughness = 1.0f;
  return desc;
}

// What the tonemap pass writes for a linear radiance at exposure 1, in 8-bit levels (post.slang).
float encodedChannel(float x) {
  const float mapped = std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
  return 255.0f * std::pow(mapped, 1.0f / 2.2f);
}
int encodedLevel(float x) {
  return static_cast<int>(std::lround(encodedChannel(x)));
}

} // namespace

TEST_CASE("a material that may transmit draws blended and one that provably cannot keeps its path",
          "[renderer][null][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  struct Case {
    const char *name;
    MaterialDesc desc;
    bool transmits;
  };
  MaterialDesc glass = dielectric();
  glass.transmission = 0.5f;
  MaterialDesc metal = glass;
  metal.metallic = 1.0f; // and no metallic-roughness texture: T = 0 everywhere
  MaterialDesc texturedMetal = metal;
  MaterialDesc unlit = glass;
  unlit.unlit = true;
  MaterialDesc none = dielectric();
  MaterialDesc masked = glass;
  masked.alphaMode = AlphaMode::Mask;
  std::vector<Case> cases{
      {"dielectric", glass, true}, {"metal", metal, false},         {"textured metal", texturedMetal, true},
      {"unlit", unlit, false},     {"transmission 0", none, false}, {"masked", masked, true}};
  for (Case &c : cases) {
    CAPTURE(c.name);
    const auto device = createNullDevice();
    auto settings = testSettings();
    settings.antialiasing = AntiAliasing::Taa; // the pre-pass then writes motion too
    Renderer renderer{*device, shaderDir(platform), settings};
    if (std::string_view{c.name} == "textured metal") {
      // Conservative: the CPU does not read the texture that decides T.
      c.desc.metallicRoughnessTexture = renderer.createTexture(solidTexture({255, 255, 255, 255}), "white");
    }
    REQUIRE(mayTransmit(c.desc) == c.transmits);
    const MeshHandle mesh = renderer.createMesh(facingQuad(0.0f), "quad");
    const MaterialHandle material = renderer.createMaterial(c.desc, c.name);
    const std::array draws{DrawItem{.mesh = mesh, .material = material}};
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize({64, 64});
    const SceneView view = boxScene(draws); // the passes keep a pointer to it until execute
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    // On the transmission path: blended, out of the depth and motion pre-pass. Otherwise its own path.
    REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == (c.transmits ? 1u : 0u));
    REQUIRE(countLines(*device, "bindPipeline \"depth motion\"") == (c.transmits ? 0u : 1u));
  }
}

TEST_CASE("with no copy, a dielectric at half transmission is the lit material blended at half coverage on a GPU",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.shadows = false;
    settings.transmission = false;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle mesh = renderer.createMesh(facingQuad(0.0f), "quad");
    MaterialDesc glassDesc = dielectric();
    glassDesc.transmission = 0.5f;
    MaterialDesc halfDesc = dielectric();
    halfDesc.alphaMode = AlphaMode::Blend;
    halfDesc.baseColor.a = 0.5f;
    const MaterialHandle glass = renderer.createMaterial(glassDesc, "glass");
    const MaterialHandle half = renderer.createMaterial(halfDesc, "half");
    const MaterialHandle solid = renderer.createMaterial(dielectric(), "solid");
    const EnvironmentHandle sky = renderer.createEnvironment(skyTexture({0.4f, 0.5f, 0.8f}), "sky");

    // Each diffuse term on its own: the sun, the flat ambient, the environment's irradiance.
    enum class Light {
      Direct,
      Ambient,
      Environment
    };
    for (const Light light : {Light::Direct, Light::Ambient, Light::Environment}) {
      CAPTURE(static_cast<int>(light));
      const auto centre = [&](MaterialHandle material) {
        const std::array draws{DrawItem{.mesh = mesh, .material = material}};
        SceneView view = boxScene(draws);
        view.hasSun = light == Light::Direct;
        view.sun.direction = glm::normalize(glm::vec3{0.3f, -0.2f, -1.0f});
        view.sun.intensity = 2.0f;
        view.ambient = light == Light::Ambient ? glm::vec3{0.6f} : glm::vec3{0.0f};
        view.environment = light == Light::Environment ? sky : EnvironmentHandle{};
        GpuScene scene{*device, renderer, {64, 64}};
        scene.render(view, 2); // the environment is ready from the second frame
        return scene.pixel(32, 32);
      };
      const Pixel transmissive = centre(glass);
      const Pixel blended = centre(half);
      const Pixel opaque = centre(solid);
      CAPTURE(transmissive.r, blended.r, opaque.r);
      REQUIRE(std::abs(transmissive.r - blended.r) <= 1);
      REQUIRE(std::abs(transmissive.g - blended.g) <= 1);
      REQUIRE(std::abs(transmissive.b - blended.b) <= 1);
      // Blended, not drawn opaque: darker over the black clear colour, brighter over the sky.
      REQUIRE(std::abs(opaque.r - transmissive.r) > 5);
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyEnvironment(sky);
    for (const MaterialHandle material : {glass, half, solid}) {
      renderer.destroyMaterial(material);
    }
    renderer.destroyMesh(mesh);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("transmission coverage is a times 1 - T in every alpha mode, shadows included, on a GPU",
          "[renderer][gpu][transmission][shadow]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle quad = renderer.createMesh(facingQuad(0.0f), "quad");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const MeshHandle wall = renderer.createMesh(primitives::plane({2.0f, 4.0f}), "wall");
    const auto make = [&](AlphaMode mode, float alpha, float transmission, float metallic = 0.0f) {
      MaterialDesc desc = dielectric();
      desc.alphaMode = mode;
      desc.baseColor.a = alpha;
      desc.transmission = transmission;
      desc.metallic = metallic;
      desc.doubleSided = true; // the wall is seen by the sun from behind
      return renderer.createMaterial(desc, "material");
    };
    // Ordinary blended materials of a known coverage are the references.
    const MaterialHandle blend25 = make(AlphaMode::Blend, 0.25f, 0.0f);
    const MaterialHandle blend50 = make(AlphaMode::Blend, 0.5f, 0.0f);
    const MaterialHandle clear = make(AlphaMode::Blend, 0.0f, 0.0f);
    const MaterialHandle solid = make(AlphaMode::Opaque, 1.0f, 0.0f);
    struct Case {
      const char *name;
      MaterialHandle material;
      MaterialHandle reference;
    };
    const std::vector<Case> cases{
        {"opaque, t 0.5", make(AlphaMode::Opaque, 1.0f, 0.5f), blend50},
        {"blend 0.5, t 0.5", make(AlphaMode::Blend, 0.5f, 0.5f), blend25},
        {"mask passed, t 0.5", make(AlphaMode::Mask, 1.0f, 0.5f), blend50},
        {"mask cut, t 0.5", make(AlphaMode::Mask, 0.3f, 0.5f), clear},
        {"opaque, t 1", make(AlphaMode::Opaque, 1.0f, 1.0f), clear},
        {"metal, t 1", make(AlphaMode::Opaque, 1.0f, 1.0f, 1.0f), solid},
    };
    const auto lit = [&](MaterialHandle material) {
      const std::array draws{DrawItem{.mesh = quad, .material = material}};
      SceneView view = boxScene(draws);
      view.hasSun = false;
      view.ambient = glm::vec3{0.6f};
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      return scene.pixel(32, 32);
    };
    for (const Case &c : cases) {
      CAPTURE(c.name);
      const Pixel got = lit(c.material);
      const Pixel want = lit(c.reference);
      CAPTURE(got.r, want.r);
      REQUIRE(std::abs(got.r - want.r) <= 1);
      // The shadow takes the same coverage through the same hashed test, pixel for pixel.
      const WallShadow shadow = wallShadow(*device, renderer, plane, wall, c.material);
      const WallShadow expected = wallShadow(*device, renderer, plane, wall, c.reference);
      CAPTURE(shadow.shadowed, expected.shadowed);
      REQUIRE(shadow.shadowed == Catch::Approx(expected.shadowed).margin(0.5));
      REQUIRE(shadow.lit == Catch::Approx(expected.lit).margin(0.5));
    }
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a provably non-transmitting material renders as it does without transmission on a GPU",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    // Transmission 0 and 1 against metallic 0 and 1: of the four, only the metal at transmission 1
    // has a nonzero factor and still renders as without it.
    for (const float metallic : {0.0f, 1.0f}) {
      MaterialDesc plain = dielectric();
      plain.metallic = metallic;
      plain.roughness = 0.4f;
      MaterialDesc transmissive = plain;
      transmissive.transmission = 1.0f;
      const MaterialHandle without = renderer.createMaterial(plain, "without");
      const MaterialHandle with = renderer.createMaterial(transmissive, "with");
      const EnvironmentHandle sky = renderer.createEnvironment(skyTexture({0.4f, 0.5f, 0.8f}), "sky");
      const auto render = [&](MaterialHandle material) {
        const std::array draws{DrawItem{.mesh = box, .material = material}};
        SceneView view = boxScene(draws);
        view.environment = sky;
        view.sun.direction = glm::normalize(glm::vec3{0.3f, -0.6f, -1.0f});
        GpuScene scene{*device, renderer, {64, 64}};
        scene.render(view, 2);
        std::vector<Pixel> pixels;
        for (unsigned y = 16; y < 48; y += 4) {
          for (unsigned x = 16; x < 48; x += 4) {
            pixels.push_back(scene.pixel(x, y));
          }
        }
        return pixels;
      };
      const std::vector<Pixel> a = render(without);
      const std::vector<Pixel> b = render(with);
      CAPTURE(metallic);
      int largest = 0;
      for (std::size_t i = 0; i < a.size(); ++i) {
        largest = std::max({largest, std::abs(a[i].r - b[i].r), std::abs(a[i].g - b[i].g), std::abs(a[i].b - b[i].b)});
      }
      CAPTURE(largest);
      if (metallic == 1.0f) {
        REQUIRE(largest == 0); // the same path, the same pixels
      } else {
        REQUIRE(largest > 20); // a clear dielectric lets the sky through
      }
      renderer.destroyEnvironment(sky);
      renderer.destroyMaterial(without);
      renderer.destroyMaterial(with);
    }
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a transmission texture on the second UV set, transformed and nearest, decides the transmission on a GPU",
          "[renderer][gpu][transmission][sampler]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.shadows = false;
    Renderer renderer{*device, shaderDir(platform), settings};
    // Black on the left, white on the right.
    TextureData halves{.size = {2, 1}, .format = Format::R8G8B8A8Unorm, .data = {}};
    halves.data.resize(static_cast<std::size_t>(halves.expectedSize()));
    for (std::size_t i = 0; i < 4; ++i) {
      halves.data[i] = std::byte{i == 3 ? std::uint8_t{255} : std::uint8_t{0}};
      halves.data[4 + i] = std::byte{255};
    }
    const TextureHandle mask = renderer.createTexture(halves, "halves");
    // The first UV set is one point, on the white half: a slot that read it would be clear all over.
    MeshData glassQuad = facingQuad(0.0f);
    for (Vertex &vertex : glassQuad.vertices) {
      vertex.uv = {0.75f, 0.5f};
    }
    const MeshHandle glassMesh = renderer.createMesh(glassQuad, "glass quad");
    const MeshHandle backMesh = renderer.createMesh(facingQuad(-1.0f), "back quad");
    MaterialDesc glassDesc = dielectric();
    glassDesc.transmission = 1.0f;
    glassDesc.transmissionTexture = mask;
    // Mirrored, so the left of the quad reads the white half: u' = 1 - u.
    glassDesc.transmissionTransform = {.offset = {1.0f, 0.0f}, .rotation = 0.0f, .scale = {-1.0f, 1.0f}, .texCoord = 1};
    glassDesc.transmissionSampler = {.wrapS = TextureWrap::ClampToEdge,
                                     .wrapT = TextureWrap::ClampToEdge,
                                     .magFilter = TextureFilter::Nearest,
                                     .minFilter = TextureFilter::Nearest,
                                     .mipFilter = TextureMipFilter::None};
    MaterialDesc backDesc;
    backDesc.baseColor = {1.0f, 0.0f, 0.0f, 1.0f};
    backDesc.unlit = true;
    const MaterialHandle glass = renderer.createMaterial(glassDesc, "glass");
    const MaterialHandle back = renderer.createMaterial(backDesc, "back");
    const std::array draws{
        DrawItem{.mesh = backMesh, .material = back, .transform = glm::scale(glm::mat4{1.0f}, {3.0f, 3.0f, 1.0f})},
        DrawItem{.mesh = glassMesh, .material = glass}};
    SceneView view = boxScene(draws);
    view.hasSun = false;
    view.ambient = glm::vec3{0.6f};
    GpuScene scene{*device, renderer, {128, 128}};
    scene.render(view);
    const Pixel background = scene.pixel(4, 64);
    REQUIRE(background.r > 200);
    REQUIRE(background.g == 0);
    // The quad spans about 40 to 88. Its left half lets everything through: the red behind, through
    // the white-point 0.8 base colour and 0.96 of the Fresnel, even at u = 0.4, where a linear filter
    // would mix in the black texel. Its right half lets nothing through: the grey glass, without red.
    for (const unsigned x : {44u, 52u, 60u}) {
      CAPTURE(x);
      const Pixel through = scene.pixel(x, 64);
      REQUIRE(std::abs(through.r - encodedLevel(0.8f * 0.96f)) <= 2);
      REQUIRE(through.g == 0);
    }
    for (const unsigned x : {68u, 76u, 84u}) {
      CAPTURE(x);
      const Pixel covered = scene.pixel(x, 64);
      REQUIRE(covered.g > 40);
      REQUIRE(std::abs(covered.r - covered.g) <= 2);
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMaterial(glass);
    renderer.destroyMaterial(back);
    renderer.destroyMesh(glassMesh);
    renderer.destroyMesh(backMesh);
    renderer.destroyTexture(mask);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// ---- Transmission, part 2 (ADR-0033): the copy, the split forward pass and the shading ----

namespace {

// A lit two-metre-square front-facing pane's material: white, clear, no light of its own.
MaterialDesc clearPane(float roughness) {
  MaterialDesc desc;
  desc.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  desc.metallic = 0.0f;
  desc.roughness = roughness;
  desc.transmission = 1.0f;
  return desc;
}

MaterialDesc unlitColor(glm::vec3 color) {
  MaterialDesc desc;
  desc.baseColor = {color, 1.0f};
  desc.unlit = true;
  return desc;
}

// What a graph for one view declares: its pass names, in order, and its transient images.
struct GraphTrace {
  std::vector<std::string> passes;
  std::vector<std::string> images;
};

GraphTrace traceScene(const SceneView &view, Renderer &renderer, IDevice &device, glm::uvec2 size = {64, 64}) {
  RenderGraph graph{device};
  RenderTarget target{device, "viewport"};
  target.resize(size);
  ICommandList &commands = device.beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device.endFrame();
  GraphTrace trace;
  for (const PassTiming &pass : graph.statistics().passes) {
    trace.passes.push_back(pass.name);
  }
  return trace;
}

bool hasName(const std::vector<std::string> &names, std::string_view prefix) {
  return std::ranges::any_of(names, [&](const std::string &name) { return name.starts_with(prefix); });
}

} // namespace

TEST_CASE("a view with a draw on the transmission path splits its forward pass around a mip chain of the opaque scene",
          "[renderer][null][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RendererSettings settings = testSettings();
  Renderer renderer{*device, shaderDir(platform), settings};
  const MeshHandle quad = renderer.createMesh(facingQuad(0.0f), "quad");
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  MaterialDesc glassDesc = dielectric();
  glassDesc.transmission = 0.5f;
  const MaterialHandle glass = renderer.createMaterial(glassDesc, "glass");
  const MaterialHandle plain = renderer.createMaterial(dielectric(), "plain");
  const std::array draws{DrawItem{.mesh = box, .material = plain}, DrawItem{.mesh = quad, .material = glass}};
  const SceneView view = boxScene(draws);
  const GraphTrace trace = traceScene(view, renderer, *device);
  const auto at = [&](std::string_view name) { return std::ranges::find(trace.passes, name) - trace.passes.begin(); };
  // 64 x 64 has seven levels: the copy is level 0, then one pass for each of the six below it.
  REQUIRE(std::ranges::count(trace.passes, "forward") == 1);
  REQUIRE(std::ranges::count(trace.passes, "transmission copy") == 1);
  REQUIRE(std::ranges::count(trace.passes, "forward transparent") == 1);
  for (int level = 1; level < 7; ++level) {
    REQUIRE(std::ranges::count(trace.passes, std::format("transmission mip {}", level)) == 1);
  }
  REQUIRE_FALSE(hasName(trace.passes, "transmission mip 7"));
  REQUIRE(at("forward") < at("transmission copy"));
  REQUIRE(at("transmission copy") < at("transmission mip 1"));
  REQUIRE(at("transmission mip 6") < at("forward transparent"));
  REQUIRE(at("forward transparent") < at("tonemap"));
  // Opaque and the sky before the copy, the glass after it: one blended draw, in the second pass.
  REQUIRE(lineIndex(*device, "bindPipeline \"forward blend\"") > lineIndex(*device, "dispatch"));
  REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == 1);

  // The setting off: the one forward pass and nothing else, with the same glass drawn blended.
  settings.transmission = false;
  Renderer off{*device, shaderDir(platform), settings};
  const MeshHandle offQuad = off.createMesh(facingQuad(0.0f), "quad");
  const MaterialHandle offGlass = off.createMaterial(glassDesc, "glass");
  const std::array offDraws{DrawItem{.mesh = offQuad, .material = offGlass}};
  const SceneView offView = boxScene(offDraws);
  const GraphTrace offTrace = traceScene(offView, off, *device);
  REQUIRE(std::ranges::count(offTrace.passes, "forward") == 1);
  REQUIRE_FALSE(hasName(offTrace.passes, "transmission"));
  REQUIRE_FALSE(hasName(offTrace.passes, "forward transparent"));
  off.destroyMaterial(offGlass);
  off.destroyMesh(offQuad);
  renderer.destroyMaterial(plain);
  renderer.destroyMaterial(glass);
  renderer.destroyMesh(box);
  renderer.destroyMesh(quad);
}

TEST_CASE("a view with nothing that can transmit declares no copy, even with a transmission factor on a metal",
          "[renderer][null][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  const MeshHandle quad = renderer.createMesh(facingQuad(0.0f), "quad");
  MaterialDesc metalDesc = dielectric();
  metalDesc.metallic = 1.0f;
  metalDesc.transmission = 1.0f; // provably T = 0: no metallic-roughness texture
  MaterialDesc unlit = unlitColor({1.0f, 0.0f, 0.0f});
  unlit.transmission = 1.0f;
  const MaterialHandle metal = renderer.createMaterial(metalDesc, "metal");
  const MaterialHandle unlitGlass = renderer.createMaterial(unlit, "unlit");
  const MaterialHandle none = renderer.createMaterial(dielectric(), "none");
  for (const MaterialHandle material : {metal, unlitGlass, none}) {
    const std::array draws{DrawItem{.mesh = quad, .material = material}};
    const SceneView view = boxScene(draws);
    const GraphTrace trace = traceScene(view, renderer, *device);
    REQUIRE(std::ranges::count(trace.passes, "forward") == 1);
    REQUIRE_FALSE(hasName(trace.passes, "transmission"));
    REQUIRE_FALSE(hasName(trace.passes, "forward transparent"));
  }
  // The one-view-per-graph trace above ends with an empty scene too.
  const SceneView empty = boxScene({});
  REQUIRE_FALSE(hasName(traceScene(empty, renderer, *device).passes, "transmission"));
  renderer.destroyMaterial(none);
  renderer.destroyMaterial(unlitGlass);
  renderer.destroyMaterial(metal);
  renderer.destroyMesh(quad);
}

TEST_CASE("the renderer enables transmission by default, as the editor and desktop player take it",
          "[renderer][null][transmission]") {
  REQUIRE(RendererSettings{}.transmission);
}

namespace {

// Two unlit halves with a pane in front of them, seen orthographically along the pane's normal, so
// every pane pixel has the view angle 0. The halves meet at the middle of the longest dimension:
// `a` is on the left of a landscape view and below the middle of a portrait one.
struct PatternScene {
  Renderer &renderer;
  MeshHandle quad;
  MaterialHandle halfA;
  MaterialHandle halfB;
  MaterialHandle pane;
  std::array<DrawItem, 3> draws;
  SceneView view;

  PatternScene(Renderer &r, glm::uvec2 size, const MaterialDesc &paneDesc, glm::vec3 a, glm::vec3 b) : renderer(r) {
    quad = renderer.createMesh(facingQuad(0.0f), "pattern quad");
    halfA = renderer.createMaterial(unlitColor(a), "pattern a");
    halfB = renderer.createMaterial(unlitColor(b), "pattern b");
    pane = renderer.createMaterial(paneDesc, "pane");
    const float height = 4.0f;
    const float width = height * static_cast<float>(size.x) / static_cast<float>(size.y);
    // A half is a quad whose half size is a quarter of the long axis (plus a margin, so it covers the
    // view's corners) centred on that quarter point, so the two meet at the middle.
    const auto half = [&](float side) {
      const bool landscape = size.x >= size.y;
      const float quarter = (landscape ? width : height) * 0.25f;
      const glm::vec3 scale =
          landscape ? glm::vec3{quarter, height * 0.5f + 1.0f, 1.0f} : glm::vec3{width * 0.5f + 1.0f, quarter, 1.0f};
      const glm::vec3 shift =
          landscape ? glm::vec3{side * quarter, 0.0f, -1.0f} : glm::vec3{0.0f, side * quarter, -1.0f};
      return glm::scale(glm::translate(glm::mat4{1.0f}, shift), scale);
    };
    draws = {DrawItem{.mesh = quad, .material = halfA, .transform = half(-1.0f)},
             DrawItem{.mesh = quad, .material = halfB, .transform = half(1.0f)},
             DrawItem{.mesh = quad,
                      .material = pane,
                      .transform = glm::scale(glm::mat4{1.0f}, {width * 0.5f + 1.0f, height * 0.5f + 1.0f, 1.0f})}};
    view = boxScene(draws);
    view.camera.projectionKind = ProjectionKind::Orthographic;
    view.camera.orthoHeight = height;
    view.hasSun = false;
    view.ambient = glm::vec3{0.0f};
  }
  ~PatternScene() {
    renderer.destroyMaterial(pane);
    renderer.destroyMaterial(halfB);
    renderer.destroyMaterial(halfA);
    renderer.destroyMesh(quad);
  }
  PatternScene(const PatternScene &) = delete;
  PatternScene &operator=(const PatternScene &) = delete;

  // The pixel `offset` pixels from the middle of the long axis towards `a` (negative) or `b`.
  [[nodiscard]] static glm::uvec2 across(glm::uvec2 size, int offset) {
    return size.x >= size.y ? glm::uvec2{static_cast<unsigned>(static_cast<int>(size.x / 2) + offset), size.y / 2}
                            : glm::uvec2{size.x / 2, static_cast<unsigned>(static_cast<int>(size.y / 2) - offset)};
  }
};

} // namespace

TEST_CASE("a clear pane over a pattern shows 0.96 of it, and the blur takes the contrast away with roughness",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::vec3 a{0.1f, 0.4f, 0.9f};
    const glm::vec3 b{1.0f, 0.7f, 0.2f};
    const glm::uvec2 size{128, 64};
    const auto sample = [&](float roughness) {
      PatternScene scene{renderer, size, clearPane(roughness), a, b};
      GpuScene gpu{*device, renderer, size};
      gpu.render(scene.view);
      const glm::uvec2 pa = PatternScene::across(size, -8);
      const glm::uvec2 pb = PatternScene::across(size, 8);
      return std::pair{gpu.pixel(pa.x, pa.y), gpu.pixel(pb.x, pb.y)};
    };
    // Roughness 0 reads level 0 exactly: the pattern, 0.96 of it, at the same texel.
    const auto [clearA, clearB] = sample(0.0f);
    CAPTURE(clearA.r, clearA.g, clearA.b, clearB.r, clearB.g, clearB.b);
    REQUIRE(std::abs(clearA.r - encodedLevel(0.96f * a.r)) <= 2);
    REQUIRE(std::abs(clearA.g - encodedLevel(0.96f * a.g)) <= 2);
    REQUIRE(std::abs(clearA.b - encodedLevel(0.96f * a.b)) <= 2);
    REQUIRE(std::abs(clearB.r - encodedLevel(0.96f * b.r)) <= 2);
    REQUIRE(std::abs(clearB.g - encodedLevel(0.96f * b.g)) <= 2);
    REQUIRE(std::abs(clearB.b - encodedLevel(0.96f * b.b)) <= 2);
    // The contrast between the halves only falls as the glass roughens, and at 1 the last level, the
    // mean of the whole frame, leaves none: the same pixel either side of the edge.
    int previous = std::abs(clearA.r - clearB.r);
    REQUIRE(previous > 100);
    for (const float roughness : {0.5f, 0.95f, 1.0f}) {
      CAPTURE(roughness);
      const auto [pa, pb] = sample(roughness);
      const int contrast = std::abs(pa.r - pb.r);
      CAPTURE(contrast, previous);
      REQUIRE(contrast <= previous + 1);
      previous = contrast;
    }
    REQUIRE(previous <= 2);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the blur follows the roughness the same way across view sizes, landscape and portrait",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::vec3 a{0.05f, 0.05f, 0.05f};
    const glm::vec3 b{1.0f, 1.0f, 1.0f};
    // The contrast between the halves, a quarter of the long axis either side of their edge, in output
    // levels: the blur of 0.5 has left almost none by then, the blur of 0.95 not all.
    const auto contrast = [&](glm::uvec2 size, float roughness) {
      PatternScene scene{renderer, size, clearPane(roughness), a, b};
      GpuScene gpu{*device, renderer, size};
      gpu.render(scene.view);
      const int quarter = static_cast<int>(std::max(size.x, size.y) / 4);
      const glm::uvec2 pa = PatternScene::across(size, -quarter);
      const glm::uvec2 pb = PatternScene::across(size, quarter);
      return std::abs(gpu.pixel(pb.x, pb.y).g - gpu.pixel(pa.x, pa.y).g);
    };
    const std::array roughnesses{0.0f, 0.5f, 0.95f, 1.0f};
    // The longest dimension through 1023, 1024, 1025 and 2047, 2048, on a strip 32 pixels thick.
    std::vector<std::array<int, 4>> landscape;
    for (const unsigned longest : {1023u, 1024u, 1025u, 2047u, 2048u}) {
      for (const bool portrait : {false, true}) {
        const glm::uvec2 size = portrait ? glm::uvec2{32, longest} : glm::uvec2{longest, 32};
        CAPTURE(size.x, size.y);
        std::array<int, 4> levels{};
        for (std::size_t i = 0; i < roughnesses.size(); ++i) {
          levels[i] = contrast(size, roughnesses[i]);
        }
        CAPTURE(levels[0], levels[1], levels[2], levels[3]);
        // Endpoints: roughness 0 is the sharp pattern, roughness 1 the mean of the frame, the same on
        // both sides of the edge. In between the contrast only falls.
        REQUIRE(levels[0] > 150);
        REQUIRE(levels[3] <= 2);
        for (std::size_t i = 1; i < levels.size(); ++i) {
          REQUIRE(levels[i] <= levels[i - 1] + 1);
        }
        // The blur is mostly spent at 0.5 not yet and at 0.95 not entirely: a view one pixel short of
        // a power of two does not reach its last level early (it measured 19 against 20 levels).
        REQUIRE(levels[1] > 100);
        REQUIRE(levels[2] >= 10);
        if (!portrait) {
          landscape.push_back(levels);
        }
      }
    }
    // Neighbouring sizes keep the contrast within a recorded tolerance, at every roughness.
    constexpr int neighbourTolerance = 3; // levels out of 255; the sweep measured at most 1
    for (const auto &[first, second] : {std::pair{0uz, 1uz}, std::pair{1uz, 2uz}, std::pair{3uz, 4uz}}) {
      for (std::size_t i = 0; i < roughnesses.size(); ++i) {
        CAPTURE(first, second, roughnesses[i], landscape[first][i], landscape[second][i]);
        REQUIRE(std::abs(landscape[first][i] - landscape[second][i]) <= neighbourTolerance);
      }
    }
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a metal behind glass is in the copy, and a metal that transmits nothing lets nothing through on a GPU",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.shadows = false;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle pane = renderer.createMesh(facingQuad(0.0f), "pane");
    // A quad drawn twice over, overlapping triangles: opaque, so the second draw of a pixel is the
    // same pixel again, depth-tested equal.
    MeshData overlapping = facingQuad(-1.0f);
    overlapping.indices = {0, 1, 2, 2, 1, 3, 0, 1, 2};
    const MeshHandle behind = renderer.createMesh(overlapping, "metal quad");
    const MaterialHandle glass = renderer.createMaterial(clearPane(0.0f), "glass");
    MaterialDesc metalDesc;
    metalDesc.baseColor = {1.0f, 0.0f, 0.0f, 1.0f};
    metalDesc.metallic = 1.0f; // and no texture for it: provably T = 0, so the opaque path
    metalDesc.roughness = 1.0f;
    metalDesc.transmission = 1.0f;
    const MaterialHandle metal = renderer.createMaterial(metalDesc, "metal");
    const auto render = [&](std::span<const DrawItem> draws) {
      SceneView view = boxScene(draws);
      view.hasSun = false;
      view.ambient = glm::vec3{0.6f};
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      return scene.pixel(32, 32);
    };
    // The metal alone, then behind the pane: the pane shows the shaded metal through the copy, at
    // 0.96 of it, so the metal was drawn before the copy and kept its depth.
    const std::array alone{DrawItem{.mesh = behind, .material = metal}};
    const std::array covered{DrawItem{.mesh = behind, .material = metal}, DrawItem{.mesh = pane, .material = glass}};
    const Pixel bare = render(alone);
    const Pixel seen = render(covered);
    CAPTURE(bare.r, seen.r);
    REQUIRE(std::abs(bare.r - encodedLevel(0.6f)) <= 2);
    REQUIRE(std::abs(seen.r - encodedLevel(0.96f * 0.6f)) <= 2);
    REQUIRE(seen.g <= 1);

    // A metal whose texture decides: the texture says metal everywhere, so T = 0 and it is drawn
    // exactly as with transmission 0, though the CPU had to keep it on the transmission path.
    TextureData white = solidTexture({255, 255, 255, 255});
    const TextureHandle metalTexture = renderer.createTexture(white, "metal");
    MaterialDesc texturedDesc = metalDesc;
    texturedDesc.metallicRoughnessTexture = metalTexture;
    MaterialDesc referenceDesc = texturedDesc;
    referenceDesc.transmission = 0.0f;
    REQUIRE(mayTransmit(texturedDesc));
    const MaterialHandle textured = renderer.createMaterial(texturedDesc, "textured metal");
    const MaterialHandle reference = renderer.createMaterial(referenceDesc, "reference metal");
    const std::array texturedDraws{DrawItem{.mesh = pane, .material = textured}};
    const std::array referenceDraws{DrawItem{.mesh = pane, .material = reference}};
    const Pixel withTransmission = render(texturedDraws);
    const Pixel without = render(referenceDraws);
    CAPTURE(withTransmission.r, without.r);
    REQUIRE(std::abs(withTransmission.r - without.r) <= 1);
    REQUIRE(std::abs(withTransmission.g - without.g) <= 1);
    REQUIRE(withTransmission.r > 100); // lit red metal, not the clear colour behind it

    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMaterial(reference);
    renderer.destroyMaterial(textured);
    renderer.destroyTexture(metalTexture);
    renderer.destroyMaterial(metal);
    renderer.destroyMaterial(glass);
    renderer.destroyMesh(behind);
    renderer.destroyMesh(pane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("opaque, masked and blended glass over a pattern each take their coverage from the copy on a GPU",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::vec3 behind{0.8f, 0.4f, 0.1f};
    const float albedo = 0.5f;
    const glm::uvec2 size{64, 64};
    const auto centre = [&](AlphaMode mode, float alpha, float cutoff) {
      MaterialDesc desc = clearPane(0.0f);
      desc.baseColor = {albedo, albedo, albedo, alpha};
      desc.alphaMode = mode;
      desc.alphaCutoff = cutoff;
      PatternScene scene{renderer, size, desc, behind, behind};
      GpuScene gpu{*device, renderer, size};
      gpu.render(scene.view);
      return gpu.pixel(32, 32);
    };
    const auto expected = [&](float factor) { return encodedLevel(factor * behind.r); };
    // Transmitted: albedo * (1 - F) of the scene behind.
    const float through = albedo * 0.96f;
    REQUIRE(std::abs(centre(AlphaMode::Opaque, 0.2f, 0.5f).r - expected(through)) <= 2);
    // Masked: a fragment under its cutoff is discarded and shows the scene itself; over it, the glass.
    REQUIRE(std::abs(centre(AlphaMode::Mask, 0.2f, 0.5f).r - expected(1.0f)) <= 2);
    REQUIRE(std::abs(centre(AlphaMode::Mask, 0.8f, 0.5f).r - expected(through)) <= 2);
    // Blended: the authored alpha mixes the glass over the scene, half and half here.
    REQUIRE(std::abs(centre(AlphaMode::Blend, 0.5f, 0.5f).r - expected(0.5f * through + 0.5f)) <= 2);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a transmissive mesh whose triangles overlap on screen draws them in index order, from either side",
          "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    auto settings = testSettings();
    settings.shadows = false;
    Renderer renderer{*device, shaderDir(platform), settings};
    // Two parallel quads, red in front and green behind, the pane's own vertex colour telling them
    // apart; the first quad's triangles come first in the index list or the second's do.
    const auto mesh = [&](bool frontFirst) {
      MeshData data;
      const auto quad = [&](float z, std::uint32_t color, std::uint32_t base) {
        for (const glm::vec2 corner : {glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{-1, 1}, glm::vec2{1, 1}}) {
          Vertex vertex{{corner, z}, {0, 0, 1}, {1, 0, 0, 1}, {0, 0}, {0, 0}};
          vertex.color = color;
          data.vertices.push_back(vertex);
        }
        for (const std::uint32_t index : {0u, 1u, 2u, 2u, 1u, 3u}) {
          data.indices.push_back(base + index);
        }
      };
      constexpr std::uint32_t red = 0xFF0000FFu;
      constexpr std::uint32_t green = 0xFF00FF00u;
      if (frontFirst) {
        quad(0.0f, red, 0);
        quad(-0.5f, green, 4);
      } else {
        quad(-0.5f, green, 0);
        quad(0.0f, red, 4);
      }
      return renderer.createMesh(data, "overlap");
    };
    MaterialDesc desc = clearPane(0.0f);
    desc.doubleSided = true;
    const MaterialHandle glass = renderer.createMaterial(desc, "glass");
    // The glass's vertex colour tints what it transmits, so a white wall stands behind it, in the copy.
    const MeshHandle wallMesh = renderer.createMesh(facingQuad(-2.0f), "wall");
    const MeshHandle farWallMesh = renderer.createMesh(facingQuad(2.0f), "wall seen from behind");
    MaterialDesc wallDesc = unlitColor({1.0f, 1.0f, 1.0f});
    wallDesc.doubleSided = true;
    const MaterialHandle wall = renderer.createMaterial(wallDesc, "wall");
    const auto centre = [&](MeshHandle overlap, bool fromBehind) {
      const std::array draws{DrawItem{.mesh = fromBehind ? farWallMesh : wallMesh,
                                      .material = wall,
                                      .transform = glm::scale(glm::mat4{1.0f}, {4.0f, 4.0f, 1.0f})},
                             DrawItem{.mesh = overlap, .material = glass}};
      SceneView view = boxScene(draws);
      view.hasSun = false;
      view.ambient = glm::vec3{0.6f};
      if (fromBehind) {
        view.camera.position = {0.0f, 0.0f, -3.0f};
        view.camera.rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3{0.0f, 1.0f, 0.0f});
      }
      GpuScene scene{*device, renderer, {64, 64}};
      scene.render(view);
      return scene.pixel(32, 32);
    };
    const MeshHandle frontFirst = mesh(true);
    const MeshHandle backFirst = mesh(false);
    // No depth writes, so the later triangles win whatever their depth: the green behind when it is
    // listed last, the red in front when it is; pinned for either side of the pane.
    for (const bool fromBehind : {false, true}) {
      CAPTURE(fromBehind);
      const Pixel greenLast = centre(frontFirst, fromBehind);
      const Pixel redLast = centre(backFirst, fromBehind);
      CAPTURE(greenLast.r, greenLast.g, redLast.r, redLast.g);
      REQUIRE(greenLast.g > greenLast.r + 20);
      REQUIRE(redLast.r > redLast.g + 20);
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMaterial(wall);
    renderer.destroyMesh(farWallMesh);
    renderer.destroyMesh(wallMesh);
    renderer.destroyMaterial(glass);
    renderer.destroyMesh(backFirst);
    renderer.destroyMesh(frontFirst);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the copy follows each view's own size, an odd one, and a resize on a GPU", "[renderer][gpu][transmission]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), testSettings()};
    const glm::vec3 a{0.1f, 0.4f, 0.9f};
    const glm::vec3 b{1.0f, 0.7f, 0.2f};
    // A Scene view and a Game view of different sizes in the same frames, an odd size, then the
    // first view resized: each pane shows its own pattern at 0.96, nothing of the other view's size.
    for (const glm::uvec2 size : {glm::uvec2{96, 64}, glm::uvec2{1023, 577}, glm::uvec2{64, 48}, glm::uvec2{97, 65}}) {
      CAPTURE(size.x, size.y);
      PatternScene scene{renderer, size, clearPane(0.0f), a, b};
      GpuScene gpu{*device, renderer, size};
      gpu.render(scene.view, 2);
      const int quarter = static_cast<int>(size.x / 4);
      const glm::uvec2 pa = PatternScene::across(size, -quarter);
      const glm::uvec2 pb = PatternScene::across(size, quarter);
      REQUIRE(std::abs(gpu.pixel(pa.x, pa.y).r - encodedLevel(0.96f * a.r)) <= 2);
      REQUIRE(std::abs(gpu.pixel(pb.x, pb.y).r - encodedLevel(0.96f * b.r)) <= 2);
    }
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
