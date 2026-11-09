// Screen-space ambient occlusion (ADR-0039, "Implementation acceptance"): the passes against the
// null device's trace, and the mask and the lighting it changes on a GPU (Lavapipe in CI).
#include <sonnet/renderer/Primitives.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/RenderTarget.h>
#include <sonnet/renderer/Renderer.h>

#include <sonnet/core/Error.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/NullDevice.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
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

// Small everything, as RendererTests.cpp's: no bloom, no anti-aliasing unless a test asks.
RendererSettings aoSettings(AntiAliasing antialiasing = AntiAliasing::None) {
  return RendererSettings{.shadows = true,
                          .shadowMapSize = 256,
                          .shadowDistance = 30.0f,
                          .bloom = false,
                          .bloomLevels = 2,
                          .antialiasing = antialiasing,
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

// A camera at `eye` looking at `target`.
Camera lookingAt(glm::vec3 eye, glm::vec3 target) {
  Camera camera;
  camera.position = eye;
  camera.rotation = glm::quatLookAt(glm::normalize(target - eye), glm::vec3{0.0f, 1.0f, 0.0f});
  return camera;
}

// The scene the occlusion tests look at: a box standing on a plane, flat ambient light and no sun,
// so the HDR scene is ambient times albedo times the occlusion, and the camera asking for it.
SceneView occlusionScene(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera = lookingAt({2.5f, 2.5f, 3.5f}, {0.5f, 0.0f, 0.0f});
  view.hasSun = false;
  view.ambient = {1.0f, 1.0f, 1.0f};
  view.ambientOcclusion = true;
  view.ambientOcclusionRadius = 0.5f;
  view.draws = draws;
  return view;
}

// Where a world point lands in a view of `size`, in pixels from the top-left.
glm::ivec2 pixelOf(const SceneView &view, glm::uvec2 size, glm::vec3 world) {
  const float aspect = static_cast<float>(size.x) / static_cast<float>(size.y);
  const glm::vec4 clip = view.camera.projection(aspect) * view.camera.view() * glm::vec4{world, 1.0f};
  const glm::vec2 ndc = glm::vec2{clip} / clip.w;
  return glm::ivec2{glm::floor(glm::vec2{(ndc.x * 0.5f + 0.5f) * static_cast<float>(size.x),
                                         (0.5f - ndc.y * 0.5f) * static_cast<float>(size.y)})};
}

// One frame's images read back: the display-encoded target, the linear HDR scene before the
// resolve and the occlusion mask.
struct Frame {
  glm::uvec2 size{0, 0};
  std::vector<std::uint8_t> color;
  std::vector<glm::vec4> hdr;
  std::vector<float> mask; // empty without one

  [[nodiscard]] float occlusion(glm::ivec2 p) const {
    REQUIRE(!mask.empty());
    return mask[static_cast<std::size_t>(p.y) * size.x + static_cast<std::size_t>(p.x)];
  }
  [[nodiscard]] glm::vec4 light(glm::ivec2 p) const {
    return hdr[static_cast<std::size_t>(p.y) * size.x + static_cast<std::size_t>(p.x)];
  }
};

// Draws a view into a target and reads back what Frame holds.
struct GpuScene {
  IDevice &device;
  Renderer &renderer;
  glm::uvec2 size;
  RenderGraph graph;
  RenderTarget target;
  BufferHandle colorReadback;
  BufferHandle hdrReadback;
  BufferHandle maskReadback;

  GpuScene(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), size(targetSize), graph(gpu), target(gpu, "viewport") {
    target.resize(size);
    const std::uint64_t pixels = std::uint64_t{size.x} * size.y;
    const auto buffer = [&](std::uint64_t bytes, const char *name) {
      return device.createBuffer(
          {.size = bytes, .usage = BufferUsage::TransferDst, .memory = MemoryUsage::GpuToCpu, .debugName = name});
    };
    colorReadback = buffer(pixels * 4, "color readback");
    hdrReadback = buffer(pixels * 8, "hdr readback");
    maskReadback = buffer(pixels, "mask readback");
  }
  ~GpuScene() {
    device.waitIdle();
    device.destroyBuffer(colorReadback);
    device.destroyBuffer(hdrReadback);
    device.destroyBuffer(maskReadback);
  }
  GpuScene(const GpuScene &) = delete;
  GpuScene &operator=(const GpuScene &) = delete;

  // Draws `frames` frames and reads the last one back.
  Frame render(const SceneView &view, int frames = 1) {
    bool hasMask = false;
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      const GraphImage color = graph.importImage(target.color());
      const GraphImage depth = graph.importImage(target.depth());
      renderer.addScenePasses(graph, view, color, depth, {0.0f, 0.0f, 0.0f, 1.0f});
      if (frame == frames - 1) {
        // The forward passes' own image, before any resolve: under TAA the resolved one is a
        // history's, which the scene's graph keeps.
        const GraphImage hdr = renderer.resolved().isValid() ? GraphImage{} : renderer.sceneColor();
        const GraphImage mask = renderer.ambientOcclusion();
        hasMask = mask.isValid();
        graph.addPass(
            "readback",
            [&](PassBuilder &builder) {
              builder.transferSrc(color);
              if (hdr.isValid()) {
                builder.transferSrc(hdr);
              }
              if (mask.isValid()) {
                builder.transferSrc(mask);
              }
            },
            [this, color, hdr, mask](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), colorReadback);
              if (hdr.isValid()) {
                cmd.copyImageToBuffer(resources.image(hdr), hdrReadback);
              }
              if (mask.isValid()) {
                cmd.copyImageToBuffer(resources.image(mask), maskReadback);
              }
            });
      }
      graph.execute(commands);
      device.endFrame();
    }
    device.waitIdle();
    Frame result;
    result.size = size;
    const auto color = device.mappedRange(colorReadback);
    result.color.resize(color.size());
    std::ranges::transform(color, result.color.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    const auto hdr = device.mappedRange(hdrReadback);
    result.hdr.resize(std::size_t{size.x} * size.y);
    for (std::size_t i = 0; i < result.hdr.size(); ++i) {
      std::array<std::uint16_t, 4> halves{};
      std::memcpy(halves.data(), hdr.data() + i * 8, 8);
      result.hdr[i] = {glm::unpackHalf1x16(halves[0]), glm::unpackHalf1x16(halves[1]), glm::unpackHalf1x16(halves[2]),
                       glm::unpackHalf1x16(halves[3])};
    }
    if (hasMask) {
      const auto mask = device.mappedRange(maskReadback);
      result.mask.resize(mask.size());
      std::ranges::transform(mask, result.mask.begin(),
                             [](std::byte b) { return static_cast<float>(std::to_integer<int>(b)) / 255.0f; });
    }
    return result;
  }
};

