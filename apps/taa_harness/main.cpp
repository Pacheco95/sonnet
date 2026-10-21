// sonnet_taa_harness: the TAA checks of ADR-0033 ("TAA"; docs/rendering.md, "TAA checks"). For each
// case of a project's taa-cases.json it plays the scene twice from the start at a fixed step: once
// under TAA, keeping the display-encoded frames, and once with TAA off, where each measured frame
// holds the simulation still and renders a set of jittered samples without temporal
// accumulation, averages them in linear HDR and tone maps the average with the renderer's own
// pass. runtime::taa then judges the difference in the glass's region of every measured frame.
#include <sonnet/core/Error.h>
#include <sonnet/core/Log.h>
#include <sonnet/platform/Platform.h>
#include <sonnet/platform/Window.h>
#include <sonnet/renderer/RenderGraph.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/Device.h>
#include <sonnet/rhi/Swapchain.h>
#include <sonnet/runtime/Capture.h>
#include <sonnet/runtime/Game.h>
#include <sonnet/runtime/Screenshot.h>
#include <sonnet/runtime/TaaCheck.h>

#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sonnet;
using json = nlohmann::json;

constexpr const char *Usage = "usage: sonnet_taa_harness <project> --out <directory> [--case <name>]... [--size WxH] "
                              "[--reference-samples N] [--glass-motion] [--keep-frames]";

// The jitter of the reference samples: Halton (2, 3), centred on the pixel, 16 of them unless
// `--reference-samples` says otherwise (8 are TAA's own sequence, which tells the sample set apart
// from the resolve in an error).
constexpr std::uint32_t DefaultReferenceSamples = 16;
constexpr std::uint32_t MaxReferenceSamples = 64;
// Frames drawn after the assets have loaded and before the simulation starts, so the environment
// and the pipelines are ready (what a capture run settles for).
constexpr std::uint32_t SettleFrames = 10;
// Whole-pixel reference matching is a diagnostic, not an error floor (#352).
constexpr int MotionSearchRadius = 6;
constexpr float FrameSeconds = runtime::CaptureRun::FrameSeconds;

struct Case {
  std::string name;
  std::string description;
  std::filesystem::path scene; // relative to the project
  std::uint32_t warmup{32};    // frames played and not judged: TAA history converges
  std::uint32_t measured{16};
  std::uint32_t substeps{4}; // updates per frame; the glass is masked at each, for the swept region
  bool diagnostic{false};    // reported, and not counted in the exit code
};

struct Arguments {
  std::filesystem::path project;
  std::filesystem::path output;
  std::vector<std::string> cases;
  glm::uvec2 size{1280, 720};
  std::uint32_t referenceSamples{DefaultReferenceSamples};
  bool glassMotion{false}; // the glass writes its own motion (#352)
  bool keepFrames{false};
};

[[nodiscard]] std::optional<Arguments> parse(const std::vector<std::string_view> &args) {
  Arguments parsed;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    const bool hasValue = i + 1 < args.size();
    if (arg == "--out" && hasValue) {
      parsed.output = args[++i];
    } else if (arg == "--case" && hasValue) {
      parsed.cases.emplace_back(args[++i]);
    } else if (arg == "--size" && hasValue) {
      const std::optional<glm::uvec2> size = runtime::parseRenderSize(args[++i]);
      if (!size) {
        return std::nullopt;
      }
      parsed.size = *size;
    } else if (arg == "--reference-samples" && hasValue) {
      const std::string_view value = args[++i];
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed.referenceSamples);
      if (error != std::errc{} || end != value.data() + value.size() || parsed.referenceSamples == 0 ||
          parsed.referenceSamples > MaxReferenceSamples) {
        return std::nullopt;
      }
    } else if (arg == "--glass-motion") {
      parsed.glassMotion = true;
    } else if (arg == "--keep-frames") {
      parsed.keepFrames = true;
    } else if (!arg.starts_with("--") && parsed.project.empty()) {
      parsed.project = arg;
    } else {
      return std::nullopt;
    }
  }
  if (parsed.project.empty() || parsed.output.empty()) {
    return std::nullopt;
  }
  return parsed;
}

