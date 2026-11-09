// The sun's screen-space contact shadows (ADR-0039, #509): the passes a frame declares on the null
// device, and what the trace and the forward pass draw on a GPU (Lavapipe in CI).
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

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
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

// Small everything, so Lavapipe finishes in seconds. The cascades are deliberately coarse: a map of
// 64 texels over 30 metres is far too coarse for a ten-centimetre box's shadow, which is what the
// contact shadows are for.
RendererSettings contactSettings() {
  return RendererSettings{.shadows = true,
                          .shadowMapSize = 64,
                          .shadowDistance = 30.0f,
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

bool has(const std::vector<std::string> &names, std::string_view name) {
  return std::ranges::find(names, name) != names.end();
}

// A ground plane and a ten-centimetre box standing on it at the origin, seen from 0.7 m away and
// above, with a sun low from -X: the box's contact falls on the ground on its +X side.
struct BoxOnPlane {
  MeshHandle box;
  MeshHandle plane;
  std::vector<DrawItem> draws;

  explicit BoxOnPlane(Renderer &renderer, float boxSize = 0.1f) {
    box = renderer.createMesh(primitives::box(), "box");
    plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    draws = {DrawItem{.mesh = plane},
             DrawItem{.mesh = box,
                      .transform = glm::translate(glm::mat4{1.0f}, {0.0f, boxSize * 0.5f, 0.0f}) *
                                   glm::scale(glm::mat4{1.0f}, glm::vec3{boxSize})}};
  }
};

SceneView contactView(std::span<const DrawItem> draws) {
  SceneView view;
  view.camera.position = {0.0f, 0.35f, 0.6f};
  view.camera.rotation = glm::quatLookAt(glm::normalize(-view.camera.position), glm::vec3{0.0f, 1.0f, 0.0f});
  view.camera.nearPlane = 0.05f;
  view.camera.orthoHeight = 0.6f;
  view.sun.direction = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});
  view.sun.intensity = 3.0f;
  view.sun.contactShadows = true;
  view.sun.contactShadowLength = 0.2f;
  view.sun.contactShadowThickness = 0.05f;
  view.ambient = glm::vec3{0.02f};
  view.draws = draws;
  return view;
}

// Where a world point lands in a view of `size`, in pixels from the top-left.
[[maybe_unused]] glm::ivec2 pixelOf(const SceneView &view, glm::uvec2 size, glm::vec3 point) {
  const float aspect = static_cast<float>(size.x) / static_cast<float>(size.y);
  const glm::vec4 clip = view.camera.projection(aspect) * view.camera.view() * glm::vec4{point, 1.0f};
  const glm::vec2 ndc = glm::vec2{clip} / clip.w;
  return glm::ivec2{static_cast<int>((ndc.x * 0.5f + 0.5f) * static_cast<float>(size.x)),
                    static_cast<int>((0.5f - ndc.y * 0.5f) * static_cast<float>(size.y))};
}

std::unique_ptr<IDevice> gpuDevice(sonnet::platform::Platform &platform) {
  try {
    return createDevice({.platform = &platform, .applicationName = "renderer_tests"});
  } catch (const sonnet::core::Exception &e) {
    SKIP("no usable Vulkan 1.4 device: " << e.what());
  }
}

// What the tone mapper makes of a linear value at exposure 1: post.slang's ACES fit, then the
// display encoding, in bytes. The ShadowFactor debug view shows the sun's visibility through it.
[[maybe_unused]] int encoded(float x) {
  const float mapped = std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
  return static_cast<int>(std::lround(std::pow(mapped, 1.0f / 2.2f) * 255.0f));
}

// Draws a view for some frames and reads back the last one's colour and contact mask.
struct ContactScene {
  IDevice &device;
  Renderer &renderer;
  glm::uvec2 size;
  RenderGraph graph;
  RenderTarget target;
  BufferHandle colorReadback;
  BufferHandle maskReadback;
  bool maskDrawn{false};

