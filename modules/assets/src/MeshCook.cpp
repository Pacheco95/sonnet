#include <sonnet/assets/Cook.h>

#include <sonnet/core/Profile.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace sonnet::assets {

namespace {

using renderer::MeshData;
using renderer::SkinWeights;
using renderer::Submesh;
using renderer::Vertex;

constexpr std::uint32_t Unused = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t KeyWords = (sizeof(Vertex) + sizeof(SkinWeights)) / sizeof(std::uint32_t);

// A vertex and its skin weights compared to the bit. Two vertices that differ only by the sign
// of a zero stay apart, which costs a duplicate and never merges what the source kept separate.
struct VertexKey {
  std::array<std::uint32_t, KeyWords> words{};
  bool operator==(const VertexKey &) const = default;
};

struct VertexKeyHash {
  [[nodiscard]] std::size_t operator()(const VertexKey &key) const noexcept {
    std::size_t hash = 0xcbf29ce484222325ull;
    for (const std::uint32_t word : key.words) {
      hash = (hash ^ word) * 0x100000001b3ull;
    }
    return hash;
  }
};

[[nodiscard]] VertexKey keyOf(const Vertex &vertex, const SkinWeights &skin) {
  VertexKey key;
  std::memcpy(key.words.data(), &vertex, sizeof(vertex));
  std::memcpy(key.words.data() + sizeof(vertex) / sizeof(std::uint32_t), &skin, sizeof(skin));
  return key;
}

// The state tipsify walks a submesh with, kept across submeshes so the buffers are allocated
// once; every entry a submesh touched is cleared before the next one runs.
struct TipsifyScratch {
  std::vector<std::uint32_t> live;      // unemitted triangles per vertex
  std::vector<std::uint32_t> cacheTime; // the time stamp the vertex last entered the cache, 0 for never
  std::vector<std::uint32_t> offsets;   // adjacency start per vertex, one past the end
  std::vector<std::uint32_t> adjacency; // triangles, grouped by vertex
  std::vector<std::uint32_t> deadEnd;
  std::vector<std::uint32_t> candidates;
  std::vector<std::uint8_t> emitted;

