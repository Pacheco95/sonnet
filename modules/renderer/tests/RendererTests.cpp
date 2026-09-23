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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
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
                          .antialiasing = false,
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

struct Pixel {
  int r, g, b, a;
};

Pixel pixelAt(std::span<const std::byte> pixels, glm::uvec2 size, unsigned x, unsigned y) {
  const std::size_t offset = (std::size_t{y} * size.x + x) * 4;
  return {std::to_integer<int>(pixels[offset]), std::to_integer<int>(pixels[offset + 1]),
          std::to_integer<int>(pixels[offset + 2]), std::to_integer<int>(pixels[offset + 3])};
}

// A GPU device, or a skip. `disableDrawIndirectCount` runs it the way MoltenVK does (ADR-0014).
std::unique_ptr<IDevice> gpuDevice(sonnet::platform::Platform &platform, bool disableDrawIndirectCount = false) {
  try {
    return createDevice({.platform = &platform,
                         .applicationName = "renderer_tests",
                         .disableDrawIndirectCount = disableDrawIndirectCount});
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
  // Wall-clock time of each frame's endFrame, where the frame is submitted. MoltenVK encodes the
  // recorded commands into Metal there, one Metal draw per indirect command (ADR-0014), which no
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
      renderer.addDebugLinePass(graph, view, color, depth);
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
  // The opaque draws are submitted from the GPU (ADR-0012): one culling pass ahead of the scene
  // clears the counts and tests the six frusta, and each drawing pass then issues one call per
  // batch. The three draws are two boxes and a sphere sorted by mesh, so two batches each in the
  // four cascades, the pre-pass and the forward pass.
  REQUIRE(hasPass(graph, "cull"));
  REQUIRE(lineIndex(*device, "bindPipeline \"cull\"") < lineIndex(*device, "bindPipeline \"shadow\""));
  REQUIRE(countLines(*device, "bindPipeline \"clear draw counts\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"cull\"") == 1);
  REQUIRE(countLines(*device, "drawIndexedIndirectCount \"draw commands\" max 2") == 6);
  REQUIRE(countLines(*device, "drawIndexedIndirectCount \"draw commands\" max 1") == 6);
  // The trailing space matches only the direct form, not drawIndexedIndirectCount.
  REQUIRE(countLines(*device, "drawIndexed ") == 0); // nothing direct: no blended draws
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

TEST_CASE("without drawIndirectCount every batch draws all its slots and no counts are cleared", "[renderer][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  device->disableDrawIndirectCount(); // as on MoltenVK (ADR-0014)
  Renderer renderer{*device, shaderDir(platform), testSettings()};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({128, 64});
  ICommandList &commands = device->beginFrame();
  const MeshHandle box = renderer.createMesh(primitives::box(), "box");
  const MeshHandle sphere = renderer.createMesh(primitives::sphere(0.5f, 8, 4), "sphere");
  const std::array draws{DrawItem{.mesh = box}, DrawItem{.mesh = sphere}, DrawItem{.mesh = box}};
  const SceneView view = boxScene(draws);
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();

  // Culling still runs, but writes one slot per candidate instead of appending against counts.
  REQUIRE(countLines(*device, "bindPipeline \"cull\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"clear draw counts\"") == 0);
  REQUIRE(countLines(*device, "drawIndexedIndirectCount") == 0);
  // The same two batches in the same six passes, each drawn over its whole range.
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 2") == 6);
  REQUIRE(countLines(*device, "drawIndexedIndirect \"draw commands\" count 1") == 6);
  REQUIRE(renderer.statistics().indirectCallCount == 12);

  renderer.destroyMesh(sphere);
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

  // The cutout box alone goes through the cascades and the pre-pass, with the double-sided
  // pipelines; the blended items only through the blend pipelines, box before sphere.
  REQUIRE(countLines(*device, "bindPipeline \"depth double sided\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"depth\"") == 0);
  REQUIRE(countLines(*device, "bindPipeline \"forward double sided\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"forward blend\"") == 1);
  // The one opaque draw is submitted indirectly, once per cascade plus the pre-pass and the
  // forward pass; the two blended ones stay on the direct path (ADR-0012).
  REQUIRE(countLines(*device, "drawIndexedIndirectCount \"draw commands\" max 1") == 6);
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
  REQUIRE(renderer.statistics().shadowDrawCount == 4);

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
      // levels, and the release of the map.
      REQUIRE(hasPass(graph, "equirect to cube"));
      REQUIRE(hasPass(graph, "cube mip 3"));
      REQUIRE(hasPass(graph, "irradiance"));
      REQUIRE(hasPass(graph, "prefilter 1"));
      REQUIRE(hasPass(graph, "release equirect"));
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
  REQUIRE(countLines(*device, "drawIndexedIndirectCount \"draw commands\" max 1") == 12);
  REQUIRE(countLines(*device, "setFrontFace Clockwise") == 6);
  const auto &trace = device->trace();
  for (std::size_t i = 0; i < trace.size(); ++i) {
    if (trace[i] == "setFrontFace Clockwise") {
      REQUIRE(i > 0);
      REQUIRE(trace[i - 1].starts_with("drawIndexedIndirectCount"));
      REQUIRE(trace[i + 1].starts_with("bindIndexBuffer"));
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
  REQUIRE(device->trace()[skinning + 1] == "pushConstants 40 bytes");
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

TEST_CASE("culling keeps what the frustum holds and drops the rest on a GPU", "[renderer][culling][gpu]") {
  // Both forms of the indirect draws: counted, and every slot with the culled ones empty.
  const bool uncounted = GENERATE(false, true);
  CAPTURE(uncounted);
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform, uncounted);
  if (!uncounted && !device->info().drawIndirectCountSupported) {
    SKIP("no drawIndirectCount on " << device->info().driverName << "; the uncounted run covers it");
  }
  REQUIRE(device->info().drawIndirectCountSupported == !uncounted);
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
    // can reach a pixel, so culling them must leave the image untouched.
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

TEST_CASE("the sun's shadow darkens the ground beside a box on a GPU", "[renderer][gpu]") {
  // Both comparisons: the hardware one, and the one the shader makes itself, which is what a
  // device without usable comparison samplers gets (docs/rendering.md, "Platform notes").
  const bool manual = GENERATE(false, true);
  CAPTURE(manual);
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = testSettings();
    settings.manualShadowCompare = manual;
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const std::array draws{DrawItem{.mesh = plane},
                           DrawItem{.mesh = box, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.5f, 0.0f})}};
    SceneView view = topDownScene(draws);
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
    // Lit by the sky alone: blue, and no brighter than the sky by more than the split-sum
    // approximation allows. That approximation does not conserve energy, so a white rough
    // dielectric under a uniform sky can pass the sky's own value slightly: it does on an
    // M4 Max (239 against 228) and does not on an RTX 4090 or Lavapipe
    // (roadmap.md, "Two GPU tests shade differently on MoltenVK").
    REQUIRE(body.b > 30);
    REQUIRE(body.b > body.r);
    REQUIRE(body.b < sky.b + 20);
    REQUIRE(renderer.isReady(environment));
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
  const auto measure = [&](IDevice &device) {
    {
      Renderer renderer{device, shaderDir(platform), {.jobs = &jobs}};
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
      view.draws = draws;
      view.lights = lights;
      view.environment = environment;
      GpuScene scene{device, renderer, {1920, 1080}};
      constexpr int frames = 30;
      scene.render(view, frames);
      // The last frame's timings are those of the frame two before it, complete by now.
      float total = 0.0f;
      for (const PassTiming &pass : scene.graph.statistics().passes) {
        WARN(
            std::format("{:<20} {:8.3f} ms GPU {:8.3f} ms CPU", pass.name, pass.gpuMilliseconds, pass.cpuMilliseconds));
        total += pass.gpuMilliseconds;
      }
      WARN(std::format("{} draws, {} lights, {} triangles: {:.3f} ms GPU per frame on {}",
                       renderer.statistics().drawCount, renderer.statistics().lightCount,
                       renderer.statistics().triangleCount, total, device.info().deviceName));
      WARN(std::format("submitted from the GPU in {} indirect calls over {} passes",
                       renderer.statistics().indirectCallCount, Renderer::CullJobsOpaque));
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
                       "worst {:.3f} ms), {} indirect draws",
                       recording, submits[submits.size() / 2], submits.size(), submits.back(),
                       device.info().drawIndirectCountSupported ? "counted" : "uncounted (ADR-0014)"));
      REQUIRE(renderer.statistics().drawCount == side * side);
      // Two meshes, so two batches, and one call each in the four cascades, the pre-pass and the
      // forward pass: what used to be sixty thousand draw calls (ADR-0012).
      REQUIRE(renderer.statistics().indirectCallCount == 2 * Renderer::CullJobsOpaque);
      REQUIRE(device.validationMessageCount() == 0);
      renderer.destroyEnvironment(environment);
      renderer.destroyMaterial(material);
      renderer.destroyMesh(sphere);
      renderer.destroyMesh(box);
    }
    REQUIRE(device.validationMessageCount() == 0);
  };
  // A device with drawIndirectCount measures the counted form, then a second device with it left
  // off measures the uncounted one, so the cost of drawing every slot (ADR-0014) can be read
  // against it. MoltenVK has only the uncounted form, and measures it once on its one device.
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  measure(*device);
  if (device->info().drawIndirectCountSupported) {
    device.reset();
    device = gpuDevice(platform, true);
    measure(*device);
  }
}