  ContactScene(IDevice &gpu, Renderer &sceneRenderer, glm::uvec2 targetSize)
      : device(gpu), renderer(sceneRenderer), size(targetSize), graph(gpu), target(gpu, "viewport") {
    target.resize(size);
    colorReadback = device.createBuffer({.size = std::uint64_t{size.x} * size.y * 4,
                                         .usage = BufferUsage::TransferDst,
                                         .memory = MemoryUsage::GpuToCpu,
                                         .debugName = "colour readback"});
    maskReadback = device.createBuffer({.size = std::uint64_t{size.x} * size.y,
                                        .usage = BufferUsage::TransferDst,
                                        .memory = MemoryUsage::GpuToCpu,
                                        .debugName = "contact readback"});
  }
  ~ContactScene() {
    device.waitIdle();
    device.destroyBuffer(maskReadback);
    device.destroyBuffer(colorReadback);
  }
  ContactScene(const ContactScene &) = delete;
  ContactScene &operator=(const ContactScene &) = delete;

  void render(const SceneView &view, int frames = 1) {
    for (int frame = 0; frame < frames; ++frame) {
      ICommandList &commands = device.beginFrame();
      graph.reset();
      const GraphImage color = graph.importImage(target.color());
      renderer.addScenePasses(graph, view, color, graph.importImage(target.depth()), {0.0f, 0.0f, 0.0f, 1.0f});
      if (frame == frames - 1) {
        const GraphImage mask = renderer.contactShadows();
        maskDrawn = mask.isValid();
        graph.addPass(
            "readback",
            [&](PassBuilder &b) {
              b.transferSrc(color);
              if (mask.isValid()) {
                b.transferSrc(mask);
              }
            },
            [&, color, mask](ICommandList &cmd, const PassResources &resources) {
              cmd.copyImageToBuffer(resources.image(color), colorReadback);
              if (mask.isValid()) {
                cmd.copyImageToBuffer(resources.image(mask), maskReadback);
              }
            });
      }
      graph.execute(commands);
      device.endFrame();
    }
    device.waitIdle();
  }

  // The contact visibility at a pixel, 0 to 255; 255 when the frame traced none.
  [[nodiscard]] int mask(glm::ivec2 p) {
    if (!maskDrawn) {
      return 255;
    }
    return std::to_integer<int>(device.mappedRange(maskReadback)[std::size_t(p.y) * size.x + std::size_t(p.x)]);
  }
  [[nodiscard]] std::vector<int> maskImage() {
    std::vector<int> values(std::size_t{size.x} * size.y, 255);
    if (maskDrawn) {
      const std::span<const std::byte> bytes = device.mappedRange(maskReadback);
      for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = std::to_integer<int>(bytes[i]);
      }
    }
    return values;
  }
  // The red channel of the tone-mapped colour.
  [[nodiscard]] int red(glm::ivec2 p) {
    return std::to_integer<int>(device.mappedRange(colorReadback)[(std::size_t(p.y) * size.x + std::size_t(p.x)) * 4]);
  }
  [[nodiscard]] std::vector<std::byte> colorImage() {
    const std::span<const std::byte> bytes = device.mappedRange(colorReadback);
    return {bytes.begin(), bytes.end()};
  }
};

// Pixels of the image where the mask is darker than half.
std::size_t darkCount(const std::vector<int> &mask) {
  return static_cast<std::size_t>(std::ranges::count_if(mask, [](int value) { return value < 128; }));
}

} // namespace

TEST_CASE("contact shadows trace after the completed depth and before the forward pass", "[renderer][contact][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({64, 48});
  const bool occlusion = GENERATE(false, true);
  const AntiAliasing antialiasing = GENERATE(AntiAliasing::None, AntiAliasing::Taa);
  RendererSettings settings = contactSettings();
  settings.occlusionCulling = occlusion;
  settings.antialiasing = antialiasing;
  Renderer renderer{*device, shaderDir(platform), settings};
  const BoxOnPlane scene{renderer};
  const SceneView view = contactView(scene.draws);
  for (int frame = 0; frame < 2; ++frame) {
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    const GraphImage mask = renderer.contactShadows();
    REQUIRE(mask.isValid());
    REQUIRE(graph.imageDesc(mask).size == glm::uvec2{64, 48});
    REQUIRE(graph.imageDesc(mask).format == Format::R8Unorm);
    graph.execute(commands);
    device->endFrame();
  }
  const std::vector<std::string> names = passNames(graph);
  // With occlusion culling the depth is complete only after its late phase (ADR-0024).
  REQUIRE(position(names, occlusion ? "depth late" : "depth") < position(names, "contact shadows"));
  REQUIRE(position(names, "contact shadows") < position(names, "forward"));
  REQUIRE(std::ranges::count(names, "contact shadows") == 1);
  // The pre-pass gains no attachment, the normal being reconstructed from depth: it clears the
  // depth with the motion under TAA and nothing else.
  const auto &trace = static_cast<const NullDevice &>(*device).trace();
  const auto prePass = std::ranges::find_if(trace, [](const std::string &line) {
    return line.starts_with("beginRendering") && line.contains(R"(depth "viewport depth" clear)");
  });
  REQUIRE(prePass != trace.end());
  REQUIRE(*prePass == (antialiasing == AntiAliasing::Taa
                           ? R"(beginRendering color "motion" clear depth "viewport depth" clear)"
                           : R"(beginRendering depth "viewport depth" clear)"));
  renderer.destroyMesh(scene.box);
  renderer.destroyMesh(scene.plane);
}