[[nodiscard]] std::vector<Case> readCases(const std::filesystem::path &project) {
  std::ifstream file{project / "taa-cases.json"};
  if (!file) {
    throw std::runtime_error{std::format("{} has no taa-cases.json", project.string())};
  }
  const json document = json::parse(file);
  std::vector<Case> cases;
  for (const json &entry : document.at("cases")) {
    Case parsed;
    parsed.name = entry.at("name").get<std::string>();
    parsed.description = entry.value("description", std::string{});
    parsed.scene = entry.at("scene").get<std::string>();
    parsed.warmup = entry.value("warmup", parsed.warmup);
    parsed.measured = entry.value("measured", parsed.measured);
    parsed.substeps = entry.value("substeps", parsed.substeps);
    parsed.diagnostic = entry.value("diagnostic", false);
    cases.push_back(std::move(parsed));
  }
  return cases;
}

// The index-th term of the Halton sequence of `base`.
[[nodiscard]] float halton(std::uint32_t index, std::uint32_t base) {
  float result = 0.0f;
  float fraction = 1.0f;
  while (index > 0) {
    fraction /= static_cast<float>(base);
    result += fraction * static_cast<float>(index % base);
    index /= base;
  }
  return result;
}

[[nodiscard]] glm::vec2 referenceJitter(std::uint32_t sample) {
  return glm::vec2{halton(sample + 1, 2), halton(sample + 1, 3)} - glm::vec2{0.5f};
}

// What the harness draws with: the window and device are the process's, the Game is per run.
struct Rig {
  rhi::IDevice &device;
  const rhi::ISwapchain &swapchain;
  platform::IWindow &window;
  std::filesystem::path project;
  glm::uvec2 size;
  std::uint32_t referenceSamples;
  bool glassMotion;
};

// A pass copying a graph image into a buffer the CPU reads after the frame.
struct Readback {
  rhi::BufferHandle buffer;
  std::uint64_t bytes{0};
};

Readback addReadback(rhi::IDevice &device, renderer::RenderGraph &graph, renderer::GraphImage image,
                     std::uint64_t bytes) {
  const Readback readback{.buffer = device.createBuffer({.size = bytes,
                                                         .usage = rhi::BufferUsage::TransferDst,
                                                         .memory = rhi::MemoryUsage::GpuToCpu,
                                                         .debugName = "taa readback"}),
                          .bytes = bytes};
  graph.addPass(
      "taa readback", [image](renderer::PassBuilder &b) { b.transferSrc(image); },
      [&device, image, readback](rhi::ICommandList &commands, const renderer::PassResources &resources) {
        commands.copyImageToBuffer(resources.image(image), readback.buffer);
        static_cast<void>(device);
      });
  return readback;
}

std::vector<std::byte> takeReadback(rhi::IDevice &device, const Readback &readback) {
  const std::span<std::byte> mapped = device.mappedRange(readback.buffer);
  std::vector<std::byte> bytes{mapped.begin(), mapped.begin() + static_cast<std::ptrdiff_t>(readback.bytes)};
  device.destroyBuffer(readback.buffer);
  return bytes;
}

// One scene played at a fixed step.
class Run {
public:
  Run(const Rig &rig, const Case &testCase, renderer::AntiAliasing antialiasing)
      : m_rig(rig), m_case(testCase), m_game(makeGame(rig, antialiasing)) {
    m_game->target().resize(rig.size);
    if (const auto opened = m_game->open(rig.project); !opened) {
      throw std::runtime_error{opened.error().toString()};
    }
    if (const auto opened = m_game->openScene(testCase.scene); !opened) {
      throw std::runtime_error{opened.error().toString()};
    }
    // Paused, so the scene is loaded and settled before its clock starts. A draw is what asks for
    // an asset, so one frame goes first and the loading is waited out after it.
    do {
      drawPlain();
    } while (!m_game->target().isValid() || m_game->assets().loading());
    for (std::uint32_t i = 0; i < SettleFrames; ++i) {
      drawPlain();
    }
    m_game->scripts().seedRandom(runtime::CaptureRun::RandomSeed);
    m_game->play();
  }

  [[nodiscard]] runtime::Game &game() noexcept {
    return *m_game;
  }

