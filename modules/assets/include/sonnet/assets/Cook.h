#pragma once

#include <sonnet/assets/Bundle.h>
#include <sonnet/assets/Project.h>

#include <sonnet/core/Error.h>
#include <sonnet/renderer/Mesh.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sonnet::assets {

// Cooking turns imported data into what the player loads (docs/assets.md, "Cooking and
// export"). Meshes are the one kind that is reshaped rather than repackaged.

struct MeshCookStatistics {
  std::uint32_t verticesBefore{0};
  std::uint32_t verticesAfter{0}; // after welding
  float cacheMissesBefore{0.0f};  // average cache miss ratio, misses per triangle
  float cacheMissesAfter{0.0f};
};

// The post-transform vertex cache a cooked mesh is ordered for: the size tipsify fans around,
// and the size the ratios above are measured at.
constexpr std::uint32_t VertexCacheSize = 16;

// Welds vertices that are equal to the bit, reorders each submesh's triangles for the
// post-transform vertex cache (tipsify) and reorders the vertices by first use so the fetch
// runs forwards. The result draws the same geometry: the same triangles with the same vertex
// values, named in a different order.
[[nodiscard]] renderer::MeshData cookMesh(const renderer::MeshData &mesh, MeshCookStatistics *statistics = nullptr);

// Misses per triangle through a FIFO cache of `cacheSize` entries: 3.0 for an index buffer that
// never reuses anything, near 0.5 for a well-ordered closed mesh.
[[nodiscard]] float averageCacheMissRatio(std::span<const std::uint32_t> indices,
                                          std::uint32_t cacheSize = VertexCacheSize);

class AssetDatabase;

struct CookOptions {
  std::filesystem::path outputDirectory;
  CookPlatform platform{hostPlatform()};
};

struct CookReport {
  std::filesystem::path bundle;
  std::uint32_t assetCount{0}; // assets written; the built-in primitives are not among them
  std::uint32_t fileCount{0};  // scenes and prefabs
  std::uint64_t bytes{0};
  MeshCookStatistics meshes;         // summed over every mesh cooked
  std::vector<std::string> warnings; // one per asset that could not be cooked, which is skipped
};

// Cooks the project `database` has open into `<outputDirectory>/game.sbundle`, the name the
// player looks for beside itself (ADR-0011). Every asset is asked of the database, so the
// importers run in the code that already runs them and the texture cache is reused; an asset
// that fails is a warning in the report and is left out rather than failing the whole cook.
// Fails outright only when the database is open on another project or the bundle cannot be
// written. `sonnet_cook` and the editor's export dialog are the two callers.
[[nodiscard]] core::Result<CookReport> cook(AssetDatabase &database, const Project &project,
                                            const CookOptions &options);

} // namespace sonnet::assets