// The plane, and a unit box on it whose +X face is at x = 0.5.
struct Fixture {
  Renderer &renderer;
  MeshHandle box;
  MeshHandle plane;
  explicit Fixture(Renderer &r)
      : renderer(r), box(r.createMesh(primitives::box(), "box")),
        plane(r.createMesh(primitives::plane({12.0f, 12.0f}), "plane")) {
  }
  ~Fixture() {
    renderer.destroyMesh(box);
    renderer.destroyMesh(plane);
  }
  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;
  [[nodiscard]] DrawItem standing(glm::vec3 at = {0.0f, 0.0f, 0.0f}, MaterialHandle material = {}) const {
    return DrawItem{.mesh = box,
                    .material = material,
                    .transform = glm::translate(glm::mat4{1.0f}, at + glm::vec3{0.0f, 0.5f, 0.0f})};
  }
  [[nodiscard]] DrawItem ground(MaterialHandle material = {}) const {
    return DrawItem{.mesh = plane, .material = material};
  }
};

// Ground points beside the box's +X face, at the face and well past the radius.
constexpr glm::vec3 BesideBox{0.58f, 0.0f, 0.0f};
constexpr glm::vec3 PastRadius{1.4f, 0.0f, 0.3f};

std::vector<std::string> passNames(const RenderGraph &graph) {
  std::vector<std::string> names;
  for (const PassTiming &pass : graph.statistics().passes) {
    names.push_back(pass.name);
  }
  return names;
}

std::ptrdiff_t position(const std::vector<std::string> &names, std::string_view name) {
  const auto it = std::ranges::find(names, name);
  REQUIRE(it != names.end());
  return it - names.begin();
}

bool contains(const std::vector<std::string> &names, std::string_view name) {
  return std::ranges::find(names, name) != names.end();
}

constexpr std::array AoPasses{"ao search", "ao temporal", "ao blur x", "ao blur y", "ao upsample"};

} // namespace

TEST_CASE("ambient occlusion runs between the completed depth and the forward pass", "[renderer][ao][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  const bool occlusionCulling = GENERATE(false, true);
  const AntiAliasing antialiasing = GENERATE(AntiAliasing::None, AntiAliasing::Fxaa, AntiAliasing::Taa);
  RendererSettings settings = aoSettings(antialiasing);
  settings.occlusionCulling = occlusionCulling;
  Renderer renderer{*device, shaderDir(platform), settings};
  Fixture fixture{renderer};
  const std::array draws{fixture.ground(), fixture.standing()};
  SceneView view = occlusionScene(draws);
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 48});
  // The rendering lines of every pass but the occlusion's, which the occlusion must leave alone: no
  // normal or other attachment is added to the pre-pass.
  const auto frame = [&] {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    std::vector<std::string> rendering;
    for (const std::string &line : device->trace()) {
      if (line.starts_with("beginRendering") && !line.contains("ambient occlusion")) {
        rendering.push_back(line);
      }
    }
    return rendering;
  };
  frame();
  const std::vector<std::string> withOcclusion = frame();
  const std::vector<std::string> names = passNames(graph);
  const std::string completed = occlusionCulling ? "depth late" : "depth";
  REQUIRE(position(names, completed) < position(names, "ao search"));
  REQUIRE(position(names, "ao search") < position(names, "ao blur x"));
  REQUIRE(position(names, "ao blur x") < position(names, "ao blur y"));
  REQUIRE(position(names, "ao blur y") < position(names, "ao upsample"));
  REQUIRE(position(names, "ao upsample") < position(names, "light clustering"));
  REQUIRE(position(names, "light clustering") < position(names, "forward"));
  // Only TAA keeps a history, between the search and the filter.
  REQUIRE(contains(names, "ao temporal") == (antialiasing == AntiAliasing::Taa));
  if (antialiasing == AntiAliasing::Taa) {
    REQUIRE(position(names, "ao search") < position(names, "ao temporal"));
    REQUIRE(position(names, "ao temporal") < position(names, "ao blur x"));
  }
  REQUIRE(renderer.ambientOcclusion().isValid());
  REQUIRE(graph.imageDesc(renderer.ambientOcclusion()).format == Format::R8Unorm);

  view.ambientOcclusion = false;
  frame();
  REQUIRE(frame() == withOcclusion);
  REQUIRE_FALSE(renderer.ambientOcclusion().isValid());
}

