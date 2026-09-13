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

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string_view>

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

// A GPU device, or a skip.
std::unique_ptr<IDevice> gpuDevice(sonnet::platform::Platform &platform) {
  try {
    return createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
  return {};
}

// Draws the view into a target for `frames` frames and reads the last one back.
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
      renderer.addScenePasses(graph, view, color, graph.importImage(target.depth()), {0.0f, 0.0f, 0.0f, 1.0f});
      if (frame == frames - 1) {
        graph.addPass(
            "readback", [&](PassBuilder &b) { b.transferSrc(color); },
            [&](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), readback);
            });
      }
      graph.execute(commands);
      device.endFrame();
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
  // Each box in four cascades, the pre-pass and the forward pass.
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 12);
  REQUIRE(countLines(*device, "drawIndexed") == 18);
  REQUIRE(lineIndex(*device, "bindPipeline \"depth\"") < lineIndex(*device, "bindPipeline \"forward\""));
  REQUIRE(renderer.statistics().drawCount == 3);
  REQUIRE(renderer.statistics().shadowDrawCount == 12);
  REQUIRE(renderer.statistics().triangleCount == 12 * 2 + (8 * 2 * 2 + 8 * 2));

  renderer.destroyMesh(sphere);
  renderer.destroyMesh(box);
  REQUIRE(!renderer.isValid(box));
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
  REQUIRE(countLines(*device, "drawIndexed 36 x1") == 4 + 1 + 1 + 1);
  const std::size_t blend = lineIndex(*device, "bindPipeline \"forward blend\"");
  REQUIRE(lineIndex(*device, "bindPipeline \"forward double sided\"") < blend);
  const auto &trace = device->trace();
  const auto firstBlendedDraw = std::find_if(trace.begin() + static_cast<std::ptrdiff_t>(blend), trace.end(),
                                             [](const std::string &line) { return line.starts_with("drawIndexed"); });
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
  data.data.resize(4 * 2 * 4, std::byte{7});
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
    // Lit by the sky's irradiance alone: blue, dimmer than the sky.
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
  static_cast<void>(renderer.reloadShader("post", garbage));

  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
  graph.execute(commands);
  device->endFrame();
  REQUIRE(countLines(*device, "bindPipeline \"forward\"") == 1);
  REQUIRE(countLines(*device, "bindPipeline \"tonemap\"") == 1);
  renderer.destroyMesh(box);
}