  // One frame of the scene: `substeps` updates of an equal share of FrameSeconds.
  // `afterSubstep(index)` runs after each, with the view it built.
  template <typename F> void advance(F &&afterSubstep) {
    for (std::uint32_t i = 0; i < m_case.substeps; ++i) {
      // The cut reaches the view in the last update before the frame is drawn, so it is the first
      // frame of the run that starts TAA's history and its jitter sequence over, however many
      // frames the loading drew.
      if (m_cutPending && i + 1 == m_case.substeps) {
        m_game->cutHistory();
        m_cutPending = false;
      }
      m_game->update(FrameSeconds / static_cast<float>(m_case.substeps));
      afterSubstep(i);
    }
  }

  // The display-encoded scene of `view`, 8-bit RGBA at the rig's size.
  [[nodiscard]] std::vector<std::byte> display(const renderer::SceneView &view) {
    return frame(view, [&](renderer::RenderGraph &, renderer::GraphImage color) { return color; }, 4);
  }

  // The linear HDR scene of `view`, half-float RGBA at the rig's size.
  [[nodiscard]] std::vector<std::byte> hdr(const renderer::SceneView &view) {
    return frame(
        view, [&](renderer::RenderGraph &, renderer::GraphImage) { return m_game->renderer().sceneColor(); }, 8);
  }

  // The silhouette of the draws with these ids, a non-zero byte per pixel.
  [[nodiscard]] runtime::taa::Mask silhouette(const renderer::SceneView &view,
                                              std::span<const std::uint32_t> selected) {
    rhi::IDevice &device = m_rig.device;
    renderer::RenderGraph &graph = m_game->graph();
    rhi::ICommandList &commands = device.beginFrame();
    graph.reset();
    const renderer::GraphImage mask =
        graph.createImage({.size = m_rig.size, .format = renderer::Renderer::IdFormat, .debugName = "glass mask"});
    m_game->renderer().addSelectionMaskPass(graph, view, mask, selected);
    const std::uint64_t pixels = std::uint64_t{m_rig.size.x} * m_rig.size.y;
    const Readback readback = addReadback(device, graph, mask, pixels * 4);
    graph.execute(commands);
    device.endFrame();
    device.waitIdle();
    const std::vector<std::byte> ids = takeReadback(device, readback);
    runtime::taa::Mask result(pixels, 0);
    for (std::size_t i = 0; i < result.size(); ++i) {
      std::uint32_t id = 0;
      std::memcpy(&id, ids.data() + i * 4, sizeof(id));
      result[i] = id != 0 ? 1 : 0;
    }
    return result;
  }

  // The renderer's tone mapping of an HDR image of the rig's size.
  [[nodiscard]] std::vector<std::byte> toneMap(const renderer::SceneView &view, std::span<const std::byte> hdrPixels) {
    rhi::IDevice &device = m_rig.device;
    const rhi::ImageHandle image = device.createImage({.size = m_rig.size,
                                                       .format = renderer::Renderer::HdrFormat,
                                                       .usage = rhi::ImageUsage::Sampled | rhi::ImageUsage::TransferDst,
                                                       .debugName = "reference average"});
    const std::array<rhi::ImageUpload, 1> uploads{{{.data = hdrPixels}}};
    device.uploadImage(image, uploads);
    renderer::RenderGraph &graph = m_game->graph();
    rhi::ICommandList &commands = device.beginFrame();
    graph.reset();
    const renderer::GraphImage color = graph.importImage(m_game->target().color());
    const renderer::GraphImage average =
        graph.importImage(image, rhi::ImageLayout::ShaderReadOnly, rhi::ImageLayout::ShaderReadOnly);
    m_game->renderer().addTonemapPass(graph, view, average, {}, color);
    const Readback readback = addReadback(device, graph, color, std::uint64_t{m_rig.size.x} * m_rig.size.y * 4);
    graph.execute(commands);
    device.endFrame();
    device.waitIdle();
    device.destroyImage(image);
    return takeReadback(device, readback);
  }

private:
  [[nodiscard]] static std::unique_ptr<runtime::Game> makeGame(const Rig &rig, renderer::AntiAliasing antialiasing) {
    runtime::GameDesc desc;
    desc.audioOutput = false;
    desc.paused = true;
    desc.renderer.antialiasing = antialiasing;
    desc.renderer.taaGlassMotion = rig.glassMotion;
    return std::make_unique<runtime::Game>(rig.window, rig.device, rig.swapchain, desc);
  }