TEST_CASE("contact shadows declare nothing unless the settings, the sun and the view's draws all ask",
          "[renderer][contact][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  RenderGraph graph{*device};
  RenderTarget target{*device, "viewport"};
  target.resize({32, 32});
  const RendererSettings settings = contactSettings();
  // Whether a frame of the box on the plane, with `change` made to the settings and the view, traces.
  const auto withScene = [&](auto &&change) {
    RendererSettings changed = settings;
    SceneView view;
    change(changed, view); // only to read the settings; the view is rebuilt below
    Renderer renderer{*device, shaderDir(platform), changed};
    const BoxOnPlane scene{renderer};
    view = contactView(scene.draws);
    change(changed, view);
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    const bool mask = renderer.contactShadows().isValid();
    graph.execute(commands);
    device->endFrame();
    REQUIRE(has(passNames(graph), "contact shadows") == mask);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
    return mask;
  };
  REQUIRE(withScene([](RendererSettings &, SceneView &) {}));
  REQUIRE_FALSE(withScene([](RendererSettings &s, SceneView &) { s.contactShadows = false; }));
  REQUIRE_FALSE(withScene([](RendererSettings &s, SceneView &) { s.shadows = false; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadows = false; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.hasSun = false; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadowLength = 0.0f; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadowThickness = 0.0f; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadowLength = -1.0f; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.sun.direction = glm::vec3{0.0f}; }));
  REQUIRE_FALSE(withScene([](RendererSettings &, SceneView &v) { v.draws = {}; }));
  // A non-finite value takes the field's default rather than turning the effect off, and a step count
  // of 0 is clamped to 1.
  REQUIRE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadowLength = std::nanf(""); }));
  REQUIRE(withScene([](RendererSettings &, SceneView &v) { v.sun.contactShadowThickness = INFINITY; }));
  REQUIRE(withScene([](RendererSettings &s, SceneView &) { s.contactShadowSteps = 0; }));
  // Only blended draws: nothing is in the pre-pass to trace through.
  {
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    MaterialDesc glass;
    glass.alphaMode = AlphaMode::Blend;
    glass.baseColor.a = 0.5f;
    const MaterialHandle material = renderer.createMaterial(glass, "glass");
    const std::array draws{DrawItem{.mesh = box, .material = material}};
    const SceneView view = contactView(draws);
    ICommandList &commands = device->beginFrame();
    graph.reset();
    renderer.addScenePasses(graph, view, graph.importImage(target.color()), graph.importImage(target.depth()));
    REQUIRE_FALSE(renderer.contactShadows().isValid());
    graph.execute(commands);
    device->endFrame();
    REQUIRE_FALSE(has(passNames(graph), "contact shadows"));
    renderer.destroyMaterial(material);
    renderer.destroyMesh(box);
  }
  REQUIRE(RendererSettings{}.contactShadows);
  REQUIRE(RendererSettings{}.contactShadowSteps == 16);
  REQUIRE_FALSE(DirectionalLight{}.contactShadows);
  REQUIRE(DirectionalLight{}.contactShadowLength == 0.2f);
  REQUIRE(DirectionalLight{}.contactShadowThickness == 0.02f);
}