TEST_CASE("ambient occlusion declares nothing when it is not permitted, not asked for or neutral",
          "[renderer][ao][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RendererSettings settings = aoSettings(AntiAliasing::Taa);
  Renderer renderer{*device, shaderDir(platform), settings};
  Fixture fixture{renderer};
  const MaterialHandle glass =
      renderer.createMaterial({.baseColor = {1.0f, 1.0f, 1.0f, 0.5f}, .alphaMode = AlphaMode::Blend}, "glass");
  const std::array draws{fixture.ground(), fixture.standing()};
  const std::array blendedOnly{fixture.standing({}, glass)};
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const auto declared = [&](const SceneView &view) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    graph.execute(commands);
    device->endFrame();
    const std::vector<std::string> names = passNames(graph);
    const auto count = std::ranges::count_if(AoPasses, [&](const char *pass) { return contains(names, pass); });
    REQUIRE((count == 0) == !renderer.ambientOcclusion().isValid());
    return count > 0;
  };
  SceneView view = occlusionScene(draws);
  REQUIRE(declared(view));
  view.ambientOcclusionRadius = 0.0f;
  REQUIRE_FALSE(declared(view));
  view.ambientOcclusionRadius = -1.0f; // bounded to 0
  REQUIRE_FALSE(declared(view));
  view.ambientOcclusionRadius = std::numeric_limits<float>::quiet_NaN(); // the default, 0.5 m
  REQUIRE(declared(view));
  view.ambientOcclusionRadius = 0.5f;
  view.ambientOcclusionStrength = 0.0f;
  REQUIRE_FALSE(declared(view));
  view.ambientOcclusionStrength = std::numeric_limits<float>::infinity(); // the default, 1
  REQUIRE(declared(view));
  view.ambientOcclusionStrength = 1.0f;
  view.ambientOcclusion = false;
  REQUIRE_FALSE(declared(view));
  view.ambientOcclusion = true;
  // A view without pre-pass draws: nothing writes the depth the search reads.
  SceneView glassView = occlusionScene(blendedOnly);
  REQUIRE_FALSE(declared(glassView));
  // The renderer's toggle bounds the scene's request.
  settings.ambientOcclusion = false;
  renderer.setSettings(settings);
  REQUIRE_FALSE(declared(view));
  settings.ambientOcclusion = true;
  renderer.setSettings(settings);
  REQUIRE(declared(view));
  renderer.destroyMaterial(glass);
}

TEST_CASE("the ambient occlusion images are half and full size at odd and one-pixel extents", "[renderer][ao][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), aoSettings(GENERATE(AntiAliasing::None, AntiAliasing::Taa))};
  Fixture fixture{renderer};
  const std::array draws{fixture.ground(), fixture.standing()};
  const SceneView view = occlusionScene(draws);
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  for (const glm::uvec2 size : {glm::uvec2{1, 1}, glm::uvec2{7, 5}, glm::uvec2{64, 33}, glm::uvec2{2, 9}}) {
    CAPTURE(size.x, size.y);
    target.resize(size);
    for (int frame = 0; frame < 2; ++frame) {
      ICommandList &commands = device->beginFrame();
      graph.reset();
      renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
      graph.execute(commands);
      device->endFrame();
    }
    REQUIRE(graph.imageDesc(renderer.ambientOcclusion()).size == size);
    REQUIRE(graph.imageDesc(renderer.ambientOcclusionSearch()).size == (size + 1u) / 2u);
  }
}