  void drawPlain() {
    m_game->update(FrameSeconds);
    static_cast<void>(display(m_game->view()));
  }

  // Draws `view` and reads back the image `pick` names, `bytesPerPixel` bytes each.
  template <typename F>
  [[nodiscard]] std::vector<std::byte> frame(const renderer::SceneView &view, F &&pick, std::uint64_t bytesPerPixel) {
    rhi::IDevice &device = m_rig.device;
    renderer::RenderGraph &graph = m_game->graph();
    rhi::ICommandList &commands = device.beginFrame();
    graph.reset();
    const renderer::GraphImage color = graph.importImage(m_game->target().color());
    const renderer::GraphImage depth = graph.importImage(m_game->target().depth());
    m_game->renderer().addScenePasses(graph, view, color, depth);
    const renderer::GraphImage image = pick(graph, color);
    const Readback readback =
        addReadback(device, graph, image, std::uint64_t{m_rig.size.x} * m_rig.size.y * bytesPerPixel);
    graph.execute(commands);
    device.endFrame();
    device.waitIdle();
    return takeReadback(device, readback);
  }

  const Rig &m_rig;
  const Case &m_case;
  std::unique_ptr<runtime::Game> m_game;
  bool m_cutPending{true};
};

// The ids of the draws whose material can transmit: the glass.
[[nodiscard]] std::vector<std::uint32_t> glassIds(const renderer::Renderer &renderer, const renderer::SceneView &view) {
  std::vector<std::uint32_t> ids;
  for (const renderer::DrawItem &draw : view.draws) {
    if (draw.id != 0 && renderer.isValid(draw.material) && renderer::mayTransmit(renderer.material(draw.material))) {
      ids.push_back(draw.id);
    }
  }
  std::ranges::sort(ids);
  ids.erase(std::ranges::unique(ids).begin(), ids.end());
  return ids;
}

struct Measured {
  std::vector<std::vector<std::byte>> taa;       // display-encoded, one per frame from firstContext
  std::vector<std::vector<std::byte>> reference; // the same
  std::vector<runtime::taa::GlassFrame> glass;
  std::size_t firstMeasured{0}; // the index of the first judged frame in the vectors above
};

// Frames before the first judged one whose glass masks the region reaches back to.
constexpr std::uint32_t ContextFrames = runtime::taa::HistoryFrames;

[[nodiscard]] Measured runTaa(const Rig &rig, const Case &testCase) {
  Measured measured;
  Run run{rig, testCase, renderer::AntiAliasing::Taa};
  const std::uint32_t total = testCase.warmup + testCase.measured;
  // The view's address is the history key. The world's previous transform spans only one update;
  // the harness renders once after several updates, so motion must use the last rendered pose.
  renderer::SceneView view;
  std::vector<renderer::DrawItem> previous(run.game().view().draws.begin(), run.game().view().draws.end());
  std::vector<renderer::DrawItem> draws;
  for (std::uint32_t frame = 0; frame < total; ++frame) {
    run.advance([](std::uint32_t) {});
    view = run.game().view();
    draws.assign(view.draws.begin(), view.draws.end());
    runtime::taa::usePreviousFrameTransforms(draws, previous);
    view.draws = draws;
    std::vector<std::byte> image = run.display(view);
    previous = draws;
    if (frame >= testCase.warmup) {
      measured.taa.push_back(std::move(image));
    } else {
      measured.taa.emplace_back(); // not judged, not kept
    }
  }
  // The context frames' slots are dropped below, with the reference run's.
  return measured;
}

