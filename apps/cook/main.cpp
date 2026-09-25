// sonnet_cook: the command-line front end of `assets::cook` (docs/assets.md, "Cooking and
// export"). The editor's export dialog runs the same function; this is what CI and a script
// reach for. A null device stands in for a GPU, so cooking needs no Vulkan implementation.
#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/assets/Project.h>
#include <sonnet/core/JobSystem.h>

#include <sonnet/platform/Platform.h>
#include <sonnet/renderer/Renderer.h>
#include <sonnet/rhi/NullDevice.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace sonnet;

constexpr const char *Usage =
    "usage: sonnet_cook <project> [--platform windows|linux|macos] [--out <directory>] [--scene <scene>]";

struct Arguments {
  std::filesystem::path project;
  std::filesystem::path output;
  assets::CookPlatform platform{assets::hostPlatform()};
  std::optional<std::filesystem::path> scene;
};

// Returns nothing and prints why when the command line does not parse.
[[nodiscard]] std::optional<Arguments> parse(std::span<const std::string_view> args) {
  Arguments parsed;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view argument = args[i];
    const auto value = [&]() -> std::string_view { return i + 1 < args.size() ? args[++i] : std::string_view{}; };
    if (argument == "--platform") {
      const std::string_view name = value();
      const auto platform = assets::cookPlatformFromString(name);
      if (!platform) {
        std::println(stderr, "sonnet_cook: unknown platform \"{}\"", name);
        return std::nullopt;
      }
      parsed.platform = *platform;
    } else if (argument == "--out") {
      parsed.output = value();
    } else if (argument == "--scene") {
      const std::string_view scene = value();
      if (scene.empty()) {
        std::println(stderr, "sonnet_cook: --scene needs a path");
        return std::nullopt;
      }
      parsed.scene = scene;
    } else if (argument.starts_with("--")) {
      std::println(stderr, "sonnet_cook: unknown option \"{}\"", argument);
      return std::nullopt;
    } else if (parsed.project.empty()) {
      parsed.project = argument;
    } else {
      std::println(stderr, "sonnet_cook: more than one project given");
      return std::nullopt;
    }
  }
  if (parsed.project.empty()) {
    std::println(stderr, "sonnet_cook: no project given");
    return std::nullopt;
  }
  if (parsed.output.empty()) {
    parsed.output = parsed.project / "export" / std::string{assets::toString(parsed.platform)};
  }
  return parsed;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const std::vector<std::string_view> args{argv + 1, argv + argc};
    const auto arguments = parse(args);
    if (!arguments) {
      std::println(stderr, "{}", Usage);
      return 2;
    }

    // Headless: cooking opens no window, and the null device gives the renderer somewhere to
    // put what the importers upload (ADR-0011).
    platform::Platform platform{{.headless = true}};
    const auto project = assets::Project::open(arguments->project);
    if (!project) {
      std::println(stderr, "sonnet_cook: {}", project.error().toString());
      return 1;
    }
    const auto device = rhi::createNullDevice();
    renderer::Renderer renderer{*device, platform.basePath() / "shaders"};
    // No workers: the cook drives the synchronous loaders and wants them done on return.
    core::JobSystem jobs{{.workerCount = 0}};
    assets::AssetDatabase database{renderer, jobs};
    database.open(project->root, project->assetRoots);

    const auto report = assets::cook(
        database, *project,
        {.outputDirectory = arguments->output, .platform = arguments->platform, .scene = arguments->scene});
    if (!report) {
      std::println(stderr, "sonnet_cook: {}", report.error().toString());
      return 1;
    }
    std::println("{}: {} assets, {} scenes and prefabs, {} bytes", report->bundle.string(), report->assetCount,
                 report->fileCount, report->bytes);
    if (report->meshes.verticesBefore > 0) {
      std::println("meshes: {} vertices welded to {}", report->meshes.verticesBefore, report->meshes.verticesAfter);
    }
    for (const std::string &warning : report->warnings) {
      std::println(stderr, "warning: {}", warning);
    }
    return 0;
  } catch (const std::exception &exception) {
    std::fprintf(stderr, "sonnet_cook: %s\n", exception.what());
    return 1;
  } catch (...) {
    std::fputs("sonnet_cook: unknown exception\n", stderr);
    return 1;
  }
}
