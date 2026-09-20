#include <sonnet/editor/Export.h>

#include <sonnet/assets/AssetDatabase.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <format>

namespace sonnet::editor {

namespace {

namespace fs = std::filesystem;

// Copies one file, overwriting what is there. A failure is a message, not an exception.
[[nodiscard]] std::optional<std::string> copyFile(const fs::path &from, const fs::path &to) {
  std::error_code error;
  fs::create_directories(to.parent_path(), error);
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
  if (error) {
    return std::format("{}: {}", from.string(), error.message());
  }
  return std::nullopt;
}

// The engine shaders, which the player loads from `shaders/` beside its binary.
std::uint32_t copyShaders(const fs::path &from, const fs::path &to, std::vector<std::string> &warnings) {
  std::error_code error;
  if (!fs::is_directory(from, error)) {
    warnings.push_back(std::format("{}: no compiled shaders to copy next to the player", from.string()));
    return 0;
  }
  std::uint32_t copied = 0;
  for (const auto &entry : fs::directory_iterator{from, error}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (const auto problem = copyFile(entry.path(), to / entry.path().filename())) {
      warnings.push_back(*problem);
    } else {
      ++copied;
    }
  }
  return copied;
}

// Windows resolves a binary's DLLs from its own directory, so they travel with it; the other two
// desktop platforms link what they need from the system or carry it in the binary.
std::uint32_t copyRuntimeLibraries(assets::CookPlatform platform, const fs::path &from, const fs::path &to,
                                   std::vector<std::string> &warnings) {
  if (platform != assets::CookPlatform::Windows) {
    return 0;
  }
  std::error_code error;
  std::uint32_t copied = 0;
  for (const auto &entry : fs::directory_iterator{from, error}) {
    if (entry.is_regular_file() && entry.path().extension() == ".dll") {
      if (const auto problem = copyFile(entry.path(), to / entry.path().filename())) {
        warnings.push_back(*problem);
      } else {
        ++copied;
      }
    }
  }
  return copied;
}

} // namespace

std::string playerFileName(assets::CookPlatform platform) {
  return platform == assets::CookPlatform::Windows ? "sonnet_player.exe" : "sonnet_player";
}

core::Result<ExportReport> exportProject(assets::AssetDatabase &database, const assets::Project &project,
                                         const ExportOptions &options) {
  SONNET_ZONE();
  ExportReport report;
  auto cooked =
      assets::cook(database, project, {.outputDirectory = options.outputDirectory, .platform = options.platform});
  if (!cooked) {
    return std::unexpected(cooked.error());
  }
  report.cook = std::move(*cooked);
  report.warnings = report.cook.warnings;

  const fs::path source = options.playerDirectory;
  const fs::path player = source / playerFileName(options.platform);
  std::error_code error;
  if (fs::is_regular_file(player, error)) {
    if (const auto problem = copyFile(player, options.outputDirectory / player.filename())) {
      report.warnings.push_back(*problem);
    } else {
      report.player = options.outputDirectory / player.filename();
      // The copy has to keep its executable bit, which copy_file preserves, and be runnable.
      fs::permissions(report.player, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                      fs::perm_options::add, error);
    }
  } else {
    // Cross-compiling a player is not the editor's job: the bundle is written either way and a
    // player built elsewhere can be dropped beside it.
    report.warnings.push_back(std::format("{}: no player binary for {}; the bundle was written without one",
                                          player.string(), assets::toString(options.platform)));
  }

  report.supportFileCount = copyShaders(source / "shaders", options.outputDirectory / "shaders", report.warnings);
  report.supportFileCount += copyRuntimeLibraries(options.platform, source, options.outputDirectory, report.warnings);

  SONNET_LOG_INFO("exported \"{}\" for {} into {}: {} assets, {} support files, {} warnings", project.name,
                  assets::toString(options.platform), options.outputDirectory.string(), report.cook.assetCount,
                  report.supportFileCount, report.warnings.size());
  return report;
}

} // namespace sonnet::editor