  explicit TipsifyScratch(std::size_t vertexCount)
      : live(vertexCount, 0), cacheTime(vertexCount, 0), offsets(vertexCount + 1, 0) {
  }
};

// Tipsify (Sander, Nehab and Barczak, 2007): emit the triangles around one vertex, then fan on
// to the neighbour whose remaining triangles will still be in the cache when they are reached.
// Linear in the triangle count, and within a few per cent of the best-known orderings.
void tipsify(std::span<const std::uint32_t> indices, std::uint32_t cacheSize, TipsifyScratch &scratch,
             std::vector<std::uint32_t> &out) {
  const std::uint32_t triangleCount = static_cast<std::uint32_t>(indices.size() / 3);
  if (triangleCount == 0) {
    return;
  }
  for (const std::uint32_t index : indices) {
    ++scratch.live[index];
  }
  // Adjacency as one array grouped by vertex: the prefix sum of the triangle counts, then the
  // triangles written into each vertex's run.
  std::uint32_t total = 0;
  for (std::size_t vertex = 0; vertex < scratch.live.size(); ++vertex) {
    scratch.offsets[vertex] = total;
    total += scratch.live[vertex];
  }
  scratch.offsets[scratch.live.size()] = total;
  scratch.adjacency.assign(total, 0);
  std::vector<std::uint32_t> cursor(scratch.offsets.begin(), scratch.offsets.end() - 1);
  for (std::uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
    for (std::uint32_t corner = 0; corner < 3; ++corner) {
      scratch.adjacency[cursor[indices[(triangle * 3) + corner]]++] = triangle;
    }
  }

  scratch.emitted.assign(triangleCount, 0);
  scratch.deadEnd.clear();
  // The first stamp is past the cache, so the first vertex seen counts as a miss.
  std::uint32_t timeStamp = cacheSize + 1;
  std::uint32_t scan = 0;
  auto skipDeadEnd = [&]() -> std::uint32_t {
    while (!scratch.deadEnd.empty()) {
      const std::uint32_t vertex = scratch.deadEnd.back();
      scratch.deadEnd.pop_back();
      if (scratch.live[vertex] > 0) {
        return vertex;
      }
    }
    // A mesh in several connected pieces: pick up the next one where the scan left off.
    for (; scan < scratch.live.size(); ++scan) {
      if (scratch.live[scan] > 0) {
        return scan;
      }
    }
    return Unused;
  };

  out.reserve(out.size() + indices.size());
  std::uint32_t fanning = indices[0];
  while (fanning != Unused) {
    scratch.candidates.clear();
    for (std::uint32_t slot = scratch.offsets[fanning]; slot < scratch.offsets[fanning + 1]; ++slot) {
      const std::uint32_t triangle = scratch.adjacency[slot];
      if (scratch.emitted[triangle] != 0) {
        continue;
      }
      scratch.emitted[triangle] = 1;
      for (std::uint32_t corner = 0; corner < 3; ++corner) {
        const std::uint32_t vertex = indices[(triangle * 3) + corner];
        out.push_back(vertex);
        scratch.deadEnd.push_back(vertex);
        scratch.candidates.push_back(vertex);
        --scratch.live[vertex];
        if (timeStamp - scratch.cacheTime[vertex] > cacheSize) {
          scratch.cacheTime[vertex] = timeStamp;
          ++timeStamp;
        }
      }
    }
    // The next fan is the candidate that still has triangles and whose whole remaining fan fits
    // in what is left of the cache; the most recently cached one among those.
    fanning = Unused;
    std::uint32_t best = 0;
    bool found = false;
    for (const std::uint32_t vertex : scratch.candidates) {
      if (scratch.live[vertex] == 0) {
        continue;
      }
      std::uint32_t priority = 0;
      if (timeStamp - scratch.cacheTime[vertex] + (2 * scratch.live[vertex]) <= cacheSize) {
        priority = timeStamp - scratch.cacheTime[vertex];
      }
      if (!found || priority > best) {
        found = true;
        best = priority;
        fanning = vertex;
      }
    }
    if (fanning == Unused) {
      fanning = skipDeadEnd();
    }
  }

  // Leave the shared buffers as the next submesh expects them: live is already zero everywhere
  // it was touched, since every triangle was emitted.
  for (const std::uint32_t index : indices) {
    scratch.cacheTime[index] = 0;
  }
}

} // namespace

float averageCacheMissRatio(std::span<const std::uint32_t> indices, std::uint32_t cacheSize) {
  const std::uint32_t triangleCount = static_cast<std::uint32_t>(indices.size() / 3);
  if (triangleCount == 0 || cacheSize == 0) {
    return 0.0f;
  }
  std::vector<std::uint32_t> cache(cacheSize, Unused);
  std::uint32_t head = 0;
  std::uint32_t misses = 0;
  for (const std::uint32_t index : indices) {
    if (std::ranges::find(cache, index) == cache.end()) {
      ++misses;
      cache[head] = index;
      head = (head + 1) % cacheSize;
    }
  }
  return static_cast<float>(misses) / static_cast<float>(triangleCount);
}