TEST_CASE("each view traces its own mask at its own size, odd and one-pixel ones included",
          "[renderer][contact][views][null]") {
  sonnet::platform::Platform platform{{.headless = true}};
  const auto device = createNullDevice();
  Renderer renderer{*device, shaderDir(platform), contactSettings()};
  const BoxOnPlane scene{renderer};
  const SceneView first = contactView(scene.draws);
  const SceneView second = contactView(scene.draws);
  RenderGraph graph{*device};
  RenderTarget a{*device, "a"};
  RenderTarget b{*device, "b"};
  const glm::uvec2 sizeA = GENERATE(glm::uvec2{1, 1}, glm::uvec2{63, 37}, glm::uvec2{64, 48});
  const glm::uvec2 sizeB{17, 9};
  a.resize(sizeA);
  b.resize(sizeB);
  ICommandList &commands = device->beginFrame();
  graph.reset();
  renderer.addScenePasses(graph, first, graph.importImage(a.color()), graph.importImage(a.depth()));
  renderer.addScenePasses(graph, second, graph.importImage(b.color()), graph.importImage(b.depth()));
  const GraphImage maskA = renderer.contactShadows(0);
  const GraphImage maskB = renderer.contactShadows(1);
  REQUIRE(maskA.isValid());
  REQUIRE(maskB.isValid());
  REQUIRE(maskA.index != maskB.index);
  REQUIRE(graph.imageDesc(maskA).size == sizeA);
  REQUIRE(graph.imageDesc(maskB).size == sizeB);
  graph.execute(commands);
  device->endFrame();
  REQUIRE(std::ranges::count(passNames(graph), "contact shadows") == 2);
  renderer.destroyMesh(scene.box);
  renderer.destroyMesh(scene.plane);
}

namespace {

constexpr glm::uvec2 GpuSize{128, 128};

// The ground three centimetres past the box's +X face, in its contact; the ground on its sunlit -X
// side; open ground further off; and the middle of the box's top.
constexpr glm::vec3 InContact{0.08f, 0.0f, 0.0f};
constexpr glm::vec3 SunSide{-0.1f, 0.0f, 0.0f};
constexpr glm::vec3 OpenGround{0.3f, 0.0f, 0.2f};
constexpr glm::vec3 BoxTop{0.0f, 0.1f, 0.0f};

} // namespace