TEST_CASE("the ambient occlusion history starts over on every cut ADR-0039 names", "[renderer][ao][taa][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 64});
  RenderTarget small{*device, "small"};
  small.resize({33, 17});
  RendererSettings settings = aoSettings(AntiAliasing::Taa);
  Renderer renderer{*device, shaderDir(platform), settings};
  Fixture fixture{renderer};
  const std::array draws{fixture.ground(), fixture.standing()};
  SceneView first = occlusionScene(draws);
  SceneView second = occlusionScene(draws);
  // Declares the views, each into its own target, and says which reused its occlusion history.
  const auto frame = [&](std::initializer_list<std::pair<SceneView *, RenderTarget *>> views) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    for (const auto &[view, viewport] : views) {
      renderer.addScenePasses(graph, *view, graph.importImage(viewport->color()), graph.importImage(viewport->depth()));
    }
    graph.execute(commands);
    device->endFrame();
    std::string reused;
    for (std::size_t i = 0; i < views.size(); ++i) {
      reused += renderer.statistics(i).ambientOcclusionHistoryUsed ? '1' : '0';
    }
    return reused;
  };
  const auto only = [&](SceneView &view) { return frame({{&view, &target}}); };
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");
  REQUIRE(only(first) == "1");

  // A change of the controls, or of the counts the renderer searches with.
  first.ambientOcclusionRadius = 0.6f;
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");
  first.ambientOcclusionStrength = 0.5f;
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");
  settings.ambientOcclusionSlices = 2;
  renderer.setSettings(settings);
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");

  // Turned off for a frame, then on.
  first.ambientOcclusion = false;
  REQUIRE(only(first) == "0");
  first.ambientOcclusion = true;
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");

  // A cut, a projection change and a resize.
  first.resetHistory = true;
  REQUIRE(only(first) == "0");
  first.resetHistory = false;
  REQUIRE(only(first) == "1");
  first.camera.projectionKind = ProjectionKind::Orthographic;
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");
  first.camera.projectionKind = ProjectionKind::Perspective;
  REQUIRE(only(first) == "0");
  target.resize({48, 40});
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");

  // A frame without a resolve: a debug view, and a reference render.
  settings.debugView = DebugView::AmbientOcclusion;
  renderer.setSettings(settings);
  REQUIRE(only(first) == "0");
  REQUIRE_FALSE(contains(passNames(graph), "ao temporal"));
  REQUIRE(contains(passNames(graph), "ao search"));
  settings.debugView = DebugView::Final;
  renderer.setSettings(settings);
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");
  first.referenceJitter = glm::vec2{0.25f, -0.25f};
  REQUIRE(only(first) == "0");
  REQUIRE_FALSE(contains(passNames(graph), "ao temporal"));
  first.referenceJitter.reset();
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");

  // A skipped frame of the view.
  REQUIRE(only(second) == "0");
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "1");

  // Two views of different sizes in one frame keep their own history and masks.
  REQUIRE(frame({{&first, &target}, {&second, &small}}) == "10");
  REQUIRE(frame({{&first, &target}, {&second, &small}}) == "11");
  REQUIRE(renderer.ambientOcclusion(0).index != renderer.ambientOcclusion(1).index);
  REQUIRE(graph.imageDesc(renderer.ambientOcclusion(0)).size == glm::uvec2{48, 40});
  REQUIRE(graph.imageDesc(renderer.ambientOcclusion(1)).size == glm::uvec2{33, 17});
  REQUIRE(graph.imageDesc(renderer.ambientOcclusionSearch(1)).size == glm::uvec2{17, 9});

  // Without TAA there is no history at all.
  settings.antialiasing = GENERATE(AntiAliasing::None, AntiAliasing::Fxaa);
  renderer.setSettings(settings);
  REQUIRE(only(first) == "0");
  REQUIRE(only(first) == "0");
  REQUIRE_FALSE(contains(passNames(graph), "ao temporal"));
}