void runReference(const Rig &rig, const Case &testCase, Measured &measured) {
  Run run{rig, testCase, renderer::AntiAliasing::None};
  const std::uint32_t total = testCase.warmup + testCase.measured;
  const std::uint32_t first = testCase.warmup - ContextFrames;
  glm::uvec2 size = rig.size;
  const std::size_t pixels = std::size_t{size.x} * size.y;
  runtime::taa::Mask previousPose; // the glass at the last substep of the previous frame
  for (std::uint32_t frame = 0; frame < total; ++frame) {
    runtime::taa::GlassFrame glass;
    glass.swept = previousPose.empty() ? runtime::taa::Mask(pixels, 0) : previousPose;
    runtime::taa::Mask pose;
    run.advance([&](std::uint32_t) {
      if (frame < first) {
        return;
      }
      const renderer::SceneView &view = run.game().view();
      pose = run.silhouette(view, glassIds(run.game().renderer(), view));
      for (std::size_t i = 0; i < pixels; ++i) {
        glass.swept[i] = (glass.swept[i] != 0 || pose[i] != 0) ? 1 : 0;
      }
    });
    if (frame < first) {
      continue;
    }
    previousPose = pose;
    glass.coverage = std::move(pose);
    measured.glass.push_back(std::move(glass));
    if (frame < testCase.warmup) {
      measured.reference.emplace_back();
      continue;
    }
    // The simulation holds still: `referenceSamples` renders of this pose, averaged in linear HDR.
    std::vector<float> sum(pixels * 4, 0.0f);
    for (std::uint32_t sample = 0; sample < rig.referenceSamples; ++sample) {
      renderer::SceneView view = run.game().view();
      view.referenceJitter = referenceJitter(sample);
      const std::vector<std::byte> hdr = run.hdr(view);
      for (std::size_t i = 0; i < sum.size(); ++i) {
        std::uint16_t half = 0;
        std::memcpy(&half, hdr.data() + i * sizeof(half), sizeof(half));
        sum[i] += glm::unpackHalf1x16(half);
      }
    }
    std::vector<std::byte> average(sum.size() * sizeof(std::uint16_t));
    for (std::size_t i = 0; i < sum.size(); ++i) {
      const std::uint16_t half = glm::packHalf1x16(sum[i] / static_cast<float>(rig.referenceSamples));
      std::memcpy(average.data() + i * sizeof(half), &half, sizeof(half));
    }
    measured.reference.push_back(run.toneMap(run.game().view(), average));
  }
  // Both runs held `warmup + measured` slots for the TAA frames; keep the context window's.
  measured.taa.erase(measured.taa.begin(), measured.taa.begin() + first);
  measured.firstMeasured = ContextFrames;
}

// The error image as a viewer reads it: grey for outside the region, red for inside, 16 times the level.
[[nodiscard]] std::vector<std::byte> errorPicture(std::span<const std::uint8_t> errors,
                                                  const runtime::taa::Mask &region) {
  std::vector<std::byte> picture(errors.size() * 4);
  for (std::size_t i = 0; i < errors.size(); ++i) {
    const auto level = static_cast<std::uint8_t>(std::min(errors[i] * 16, 255));
    picture[i * 4 + 0] = static_cast<std::byte>(level);
    picture[i * 4 + 1] = static_cast<std::byte>(region[i] != 0 ? 0 : level);
    picture[i * 4 + 2] = static_cast<std::byte>(region[i] != 0 ? 0 : level);
    picture[i * 4 + 3] = std::byte{255};
  }
  return picture;
}

// Three images side by side: TAA, reference, error.
void writeTriptych(const std::filesystem::path &file, glm::uvec2 size, std::span<const std::byte> a,
                   std::span<const std::byte> b, std::span<const std::byte> c) {
  const glm::uvec2 wide{size.x * 3, size.y};
  std::vector<std::byte> pixels(std::size_t{wide.x} * wide.y * 4);
  for (std::uint32_t y = 0; y < size.y; ++y) {
    const std::size_t rowBytes = std::size_t{size.x} * 4;
    const std::size_t row = std::size_t{y} * rowBytes;
    const std::size_t wideRow = std::size_t{y} * wide.x * 4;
    std::memcpy(pixels.data() + wideRow, a.data() + row, rowBytes);
    std::memcpy(pixels.data() + wideRow + rowBytes, b.data() + row, rowBytes);
    std::memcpy(pixels.data() + wideRow + 2 * rowBytes, c.data() + row, rowBytes);
  }
  if (const auto written = runtime::writeScreenshot(file, wide, renderer::Renderer::ColorFormat, pixels); !written) {
    SONNET_LOG_ERROR("{}", written.error().toString());
  }
}