renderer::MeshData cookMesh(const MeshData &mesh, MeshCookStatistics *statistics) {
  SONNET_ZONE();
  const bool skinned = mesh.skin.size() == mesh.vertices.size() && !mesh.skin.empty();
  if (statistics != nullptr) {
    statistics->verticesBefore = static_cast<std::uint32_t>(mesh.vertices.size());
    statistics->verticesAfter = statistics->verticesBefore;
    statistics->cacheMissesBefore = averageCacheMissRatio(mesh.indices);
    statistics->cacheMissesAfter = statistics->cacheMissesBefore;
  }
  // An index buffer that does not hold whole triangles is not something to reorder; a mesh
  // without indices has no triangles to weld towards either.
  if (mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
    return mesh;
  }

  // Welding: every vertex that is equal to the bit becomes one, and the indices follow.
  MeshData welded;
  welded.vertices.reserve(mesh.vertices.size());
  if (skinned) {
    welded.skin.reserve(mesh.skin.size());
  }
  std::vector<std::uint32_t> weld(mesh.vertices.size(), Unused);
  {
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> unique;
    unique.reserve(mesh.vertices.size());
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
      const VertexKey key = keyOf(mesh.vertices[vertex], skinned ? mesh.skin[vertex] : SkinWeights{});
      const auto [entry, inserted] = unique.try_emplace(key, static_cast<std::uint32_t>(welded.vertices.size()));
      if (inserted) {
        welded.vertices.push_back(mesh.vertices[vertex]);
        if (skinned) {
          welded.skin.push_back(mesh.skin[vertex]);
        }
      }
      weld[vertex] = entry->second;
    }
  }
  std::vector<std::uint32_t> indices(mesh.indices.size());
  for (std::size_t slot = 0; slot < mesh.indices.size(); ++slot) {
    const std::uint32_t index = mesh.indices[slot];
    if (index >= weld.size()) {
      return mesh; // an index past the vertices: leave the mesh alone rather than reshape it
    }
    indices[slot] = weld[index];
  }

  // Triangle order, one submesh at a time so a submesh stays one range of the index buffer.
  // A mesh without submeshes is one range and keeps none.
  std::vector<Submesh> ranges = mesh.submeshes;
  if (ranges.empty()) {
    ranges.push_back({.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(indices.size()), .materialSlot = 0});
  }
  TipsifyScratch scratch{welded.vertices.size()};
  welded.indices.reserve(indices.size());
  welded.submeshes.reserve(mesh.submeshes.size());
  for (const Submesh &range : ranges) {
    const std::uint32_t first = static_cast<std::uint32_t>(welded.indices.size());
    const bool whole =
        range.indexCount % 3 == 0 && static_cast<std::size_t>(range.firstIndex) + range.indexCount <= indices.size();
    const std::span<const std::uint32_t> span =
        whole ? std::span{indices}.subspan(range.firstIndex, range.indexCount) : std::span<const std::uint32_t>{};
    if (whole) {
      tipsify(span, VertexCacheSize, scratch, welded.indices);
    }
    if (!mesh.submeshes.empty()) {
      welded.submeshes.push_back({.firstIndex = first,
                                  .indexCount = static_cast<std::uint32_t>(welded.indices.size()) - first,
                                  .materialSlot = range.materialSlot});
    }
  }
  // A submesh the ranges could not describe would have dropped triangles; keep the source.
  if (welded.indices.size() != indices.size()) {
    return mesh;
  }

  // Vertex fetch: renumber by first use, so reading the vertex buffer runs forwards and
  // vertices no index names are dropped.
  std::vector<std::uint32_t> fetch(welded.vertices.size(), Unused);
  MeshData cooked;
  cooked.vertices.reserve(welded.vertices.size());
  if (skinned) {
    cooked.skin.reserve(welded.skin.size());
  }
  cooked.indices.resize(welded.indices.size());
  for (std::size_t slot = 0; slot < welded.indices.size(); ++slot) {
    const std::uint32_t index = welded.indices[slot];
    if (fetch[index] == Unused) {
      fetch[index] = static_cast<std::uint32_t>(cooked.vertices.size());
      cooked.vertices.push_back(welded.vertices[index]);
      if (skinned) {
        cooked.skin.push_back(welded.skin[index]);
      }
    }
    cooked.indices[slot] = fetch[index];
  }
  cooked.submeshes = std::move(welded.submeshes);

  if (statistics != nullptr) {
    statistics->verticesAfter = static_cast<std::uint32_t>(cooked.vertices.size());
    statistics->cacheMissesAfter = averageCacheMissRatio(cooked.indices);
  }
  return cooked;
}

} // namespace sonnet::assets