TEST_CASE("a box on a plane occludes the ground beside it and an open plane stays unoccluded on a GPU",
          "[renderer][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), aoSettings()};
    Fixture fixture{renderer};
    const ProjectionKind projection = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    CAPTURE(projection == ProjectionKind::Orthographic);
    const glm::uvec2 size{256, 192};
    GpuScene scene{*device, renderer, size};

    const std::array plane{fixture.ground()};
    SceneView open = occlusionScene(plane);
    open.camera.projectionKind = projection;
    open.camera.orthoHeight = 4.0f;
    const Frame alone = scene.render(open);
    // Every pixel: the plane and the sky past its far edge. A few slices through a grazing plane
    // scatter around 1 by a few hundredths before the filter averages them, most where the plane's
    // edge leaves the filter fewer neighbours.
    double sum = 0.0;
    for (const float value : alone.mask) {
      sum += static_cast<double>(value);
    }
    CAPTURE(*std::ranges::min_element(alone.mask), sum / static_cast<double>(alone.mask.size()));
    REQUIRE(*std::ranges::min_element(alone.mask) > 0.95f);
    REQUIRE(sum / static_cast<double>(alone.mask.size()) > 0.995);

    const std::array draws{fixture.ground(), fixture.standing()};
    SceneView boxed = occlusionScene(draws);
    boxed.camera = open.camera;
    const Frame withBox = scene.render(boxed);
    const glm::ivec2 beside = pixelOf(boxed, size, BesideBox);
    const glm::ivec2 past = pixelOf(boxed, size, PastRadius);
    const glm::ivec2 top = pixelOf(boxed, size, {0.0f, 1.0f, 0.0f});
    CAPTURE(withBox.occlusion(beside), withBox.occlusion(past), withBox.occlusion(top));
    REQUIRE(withBox.occlusion(beside) < 0.85f);
    // Outside the world-space radius the box does nothing, and its open top face is unoccluded.
    REQUIRE(withBox.occlusion(past) > 0.97f);
    REQUIRE(withBox.occlusion(top) > 0.95f);
    // The flat ambient term takes exactly the mask.
    const glm::vec4 lit = withBox.light(beside);
    REQUIRE(std::abs(lit.r - withBox.occlusion(beside)) < 0.01f);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the inside corner of a wall and the floor is occluded and a box's silhouette leaves no halo on a GPU",
          "[renderer][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), aoSettings()};
    Fixture fixture{renderer};
    const glm::uvec2 size{256, 192};
    GpuScene scene{*device, renderer, size};
    // A wall 4 m wide standing on the floor at z = -1.
    const std::array corner{
        fixture.ground(),
        DrawItem{.mesh = fixture.box,
                 .transform = glm::scale(glm::translate(glm::mat4{1.0f}, {0.0f, 1.0f, -1.1f}), {4.0f, 2.0f, 0.2f})}};
    SceneView view = occlusionScene(corner);
    view.camera = lookingAt({0.0f, 1.5f, 2.5f}, {0.0f, 0.3f, -1.0f});
    const Frame frame = scene.render(view);
    const float atCorner = frame.occlusion(pixelOf(view, size, {0.0f, 0.0f, -0.92f}));
    const float onWall = frame.occlusion(pixelOf(view, size, {0.0f, 0.08f, -1.0f}));
    const float openFloor = frame.occlusion(pixelOf(view, size, {0.0f, 0.0f, 0.6f}));
    const float openWall = frame.occlusion(pixelOf(view, size, {0.0f, 1.6f, -1.0f}));
    CAPTURE(atCorner, onWall, openFloor, openWall);
    REQUIRE(atCorner < 0.85f);
    REQUIRE(onWall < 0.85f);
    REQUIRE(openFloor > 0.97f);
    REQUIRE(openWall > 0.97f);

    // A box floating two metres in front of the wall, far more than the radius: no dark rim on the
    // wall around its silhouette, nor on its own face at the edge.
    const std::array floating{
        DrawItem{.mesh = fixture.box,
                 .transform = glm::scale(glm::translate(glm::mat4{1.0f}, {0.0f, 1.0f, -1.1f}), {4.0f, 2.0f, 0.2f})},
        DrawItem{.mesh = fixture.box, .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 1.0f, 1.0f})}};
    SceneView edge = occlusionScene(floating);
    edge.camera = lookingAt({0.0f, 1.0f, 4.0f}, {0.0f, 1.0f, 0.0f});
    const Frame edges = scene.render(edge);
    float darkest = 1.0f;
    for (std::uint32_t y = 0; y < size.y; ++y) {
      for (std::uint32_t x = 0; x < size.x; ++x) {
        darkest = std::min(darkest, edges.occlusion({static_cast<int>(x), static_cast<int>(y)}));
      }
    }
    REQUIRE(darkest > 0.95f);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("ambient occlusion multiplies flat ambient, diffuse and specular IBL once, and nothing else, on a GPU",
          "[renderer][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = aoSettings();
    Renderer renderer{*device, shaderDir(platform), settings};
    Fixture fixture{renderer};
    const glm::uvec2 size{256, 192};
    GpuScene scene{*device, renderer, size};
    const TextureHandle halfGrey = renderer.createTexture(solidTexture({128, 128, 128, 255}), "half occlusion");
    const MaterialHandle dielectric = renderer.createMaterial({.metallic = 0.0f, .roughness = 0.8f}, "dielectric");
    const MaterialHandle metal = renderer.createMaterial({.metallic = 1.0f, .roughness = 0.3f}, "metal");
    const MaterialHandle occluded =
        renderer.createMaterial({.metallic = 0.0f, .occlusionTexture = halfGrey}, "occluded dielectric");
    const MaterialHandle unlit =
        renderer.createMaterial({.baseColor = {0.5f, 0.5f, 0.5f, 1.0f}, .unlit = true}, "unlit");
    const MaterialHandle glowing =
        renderer.createMaterial({.baseColor = {0.0f, 0.0f, 0.0f, 1.0f}, .emissive = {0.7f, 0.3f, 0.2f}}, "emissive");
    TextureData sky{.size = {8, 4}, .format = Format::R16G16B16A16Sfloat, .mipLevels = 1, .cube = false, .data = {}};
    sky.data.resize(static_cast<std::size_t>(sky.expectedSize()));
    for (std::size_t i = 0; i < sky.data.size(); i += 8) {
      const std::array<std::uint16_t, 4> texel{glm::packHalf1x16(0.8f), glm::packHalf1x16(0.8f),
                                               glm::packHalf1x16(0.8f), glm::packHalf1x16(1.0f)};
      std::memcpy(sky.data.data() + i, texel.data(), 8);
    }
    const EnvironmentHandle environment = renderer.createEnvironment(sky, "sky");

    // Renders the box on a ground of `material` with and without the camera's request, and checks
    // the ratio of the two HDR scenes where the mask is: the mask itself for an indirect term.
    const auto ratioFollowsMask = [&](MaterialHandle material, SceneView view, bool indirect) {
      const std::array draws{fixture.ground(material), fixture.standing({}, material)};
      view.draws = draws;
      view.ambientOcclusion = false;
      const Frame without = scene.render(view, 2);
      view.ambientOcclusion = true;
      const Frame with = scene.render(view, 2);
      REQUIRE(!with.mask.empty());
      const glm::ivec2 beside = pixelOf(view, size, BesideBox);
      REQUIRE(with.occlusion(beside) < 0.85f);
      for (const glm::ivec2 p : {beside, pixelOf(view, size, PastRadius), pixelOf(view, size, {-0.6f, 0.0f, 0.8f})}) {
        const float off = without.light(p).g;
        const float on = with.light(p).g;
        CAPTURE(p.x, p.y, off, on, with.occlusion(p));
        if (indirect) {
          REQUIRE(off > 0.01f);
          REQUIRE(std::abs(on / off - with.occlusion(p)) < 0.01f);
        } else {
          REQUIRE(on == off);
        }
      }
      return std::pair{without, with};
    };

    SECTION("flat ambient") {
      SceneView view = occlusionScene({});
      ratioFollowsMask(dielectric, view, true);
    }
    SECTION("diffuse and specular image-based light, each apart") {
      SceneView view = occlusionScene({});
      view.environment = environment;
      settings.debugView = DebugView::IblDiffuse;
      renderer.setSettings(settings);
      ratioFollowsMask(dielectric, view, true);
      settings.debugView = DebugView::IblSpecular;
      renderer.setSettings(settings);
      ratioFollowsMask(metal, view, true);
    }
    SECTION("the material's occlusion and the mask multiply once") {
      SceneView view = occlusionScene({});
      const auto [plainOff, plainOn] = ratioFollowsMask(dielectric, view, true);
      const auto [occludedOff, occludedOn] = ratioFollowsMask(occluded, view, true);
      const glm::ivec2 beside = pixelOf(view, size, BesideBox);
      const float material = 128.0f / 255.0f;
      REQUIRE(std::abs(occludedOn.light(beside).g - plainOff.light(beside).g * material * plainOn.occlusion(beside)) <
              0.01f);
    }
    SECTION("direct light alone is unchanged") {
      SceneView view = occlusionScene({});
      view.hasSun = true;
      view.sun.direction = glm::normalize(glm::vec3{-0.3f, -1.0f, -0.2f});
      view.ambient = glm::vec3{0.0f};
      ratioFollowsMask(dielectric, view, false);
    }
    SECTION("unlit and emissive surfaces are unchanged") {
      SceneView view = occlusionScene({});
      ratioFollowsMask(unlit, view, false);
      view.ambient = glm::vec3{0.0f};
      ratioFollowsMask(glowing, view, false);
    }
    renderer.destroyEnvironment(environment);
    for (const MaterialHandle material : {dielectric, metal, occluded, unlit, glowing}) {
      renderer.destroyMaterial(material);
    }
    renderer.destroyTexture(halfGrey);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("cut-out pixels occlude nothing and blended or transmissive surfaces take no screen occlusion on a GPU",
          "[renderer][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = aoSettings();
    settings.transmission = false; // a transmissive draw is then a plain blend, whose sum is known
    Renderer renderer{*device, shaderDir(platform), settings};
    Fixture fixture{renderer};
    const glm::uvec2 size{256, 192};
    GpuScene scene{*device, renderer, size};

    // A masked box whose every texel is under the cutoff writes no depth, so it hides nothing.
    const MaterialHandle cut = renderer.createMaterial(
        {.baseColor = {1.0f, 1.0f, 1.0f, 0.1f}, .alphaCutoff = 0.5f, .alphaMode = AlphaMode::Mask}, "cut out");
    const MaterialHandle kept = renderer.createMaterial(
        {.baseColor = {1.0f, 1.0f, 1.0f, 0.9f}, .alphaCutoff = 0.5f, .alphaMode = AlphaMode::Mask}, "kept");
    const std::array cutDraws{fixture.ground(), fixture.standing({}, cut)};
    const std::array keptDraws{fixture.ground(), fixture.standing({}, kept)};
    SceneView view = occlusionScene(cutDraws);
    const glm::ivec2 beside = pixelOf(view, size, BesideBox);
    REQUIRE(scene.render(view).occlusion(beside) > 0.97f);
    view.draws = keptDraws;
    REQUIRE(scene.render(view).occlusion(beside) < 0.8f);

    // A pane hovering just above the occluded ground. The ground under it keeps its occlusion and the
    // pane takes none: the blend of the two is the pane's own light over the occluded ground.
    const MaterialHandle pane =
        GENERATE(0, 1) == 0
            ? renderer.createMaterial({.baseColor = {0.4f, 0.4f, 0.4f, 0.5f},
                                       .metallic = 0.0f,
                                       .alphaMode = AlphaMode::Blend,
                                       .doubleSided = true},
                                      "blended pane")
            : renderer.createMaterial(
                  {.baseColor = {0.4f, 0.4f, 0.4f, 1.0f}, .metallic = 0.0f, .transmission = 0.5f, .doubleSided = true},
                  "transmissive pane");
    const DrawItem paneDraw{.mesh = fixture.plane,
                            .material = pane,
                            .transform =
                                glm::scale(glm::translate(glm::mat4{1.0f}, {0.9f, 0.05f, 0.0f}), {0.08f, 1.0f, 0.08f})};
    const std::array ground{fixture.ground(), fixture.standing()};
    const std::array covered{fixture.ground(), fixture.standing(), paneDraw};
    view.draws = ground;
    view.ambientOcclusion = false;
    const Frame groundOff = scene.render(view);
    view.draws = covered;
    const Frame coveredOff = scene.render(view);
    view.ambientOcclusion = true;
    const Frame coveredOn = scene.render(view);
    const glm::ivec2 under = pixelOf(view, size, {0.6f, 0.05f, 0.0f});
    const float mask = coveredOn.occlusion(under);
    CAPTURE(mask, groundOff.light(under).g, coveredOff.light(under).g, coveredOn.light(under).g);
    REQUIRE(mask < 0.9f);
    // a * pane + (1 - a) * ground, with only the ground's term taking the mask.
    const float expected = coveredOff.light(under).g - 0.5f * groundOff.light(under).g * (1.0f - mask);
    REQUIRE(std::abs(coveredOn.light(under).g - expected) < 0.01f);
    for (const MaterialHandle material : {cut, kept, pane}) {
      renderer.destroyMaterial(material);
    }
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

namespace {

MeshData skinnedBox() {
  MeshData box = primitives::box();
  box.skin.assign(box.vertices.size(), SkinWeights{.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}});
  return box;
}

// The mean absolute difference of two masks.
float meanDifference(const Frame &a, const Frame &b) {
  REQUIRE(a.mask.size() == b.mask.size());
  double sum = 0.0;
  for (std::size_t i = 0; i < a.mask.size(); ++i) {
    sum += static_cast<double>(std::abs(a.mask[i] - b.mask[i]));
  }
  return static_cast<float>(sum / static_cast<double>(a.mask.size()));
}

} // namespace

TEST_CASE("without TAA the search pattern is fixed, and a reference jitter repeats its own", "[renderer][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), aoSettings(GENERATE(AntiAliasing::None, AntiAliasing::Fxaa))};
    Fixture fixture{renderer};
    GpuScene scene{*device, renderer, {96, 64}};
    const std::array draws{fixture.ground(), fixture.standing()};
    SceneView view = occlusionScene(draws);
    const Frame first = scene.render(view);
    const Frame second = scene.render(view);
    REQUIRE(first.mask == second.mask);
    view.referenceJitter = glm::vec2{0.25f, -1.0f / 6.0f}; // TAA's second sample
    const Frame jittered = scene.render(view);
    REQUIRE(scene.render(view).mask == jittered.mask);
    REQUIRE(renderer.statistics().ambientOcclusionHistoryUsed == false);
    REQUIRE(jittered.mask != first.mask);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("under TAA the occlusion settles near the spatial reference and a vacated region recovers in four frames",
          "[renderer][ao][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), aoSettings(AntiAliasing::Taa)};
    Renderer reference{*device, shaderDir(platform), aoSettings(AntiAliasing::None)};
    Fixture fixture{renderer};
    Fixture referenceFixture{reference};
    const MeshHandle skinned = renderer.createMesh(skinnedBox(), "skinned box");
    const glm::uvec2 size{256, 192};
    GpuScene scene{*device, renderer, size};
    GpuScene spatial{*device, reference, size};

    // A held pose: after 32 frames to settle, a whole eight-frame jitter cycle stays near the
    // fixed-pattern spatial result and barely changes from frame to frame.
    const std::array draws{fixture.ground(), fixture.standing()};
    const std::array referenceDraws{referenceFixture.ground(), referenceFixture.standing()};
    SceneView view = occlusionScene(draws);
    SceneView referenceView = occlusionScene(referenceDraws);
    const Frame expected = spatial.render(referenceView);
    Frame previous = scene.render(view, 32);
    float worstDrift = 0.0f;
    float worstReference = 0.0f;
    for (int frame = 0; frame < 8; ++frame) {
      const Frame current = scene.render(view);
      REQUIRE(renderer.statistics().ambientOcclusionHistoryUsed);
      worstDrift = std::max(worstDrift, meanDifference(current, previous));
      worstReference = std::max(worstReference, meanDifference(current, expected));
      previous = current;
    }
    CAPTURE(worstDrift, worstReference);
    REQUIRE(worstDrift < 0.01f);
    REQUIRE(worstReference < 0.03f);

    // The camera moves: the history follows it, and the occlusion beside the box stays.
    for (int frame = 0; frame < 8; ++frame) {
      view.camera.position.x -= 0.03f;
      const Frame moved = scene.render(view);
      referenceView.camera = view.camera;
      const glm::ivec2 beside = pixelOf(view, size, BesideBox);
      CAPTURE(frame, moved.occlusion(beside));
      REQUIRE(moved.occlusion(beside) < 0.85f);
    }
    REQUIRE(meanDifference(scene.render(view), spatial.render(referenceView)) < 0.03f);

    // The occluder, rigid or skinned, slides away and is then removed: the ground it occluded
    // recovers within four frames of it leaving.
    const bool skin = GENERATE(false, true);
    CAPTURE(skin);
    glm::vec3 at{0.0f};
    glm::mat4 last = glm::translate(glm::mat4{1.0f}, at + glm::vec3{0.0f, 0.5f, 0.0f});
    std::array<glm::mat4, 1> joints{glm::mat4{1.0f}};
    std::vector<DrawItem> moving{fixture.ground(), fixture.standing()};
    for (int frame = 0; frame < 10; ++frame) {
      at.z -= 0.15f;
      const glm::mat4 now = glm::translate(glm::mat4{1.0f}, at + glm::vec3{0.0f, 0.5f, 0.0f});
      if (skin) {
        joints[0] = now;
        moving[1] = DrawItem{.mesh = skinned, .firstJoint = 0, .jointCount = 1, .skinInstance = 9};
      } else {
        moving[1] = DrawItem{.mesh = fixture.box, .transform = now, .previousTransform = last};
      }
      last = now;
      view.draws = moving;
      view.joints = joints;
      static_cast<void>(scene.render(view));
    }
    const std::array groundOnly{fixture.ground()};
    view.draws = groundOnly;
    Frame vacated;
    for (int frame = 0; frame < 4; ++frame) {
      vacated = scene.render(view);
    }
    float darkest = 1.0f;
    for (const float value : vacated.mask) {
      darkest = std::min(darkest, value);
    }
    CAPTURE(darkest);
    REQUIRE(darkest > 0.95f);
    renderer.destroyMesh(skinned);
    REQUIRE(device->validationMessageCount() == 0);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

namespace {

double percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::ranges::sort(values);
  const auto index = static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size()))) - 1;
  return values[std::min(index, values.size() - 1)];
}