// Judges one case, writes its files, and says whether every measured frame passed.
[[nodiscard]] bool judge(const Rig &rig, const Case &testCase, const Measured &measured,
                         const std::filesystem::path &directory, bool keepFrames, const std::string &device) {
  std::filesystem::create_directories(directory);
  std::vector<std::vector<std::uint8_t>> errors(measured.glass.size());
  for (std::size_t i = measured.firstMeasured; i < errors.size(); ++i) {
    errors[i] = runtime::taa::errorLevels(rig.size, measured.taa[i], measured.reference[i]);
  }
  const std::vector<runtime::taa::FrameResult> results =
      runtime::taa::evaluate(rig.size, measured.glass, errors, measured.firstMeasured);

  std::ofstream csv{directory / "metrics.csv"};
  csv << "frame,glass_pixels,glass_mean,glass_p99,glass_max,opaque_mean,opaque_p99,opaque_max,vacated,"
         "unrecovered,pass,covered_pixels,covered_mean,covered_p99,uncovered_pixels,uncovered_mean,uncovered_p99,"
         "residual_unshifted_mean,residual_unshifted_p99,residual_matched_mean,residual_matched_p99\n";
  bool allPass = true;
  std::size_t worst = 0;
  for (std::size_t i = 0; i < results.size(); ++i) {
    const runtime::taa::FrameResult &r = results[i];
    csv << std::format("{},{},{:.3f},{},{},{:.3f},{},{},{},{},{},{},{:.3f},{},{},{:.3f},{}", testCase.warmup + i,
                       r.glass.pixels, r.glass.mean, r.glass.percentile99, r.glass.maximum, r.opaque.mean,
                       r.opaque.percentile99, r.opaque.maximum, r.vacated, r.unrecovered, r.pass() ? 1 : 0,
                       r.covered.pixels, r.covered.mean, r.covered.percentile99, r.uncovered.pixels, r.uncovered.mean,
                       r.uncovered.percentile99);
    // No prior reference is captured for the first measured frame. Empty cells mean unavailable,
    // not a measured zero; the remaining rows compare the same reference pair in both treatments.
    if (i > 0) {
      const runtime::taa::ReferenceMotionResidual residual = runtime::taa::referenceMotionResidual(
          rig.size, measured.reference[i + measured.firstMeasured], measured.reference[i + measured.firstMeasured - 1],
          measured.glass[i + measured.firstMeasured].coverage, MotionSearchRadius);
      csv << std::format(",{:.3f},{},{:.3f},{}", residual.unshifted.mean, residual.unshifted.percentile99,
                         residual.integerMatched.mean, residual.integerMatched.percentile99);
    } else {
      csv << ",,,,";
    }
    csv << '\n';
    allPass = allPass && r.pass();
    if (r.glass.mean > results[worst].glass.mean) {
      worst = i;
    }
  }
  // Region of the worst frame, for the picture.
  const std::size_t worstFrame = worst + measured.firstMeasured;
  runtime::taa::Mask region(std::size_t{rig.size.x} * rig.size.y, 0);
  for (std::size_t other = worstFrame - runtime::taa::HistoryFrames; other <= worstFrame; ++other) {
    for (std::size_t i = 0; i < region.size(); ++i) {
      region[i] =
          (region[i] != 0 || measured.glass[other].coverage[i] != 0 || measured.glass[other].swept[i] != 0) ? 1 : 0;
    }
  }
  region = runtime::taa::dilate(rig.size, region, runtime::taa::DilationPixels);
  writeTriptych(directory / "worst.png", rig.size, measured.taa[worstFrame], measured.reference[worstFrame],
                errorPicture(errors[worstFrame], region));
  if (keepFrames) {
    for (std::size_t i = measured.firstMeasured; i < errors.size(); ++i) {
      writeTriptych(directory / std::format("frame-{:03}.png", testCase.warmup + i - measured.firstMeasured), rig.size,
                    measured.taa[i], measured.reference[i], errorPicture(errors[i], region));
    }
  }

  json summary;
  summary["case"] = testCase.name;
  summary["description"] = testCase.description;
  summary["scene"] = testCase.scene.string();
  summary["device"] = device;
  summary["size"] = {rig.size.x, rig.size.y};
  summary["frameSeconds"] = FrameSeconds;
  summary["substeps"] = testCase.substeps;
  summary["warmupFrames"] = testCase.warmup;
  summary["measuredFrames"] = testCase.measured;
  summary["referenceSamples"] = rig.referenceSamples;
  summary["glassMotion"] = rig.glassMotion;
  summary["motionInterval"] = "rendered-frame";
  summary["referenceMotionSearchRadius"] = MotionSearchRadius;
  summary["referenceJitter"] = json::array();
  for (std::uint32_t i = 0; i < rig.referenceSamples; ++i) {
    const glm::vec2 jitter = referenceJitter(i);
    summary["referenceJitter"].push_back({jitter.x, jitter.y});
  }
  // The worst of each measure over the measured frames, which the budgets are judged on.
  const auto worstOf = [&](auto measure) {
    double result = 0.0;
    for (const runtime::taa::FrameResult &r : results) {
      result = std::max(result, static_cast<double>(measure(r)));
    }
    return result;
  };
  summary["worstMean"] = worstOf([](const auto &r) { return r.glass.mean; });
  summary["worstPercentile99"] = worstOf([](const auto &r) { return r.glass.percentile99; });
  summary["worstUnrecovered"] = worstOf([](const auto &r) { return r.unrecovered; });
  summary["worstOpaqueMean"] = worstOf([](const auto &r) { return r.opaque.mean; });
  summary["worstOpaquePercentile99"] = worstOf([](const auto &r) { return r.opaque.percentile99; });
  summary["worstFrame"] = testCase.warmup + worst;
  summary["diagnostic"] = testCase.diagnostic;
  summary["pass"] = allPass;
  std::ofstream{directory / "summary.json"} << summary.dump(2) << '\n';
  std::println("{}: {} ({} frames, worst frame {} mean {:.2f} p99 {} unrecovered {})", testCase.name,
               allPass ? "pass" : "FAIL", results.size(), testCase.warmup + worst, results[worst].glass.mean,
               results[worst].glass.percentile99, results[worst].unrecovered);
  return allPass;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const std::vector<std::string_view> args{argv + 1, argv + argc};
    const std::optional<Arguments> arguments = parse(args);
    if (!arguments) {
      std::println(stderr, "{}", Usage);
      return 2;
    }
    std::vector<Case> cases = readCases(arguments->project);
    if (!arguments->cases.empty()) {
      std::erase_if(
          cases, [&](const Case &c) { return std::ranges::find(arguments->cases, c.name) == arguments->cases.end(); });
    }
    if (cases.empty()) {
      std::println(stderr, "sonnet_taa_harness: no such case");
      return 2;
    }

    platform::Platform platform;
    const std::unique_ptr<platform::IWindow> window =
        platform.createWindow({.title = "Sonnet TAA harness", .size = {640, 360}, .resizable = false, .hidden = true});
    const std::unique_ptr<rhi::IDevice> device =
        rhi::createDevice({.platform = &platform, .applicationName = "TAA harness"});
    const std::unique_ptr<rhi::ISwapchain> swapchain = device->createSwapchain(*window);
    const Rig rig{*device,
                  *swapchain,
                  *window,
                  std::filesystem::absolute(arguments->project),
                  arguments->size,
                  arguments->referenceSamples,
                  arguments->glassMotion};
    const std::string deviceName = device->info().deviceName;

    bool allPass = true;
    for (const Case &testCase : cases) {
      Measured measured = runTaa(rig, testCase);
      runReference(rig, testCase, measured);
      const bool passed =
          judge(rig, testCase, measured, arguments->output / testCase.name, arguments->keepFrames, deviceName);
      allPass = (passed || testCase.diagnostic) && allPass;
    }
    device->waitIdle();
    return allPass ? 0 : 1;
  } catch (const std::exception &exception) {
    std::fprintf(stderr, "sonnet_taa_harness: %s\n", exception.what());
    return 1;
  }
}
