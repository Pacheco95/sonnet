#pragma once

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Cook.h>
#include <sonnet/assets/Project.h>
#include <sonnet/core/Error.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sonnet::assets {
class AssetDatabase;
}

namespace sonnet::editor {

struct ExportOptions {
  std::filesystem::path outputDirectory;
  assets::CookPlatform platform{assets::hostPlatform()};
  std::optional<std::filesystem::path> scene{std::nullopt};
  // Where the target's player binary, its `shaders/` folder and the runtime libraries beside it
  // are. Defaults to the editor's own directory, which is right for the host platform only:
  // building a player for another one is CI's job, not the editor's (ADR-0011).
  std::filesystem::path playerDirectory;
};

struct ExportReport {
  assets::CookReport cook;
  std::filesystem::path player;      // the copied binary, empty when none was found
  std::uint32_t supportFileCount{0}; // shaders and the libraries next to the binary
  std::vector<std::string> warnings; // the cook's, plus anything that could not be copied
};

// The name the player binary has on a target, which is what export looks for and writes.
[[nodiscard]] std::string playerFileName(assets::CookPlatform platform);

// Cooks the project the database has open and assembles a directory that runs on its own
// (docs/player.md, "What an export is"): the bundle, the player binary, the compiled engine
// shaders and, on Windows, the libraries beside the binary. A missing player is a warning and
// leaves the bundle written, so an export can be finished by hand or by CI; only a failed cook
// fails the export.
[[nodiscard]] core::Result<ExportReport> exportProject(assets::AssetDatabase &database, const assets::Project &project,
                                                       const ExportOptions &options);

} // namespace sonnet::editor