double environmentSeconds(const char *name, double fallback) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is the standard call; _dupenv_s is MSVC-only
#endif
  const char *value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  return value != nullptr ? std::atof(value) : fallback;
}

} // namespace

// The added GPU frame time ADR-0039 budgets, on whatever device runs it: a course of 400 boxes of
// varied sizes on a ground under a sun and an environment, TAA and shadows on, at 1920x1080 and the
// default sample counts, offscreen so no presentation paces it. Three cold pairs of three-second
// windows after a warm-up, alternating which goes first, then one sustained pair of
// SONNET_BENCH_AO_SUSTAINED seconds each (0, the default, skips it). Each sample is a complete GPU
// frame: the sum of every pass's timestamps of one frame, all of them available.
TEST_CASE("the added GPU frame time of ambient occlusion at 1080p", "[.][benchmark][ao][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), RendererSettings{}};
    Fixture fixture{renderer};
    const MeshHandle ground = renderer.createMesh(primitives::plane({80.0f, 80.0f}), "ground");
    const MaterialHandle stone = renderer.createMaterial({.metallic = 0.0f, .roughness = 0.7f}, "stone");
    TextureData sky{.size = {8, 4}, .format = Format::R16G16B16A16Sfloat, .mipLevels = 1, .cube = false, .data = {}};
    sky.data.resize(static_cast<std::size_t>(sky.expectedSize()));
    for (std::size_t i = 0; i < sky.data.size(); i += 8) {
      const std::array<std::uint16_t, 4> texel{glm::packHalf1x16(0.6f), glm::packHalf1x16(0.7f),
                                               glm::packHalf1x16(0.9f), glm::packHalf1x16(1.0f)};
      std::memcpy(sky.data.data() + i, texel.data(), 8);
    }
    const EnvironmentHandle environment = renderer.createEnvironment(sky, "sky");
    std::vector<DrawItem> draws{DrawItem{.mesh = ground, .material = stone}};
    for (int z = 0; z < 20; ++z) {
      for (int x = 0; x < 20; ++x) {
        const float height = 0.3f + static_cast<float>((x * 7 + z * 13) % 9) * 0.25f;
        const float width = 0.4f + static_cast<float>((x * 5 + z * 3) % 5) * 0.2f;
        draws.push_back(DrawItem{
            .mesh = fixture.box,
            .material = stone,
            .transform =
                glm::scale(glm::translate(glm::mat4{1.0f}, {static_cast<float>(x) * 2.0f - 19.0f, height * 0.5f,
                                                            static_cast<float>(z) * -2.0f + 6.0f}),
                           {width, height, width})});
      }
    }
    SceneView view;
    view.camera = lookingAt({0.0f, 3.0f, 10.0f}, {0.0f, 0.5f, -6.0f});
    view.sun.direction = glm::normalize(glm::vec3{-0.4f, -1.0f, -0.3f});
    view.environment = environment;
    view.draws = draws;
    const glm::uvec2 size{1920, 1080};
    RenderGraph graph{*device};
    RenderTarget target{*device, "viewport"};
    target.resize(size);
    const auto frame = [&] {
      ICommandList &commands = device->beginFrame();
      graph.reset();
      renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
      graph.execute(commands);
      device->endFrame();
    };
    struct Window {
      std::vector<double> totals;
      std::vector<double> occlusion;
      std::size_t frames{0};
    };
    const auto measure = [&](bool on, double seconds) {
      view.ambientOcclusion = on;
      view.resetHistory = true;
      for (int i = 0; i < 120; ++i) { // the pipelines, the pool and TAA settle
        frame();
        view.resetHistory = false;
      }
      Window window;
      const auto start = std::chrono::steady_clock::now();
      while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        frame();
        ++window.frames;
        double total = 0.0;
        double ao = 0.0;
        bool complete = true;
        for (const PassTiming &pass : graph.statistics().passes) {
          complete = complete && pass.gpuAvailable;
          total += static_cast<double>(pass.gpuMilliseconds);
          if (pass.name.starts_with("ao ")) {
            ao += static_cast<double>(pass.gpuMilliseconds);
          }
        }
        if (complete) {
          window.totals.push_back(total);
          window.occlusion.push_back(ao);
        }
      }
      std::printf("%-3s %7.1f s %6zu frames %6zu complete: GPU median %.3f p95 %.3f ms, ao passes median %.3f p95 "
                  "%.3f ms, transient images %.1f MiB\n",
                  on ? "on" : "off", seconds, window.frames, window.totals.size(), percentile(window.totals, 0.5),
                  percentile(window.totals, 0.95), percentile(window.occlusion, 0.5),
                  percentile(window.occlusion, 0.95),
                  static_cast<double>(graph.statistics().transientImageBytes) / (1024.0 * 1024.0));
      std::fflush(stdout);
      return window;
    };
    const auto pair = [&](const char *label, double seconds, bool onFirst) {
      const Window first = measure(onFirst, seconds);
      const Window second = measure(!onFirst, seconds);
      const Window &on = onFirst ? first : second;
      const Window &off = onFirst ? second : first;
      std::printf("%s: added median %+.3f ms, p95 %+.3f ms\n", label,
                  percentile(on.totals, 0.5) - percentile(off.totals, 0.5),
                  percentile(on.totals, 0.95) - percentile(off.totals, 0.95));
      std::fflush(stdout);
    };
    pair("cold pair 1 (on first)", 3.0, true);
    pair("cold pair 2 (off first)", 3.0, false);
    pair("cold pair 3 (on first)", 3.0, true);
    if (const double sustained = environmentSeconds("SONNET_BENCH_AO_SUSTAINED", 0.0); sustained > 0.0) {
      pair("sustained pair (off first)", sustained, false);
    }
    device->waitIdle();
    renderer.destroyEnvironment(environment);
    renderer.destroyMaterial(stone);
    renderer.destroyMesh(ground);
    REQUIRE(device->validationMessageCount() == 0);
  }
}