TEST_CASE("a small box on a plane gets the contact its coarse shadow map misses on a GPU", "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = contactSettings();
    settings.debugView = DebugView::ShadowFactor;
    Renderer renderer{*device, shaderDir(platform), settings};
    const BoxOnPlane scene{renderer};
    SceneView view = contactView(scene.draws);
    view.camera.projectionKind = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    ContactScene gpu{*device, renderer, GpuSize};
    const glm::ivec2 contact = pixelOf(view, GpuSize, InContact);
    const glm::ivec2 sunSide = pixelOf(view, GpuSize, SunSide);
    const glm::ivec2 open = pixelOf(view, GpuSize, OpenGround);
    const glm::ivec2 top = pixelOf(view, GpuSize, BoxTop);

    view.sun.contactShadows = false;
    gpu.render(view);
    REQUIRE_FALSE(gpu.maskDrawn);
    // The cascades alone leave the ground at the box's foot lit.
    REQUIRE(gpu.red(contact) > encoded(0.8f));

    view.sun.contactShadows = true;
    gpu.render(view);
    REQUIRE(gpu.maskDrawn);
    REQUIRE(gpu.mask(contact) < 32);
    REQUIRE(gpu.red(contact) < encoded(0.15f));
    // Nothing on the sunlit side, on open ground or on the box's own lit top.
    REQUIRE(gpu.mask(sunSide) == 255);
    REQUIRE(gpu.mask(open) == 255);
    REQUIRE(gpu.mask(top) >= 250);
    REQUIRE(gpu.red(sunSide) > encoded(0.8f));
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("an isolated plane casts no contact shadow on itself on a GPU", "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), contactSettings()};
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    const std::array draws{DrawItem{.mesh = plane}};
    SceneView view = contactView(draws);
    view.camera.projectionKind = GENERATE(ProjectionKind::Perspective, ProjectionKind::Orthographic);
    // From above and from a grazing angle, with the sun high and low.
    view.camera.position = GENERATE(glm::vec3{0.0f, 0.35f, 0.6f}, glm::vec3{0.0f, 0.05f, 2.0f});
    view.camera.rotation = glm::quatLookAt(glm::normalize(-view.camera.position), glm::vec3{0.0f, 1.0f, 0.0f});
    view.sun.direction =
        GENERATE(glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f}), glm::normalize(glm::vec3{0.3f, -0.05f, 1.0f}));
    ContactScene gpu{*device, renderer, GpuSize};
    gpu.render(view);
    REQUIRE(gpu.maskDrawn);
    REQUIRE(std::ranges::min(gpu.maskImage()) == 255);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("a thin post in front of a wall leaves no dark halo around its silhouette on a GPU",
          "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), contactSettings()};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({10.0f, 10.0f}), "plane");
    // A wall a metre behind a three-centimetre post, the sun behind the camera and to one side, so
    // the wall's rays towards it pass behind the post on screen.
    const std::array draws{
        DrawItem{.mesh = plane,
                 .transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, -1.0f}) *
                              glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f})},
        DrawItem{.mesh = box, .transform = glm::scale(glm::mat4{1.0f}, {0.03f, 1.0f, 0.03f})}};
    SceneView view = contactView(draws);
    view.camera.position = {0.0f, 0.0f, 1.2f};
    view.camera.rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    view.sun.direction = glm::normalize(glm::vec3{GENERATE(0.3f, -0.3f), -0.2f, -1.0f});
    view.sun.contactShadowLength = 0.5f;
    view.sun.contactShadowThickness = 0.1f;
    ContactScene gpu{*device, renderer, GpuSize};
    gpu.render(view);
    REQUIRE(gpu.maskDrawn);
    REQUIRE(std::ranges::min(gpu.maskImage()) >= 250);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the contact follows the sun's direction, the ray's length and the thickness on a GPU",
          "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    Renderer renderer{*device, shaderDir(platform), contactSettings()};
    const BoxOnPlane scene{renderer};
    SceneView view = contactView(scene.draws);
    ContactScene gpu{*device, renderer, GpuSize};
    const glm::ivec2 plusX = pixelOf(view, GpuSize, InContact);
    const glm::ivec2 minusX = pixelOf(view, GpuSize, -InContact);

    gpu.render(view);
    const std::size_t standard = darkCount(gpu.maskImage());
    REQUIRE(gpu.mask(plusX) < 32);
    REQUIRE(gpu.mask(minusX) == 255);

    // The sun from +X puts the contact on the box's -X side.
    view.sun.direction = glm::normalize(glm::vec3{-1.0f, -1.0f, 0.0f});
    gpu.render(view);
    REQUIRE(gpu.mask(plusX) == 255);
    REQUIRE(gpu.mask(minusX) < 32);
    view.sun.direction = glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f});

    // A ray of two centimetres reaches a much smaller contact.
    view.sun.contactShadowLength = 0.02f;
    view.sun.contactShadowThickness = 0.02f;
    gpu.render(view);
    const std::size_t shorter = darkCount(gpu.maskImage());
    REQUIRE(shorter * 3 < standard);
    REQUIRE(shorter > 0);
    view.sun.contactShadowLength = 0.2f;

    // A thickness below the self-intersection tolerance accepts no hit at all.
    view.sun.contactShadowThickness = 0.001f;
    gpu.render(view);
    REQUIRE(gpu.maskDrawn);
    REQUIRE(darkCount(gpu.maskImage()) == 0);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("the sun's visibility is the minimum of the cascades' and the contact's on a GPU",
          "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = contactSettings();
    settings.debugView = DebugView::ShadowFactor;
    // A fine first cascade, so both terms are partial somewhere around the box; or cascades that end
    // before the box, where theirs is 1 and the contact still darkens.
    const bool outsideCascades = GENERATE(false, true);
    settings.shadowMapSize = 1024;
    settings.shadowDistance = outsideCascades ? 0.3f : 2.0f;
    Renderer renderer{*device, shaderDir(platform), settings};
    const BoxOnPlane scene{renderer};
    SceneView view = contactView(scene.draws);
    ContactScene gpu{*device, renderer, GpuSize};
    view.sun.contactShadows = false;
    gpu.render(view);
    const std::vector<std::byte> cascades = gpu.colorImage();
    view.sun.contactShadows = true;
    gpu.render(view);
    const std::vector<std::byte> combined = gpu.colorImage();
    const std::vector<int> mask = gpu.maskImage();
    int worst = 0;
    std::size_t partial = 0;
    std::size_t unshadowed = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
      const int cascade = std::to_integer<int>(cascades[i * 4]);
      const int contact = encoded(static_cast<float>(mask[i]) / 255.0f);
      worst = std::max(worst, std::abs(std::to_integer<int>(combined[i * 4]) - std::min(cascade, contact)));
      partial += cascade < encoded(0.9f) && contact < encoded(0.9f) ? 1u : 0u;
      // Past the cascades every surface is unshadowed by them; the sky is the clear colour, black.
      unshadowed += cascade == 0 || cascade >= encoded(1.0f) - 1 ? 1u : 0u;
    }
    REQUIRE(worst <= 2);
    REQUIRE(darkCount(mask) > 0);
    if (outsideCascades) {
      REQUIRE(unshadowed == mask.size());
    } else {
      REQUIRE(partial > 0); // where a product would be darker than either term
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("contact shadows leave punctual, indirect and blended light alone on a GPU", "[renderer][contact][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = contactSettings();
    Renderer renderer{*device, shaderDir(platform), settings};
    const BoxOnPlane scene{renderer};
    ContactScene gpu{*device, renderer, GpuSize};
    SECTION("a sun without intensity, a point light and the ambient term draw the same either way") {
      const std::array lights{Light{.type = LightType::Point, .position = {0.1f, 0.15f, 0.05f}, .intensity = 0.5f}};
      SceneView view = contactView(scene.draws);
      view.sun.intensity = 0.0f;
      view.lights = lights;
      view.ambient = glm::vec3{0.3f};
      view.sun.contactShadows = false;
      gpu.render(view);
      const std::vector<std::byte> off = gpu.colorImage();
      view.sun.contactShadows = true;
      gpu.render(view);
      REQUIRE(gpu.maskDrawn);
      REQUIRE(gpu.mask(pixelOf(view, GpuSize, InContact)) < 32); // traced, and not applied to them
      REQUIRE(gpu.colorImage() == off);
    }
    SECTION("a blended surface over the contact keeps its own sun visibility") {
      settings.debugView = DebugView::ShadowFactor;
      renderer.setSettings(settings);
      MaterialDesc glass;
      glass.alphaMode = AlphaMode::Blend;
      const MaterialHandle material = renderer.createMaterial(glass, "glass");
      // A four-centimetre pane above the box's top, on the line from the camera to the contact.
      SceneView view = contactView(scene.draws);
      const glm::vec3 pane = glm::mix(view.camera.position, InContact, 0.6f);
      std::vector<DrawItem> draws = scene.draws;
      draws.push_back(DrawItem{.mesh = scene.plane,
                               .material = material,
                               .transform = glm::translate(glm::mat4{1.0f}, pane) *
                                            glm::scale(glm::mat4{1.0f}, glm::vec3{0.004f})});
      view.draws = draws;
      gpu.render(view);
      const glm::ivec2 onPane = pixelOf(view, GpuSize, pane);
      REQUIRE(gpu.mask(onPane) < 32); // the ground's contact, behind the pane
      REQUIRE(gpu.red(onPane) > encoded(0.8f));
      renderer.destroyMaterial(material);
    }
    SECTION("with shadows off there is no trace and the sun is unshadowed") {
      settings.debugView = DebugView::ShadowFactor;
      settings.shadows = false;
      renderer.setSettings(settings);
      const SceneView view = contactView(scene.draws);
      gpu.render(view);
      REQUIRE_FALSE(gpu.maskDrawn);
      REQUIRE(gpu.red(pixelOf(view, GpuSize, InContact)) == encoded(1.0f));
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("without jitter the steps keep one pattern, and a reference jitter repeats its own",
          "[renderer][contact][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = contactSettings();
    settings.antialiasing = AntiAliasing::Taa;
    Renderer renderer{*device, shaderDir(platform), settings};
    const BoxOnPlane scene{renderer};
    SceneView view = contactView(scene.draws);
    ContactScene gpu{*device, renderer, GpuSize};
    // Under TAA the eight jitter phases move the steps: the masks of a cycle are not all one.
    std::vector<std::vector<int>> cycle;
    for (int frame = 0; frame < 8; ++frame) {
      gpu.render(view);
      cycle.push_back(gpu.maskImage());
    }
    REQUIRE(std::ranges::any_of(cycle, [&](const std::vector<int> &mask) { return mask != cycle.front(); }));

    // No anti-aliasing, FXAA and a debug view of the unresolved frame draw without jitter, one pattern.
    for (const auto &[mode, debug] :
         {std::pair{AntiAliasing::None, DebugView::Final}, std::pair{AntiAliasing::Fxaa, DebugView::Final},
          std::pair{AntiAliasing::Taa, DebugView::ShadowFactor}}) {
      RendererSettings still = settings;
      still.antialiasing = mode;
      still.debugView = debug;
      renderer.setSettings(still);
      gpu.render(view);
      const std::vector<int> first = gpu.maskImage();
      REQUIRE(renderer.jitter() == glm::vec2{0.0f});
      gpu.render(view, 3);
      REQUIRE(gpu.maskImage() == first);
      REQUIRE(darkCount(first) > 0);
    }

    // A reference render takes the jitter it is given and the pattern that goes with it, every time,
    // without a history.
    renderer.setSettings(settings);
    view.referenceJitter = glm::vec2{0.25f, -1.0f / 6.0f};
    gpu.render(view);
    const std::vector<int> reference = gpu.maskImage();
    REQUIRE_FALSE(renderer.resolved().isValid());
    gpu.render(view, 2);
    REQUIRE(gpu.maskImage() == reference);
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

TEST_CASE("under TAA the contact holds still through the jitter cycle and lets go of a moved box",
          "[renderer][contact][taa][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings = contactSettings();
    settings.antialiasing = AntiAliasing::Taa;
    Renderer renderer{*device, shaderDir(platform), settings};
    RendererSettings referenceSettings = settings;
    referenceSettings.antialiasing = AntiAliasing::None;
    Renderer referenceRenderer{*device, shaderDir(platform), referenceSettings};
    const BoxOnPlane scene{renderer};
    const BoxOnPlane referenceScene{referenceRenderer};
    SceneView view = contactView(scene.draws);
    SceneView reference = contactView(referenceScene.draws);
    ContactScene gpu{*device, renderer, GpuSize};
    ContactScene spatial{*device, referenceRenderer, GpuSize};
    const glm::ivec2 contact = pixelOf(view, GpuSize, InContact);
    const glm::ivec2 open = pixelOf(view, GpuSize, OpenGround);

    // Settled for 32 frames, then a whole cycle of the eight jitter phases.
    gpu.render(view, 32);
    std::vector<int> held;
    float referenceSum = 0.0f;
    for (int frame = 0; frame < 8; ++frame) {
      gpu.render(view);
      held.push_back(gpu.red(contact));
      // The spatial reference: the same pose and jitter, drawn without a history.
      reference.referenceJitter = renderer.jitter();
      spatial.render(reference);
      referenceSum += static_cast<float>(spatial.red(contact));
    }
    CAPTURE(held, referenceSum / 8.0f);
    const auto [low, high] = std::ranges::minmax(held);
    REQUIRE(high - low <= 3);
    REQUIRE(std::abs(static_cast<float>(low + high) * 0.5f - referenceSum / 8.0f) <= 6.0f);
    const int lit = gpu.red(open);
    REQUIRE(high * 2 < lit);

    // The box moves ten centimetres along +Z: within four frames its old contact is the open ground
    // the history held it over, and its new one is dark.
    const glm::ivec2 moved = pixelOf(view, GpuSize, InContact + glm::vec3{0.0f, 0.0f, 0.15f});
    std::vector<DrawItem> draws = scene.draws;
    draws[1].previousTransform = draws[1].transform;
    draws[1].transform = glm::translate(glm::mat4{1.0f}, {0.0f, 0.0f, 0.15f}) * draws[1].transform;
    view.draws = draws;
    gpu.render(view);
    draws[1].previousTransform = draws[1].transform;
    gpu.render(view, 3);
    CAPTURE(gpu.red(contact), gpu.red(moved), lit);
    REQUIRE(std::abs(gpu.red(contact) - lit) <= 6);
    REQUIRE(gpu.red(moved) * 2 < lit);

    // Removed: the region it vacated recovers within four frames as well.
    view.draws = std::span{scene.draws}.first(1);
    gpu.render(view, 4);
    CAPTURE(gpu.red(moved));
    REQUIRE(std::abs(gpu.red(moved) - lit) <= 6);
    REQUIRE(device->validationMessageCount() == 0);
    referenceRenderer.destroyMesh(referenceScene.box);
    referenceRenderer.destroyMesh(referenceScene.plane);
    renderer.destroyMesh(scene.box);
    renderer.destroyMesh(scene.plane);
  }
  REQUIRE(device->validationMessageCount() == 0);
}

// What the contact shadows cost (ADR-0039): `renderer_tests "[contact-cost]"` draws a ground that
// fills the lower view under four hundred boxes and a sky, at 1080p with the default settings (TAA,
// occlusion culling, 2048 cascades), and prints the median and p95 of whole GPU frames and of the pass,
// with the sun's request off and on in alternating pairs. Headless, so no present paces the GPU.
TEST_CASE("the cost of the contact shadows at 1080p", "[.][benchmark][contact-cost][gpu]") {
  sonnet::platform::Platform platform{{.headless = true}};
  std::unique_ptr<IDevice> device = gpuDevice(platform);
  {
    RendererSettings settings;
    settings.contactShadowSteps = GENERATE(16u, 8u);
    Renderer renderer{*device, shaderDir(platform), settings};
    const MeshHandle box = renderer.createMesh(primitives::box(), "box");
    const MeshHandle plane = renderer.createMesh(primitives::plane({200.0f, 200.0f}), "plane");
    std::vector<DrawItem> draws{DrawItem{.mesh = plane}};
    for (int row = 0; row < 20; ++row) {
      for (int column = 0; column < 20; ++column) {
        const glm::vec3 position{static_cast<float>(column - 10) * 2.0f, 0.4f, -static_cast<float>(row) * 2.0f - 2.0f};
        draws.push_back(
            {.mesh = box,
             .transform = glm::translate(glm::mat4{1.0f}, position) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.8f})});
      }
    }
    SceneView view;
    view.camera.position = {0.0f, 1.7f, 2.0f};
    view.camera.rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    view.sun.direction = glm::normalize(glm::vec3{0.5f, -0.75f, -0.4f});
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
    const auto rank = [](std::vector<float> &values, float fraction) {
      std::ranges::sort(values);
      const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<float>(values.size())));
      return values[std::max<std::size_t>(index, 1) - 1];
    };
    WARN(std::format("{} steps on {}", settings.contactShadowSteps, device->info().deviceName));
    for (int pair = 0; pair < 3; ++pair) {
      for (const bool contact : pair % 2 == 0 ? std::array{false, true} : std::array{true, false}) {
        view.sun.contactShadows = contact;
        for (int warm = 0; warm < 120; ++warm) {
          frame();
        }
        std::vector<float> totals;
        std::vector<float> pass;
        for (int measured = 0; measured < 300; ++measured) {
          frame();
          float total = 0.0f;
          bool whole = true;
          for (const PassTiming &timing : graph.statistics().passes) {
            whole = whole && timing.gpuAvailable;
            total += timing.gpuMilliseconds;
            if (timing.name == "contact shadows" && timing.gpuAvailable) {
              pass.push_back(timing.gpuMilliseconds);
            }
          }
          if (whole) {
            totals.push_back(total);
          }
        }
        REQUIRE(!totals.empty());
        WARN(std::format("  pair {} contact {:3}: frame median {:.4f} p95 {:.4f} ms ({} whole); pass {}", pair,
                         contact ? "on" : "off", rank(totals, 0.5f), rank(totals, 0.95f), totals.size(),
                         pass.empty() ? std::string{"-"}
                                      : std::format("median {:.4f} p95 {:.4f} ms ({} samples)", rank(pass, 0.5f),
                                                    rank(pass, 0.95f), pass.size())));
      }
    }
    REQUIRE(device->validationMessageCount() == 0);
    renderer.destroyMesh(plane);
    renderer.destroyMesh(box);
  }
  REQUIRE(device->validationMessageCount() == 0);
}
