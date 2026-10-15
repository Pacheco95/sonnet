#include <sonnet/assets/Importers.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <meshoptimizer.h>
#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <variant>

namespace sonnet::assets {

namespace {

// The extensions read, the same for every parse so a file one parse accepts no other refuses.
// KHR_mesh_quantization only allows 8- and 16-bit vertex attributes, which iterateAccessor converts
// to float (normalized or not); the dequantizing scale and offset are the node's transform.
constexpr fastgltf::Extensions ReadExtensions =
    fastgltf::Extensions::KHR_materials_emissive_strength | fastgltf::Extensions::KHR_materials_transmission |
    fastgltf::Extensions::KHR_texture_transform | fastgltf::Extensions::KHR_mesh_quantization |
    fastgltf::Extensions::EXT_meshopt_compression | fastgltf::Extensions::KHR_texture_basisu;

bool readsExtension(std::string_view name) {
  static const auto Read = fastgltf::stringifyExtensionBits(ReadExtensions);
  return std::ranges::find(Read, name) != Read.end();
}

// The extensions in `names` the importer does not read, comma-separated, or empty.
template <typename Names> std::string unreadExtensions(const Names &names) {
  std::string unread;
  for (const auto &name : names) {
    if (!readsExtension(name)) {
      unread += std::format("{}{}", unread.empty() ? "" : ", ", std::string_view{name});
    }
  }
  return unread;
}

// The file's extensionsRequired that the importer does not read, read again from its JSON, since
// fastgltf refuses such a file without naming them. Empty when the file cannot be read.
std::string unreadRequiredExtensions(const std::filesystem::path &path) {
  const auto bytes = core::readFile(path);
  if (!bytes) {
    return {};
  }
  std::string_view json{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
  // A .glb holds its JSON in the first chunk: a 12-byte header, then the chunk's length and type.
  if (json.starts_with("glTF") && json.size() >= 20) {
    std::uint32_t length = 0;
    std::memcpy(&length, bytes->data() + 12, sizeof(length));
    json = json.substr(20, length);
  }
  simdjson::dom::parser parser;
  simdjson::dom::array required;
  if (parser.parse(json.data(), json.size())["extensionsRequired"].get_array().get(required) != simdjson::SUCCESS) {
    return {};
  }
  std::vector<std::string_view> names;
  for (const simdjson::dom::element element : required) {
    std::string_view name;
    if (element.get_string().get(name) == simdjson::SUCCESS) {
      names.push_back(name);
    }
  }
  return unreadExtensions(names);
}

core::Error gltfError(const std::filesystem::path &path, std::string_view what, fastgltf::Error error) {
  return core::Error{std::format("{}: {}: {}", path.string(), what, fastgltf::getErrorMessage(error)),
                     core::ErrorCategory::Io};
}

// A parse that failed. The spec has a file refused when it requires an extension the importer does
// not read; the error names them.
core::Error parseError(const std::filesystem::path &path, fastgltf::Error error) {
  if (error == fastgltf::Error::MissingExtensions || error == fastgltf::Error::UnknownRequiredExtension) {
    if (const std::string unread = unreadRequiredExtensions(path); !unread.empty()) {
      return core::Error{std::format("{}: requires {}, which the importer does not read", path.string(), unread),
                         core::ErrorCategory::Io};
    }
  }
  return gltfError(path, "parsing", error);
}

// EXT_meshopt_compression: by buffer view index, the bytes of each compressed view, decoded; empty
// for a view that is not compressed. The views' own buffer is the extension's fallback, which may
// hold no data, so every accessor read goes through DecodedViewAdapter instead.
using DecodedViews = std::vector<std::vector<std::byte>>;

// A loaded buffer's bytes, or none: an external buffer the parse did not load, or a fallback.
fastgltf::span<const std::byte> loadedBytes(const fastgltf::Buffer &buffer) {
  return std::visit(
      [](const auto &source) -> fastgltf::span<const std::byte> {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, fastgltf::sources::Array> ||
                      std::is_same_v<Source, fastgltf::sources::Vector>) {
          return fastgltf::span<const std::byte>{source.bytes.data(), source.bytes.size()};
        } else if constexpr (std::is_same_v<Source, fastgltf::sources::ByteView>) {
          return source.bytes;
        } else {
          return {};
        }
      },
      buffer.data);
}

core::Result<DecodedViews> decodeCompressedViews(const fastgltf::Asset &asset, const std::filesystem::path &path) {
  DecodedViews decoded(asset.bufferViews.size());
  for (std::size_t v = 0; v < asset.bufferViews.size(); ++v) {
    const fastgltf::BufferView &view = asset.bufferViews[v];
    if (view.meshoptCompression == nullptr) {
      continue;
    }
    const fastgltf::CompressedBufferView &compressed = *view.meshoptCompression;
    const auto fail = [&](std::string_view what) {
      return std::unexpected(
          core::Error{std::format("{}: buffer view {}: EXT_meshopt_compression {}", path.string(), v, what),
                      core::ErrorCategory::Io});
    };
    const fastgltf::span<const std::byte> source = compressed.bufferIndex < asset.buffers.size()
                                                       ? loadedBytes(asset.buffers[compressed.bufferIndex])
                                                       : fastgltf::span<const std::byte>{};
    if (compressed.byteOffset > source.size() || compressed.byteLength > source.size() - compressed.byteOffset) {
      return fail("data is not loaded");
    }
    // The extension has the view hold exactly the decoded elements, which also bounds the allocation.
    if (compressed.byteStride == 0 || compressed.count != view.byteLength / compressed.byteStride ||
        view.byteLength % compressed.byteStride != 0) {
      return fail("count and byteStride do not match the view's byteLength");
    }
    // The strides each filter allows; meshoptimizer asserts on any other (fastgltf checks the modes').
    using Filter = fastgltf::MeshoptCompressionFilter;
    const std::size_t stride = compressed.byteStride;
    if (compressed.filter != Filter::None && (compressed.mode != fastgltf::MeshoptCompressionMode::Attributes ||
                                              (compressed.filter == Filter::Octahedral && stride != 4 && stride != 8) ||
                                              (compressed.filter == Filter::Quaternion && stride != 8))) {
      return fail("filter does not fit the view's mode or byteStride");
    }
    std::vector<std::byte> &out = decoded[v];
    out.resize(view.byteLength);
    const auto *bytes = reinterpret_cast<const unsigned char *>(source.data() + compressed.byteOffset);
    int result = -1;
    switch (compressed.mode) {
    case fastgltf::MeshoptCompressionMode::Attributes:
      result =
          meshopt_decodeVertexBuffer(out.data(), compressed.count, compressed.byteStride, bytes, compressed.byteLength);
      break;
    case fastgltf::MeshoptCompressionMode::Triangles:
      result =
          meshopt_decodeIndexBuffer(out.data(), compressed.count, compressed.byteStride, bytes, compressed.byteLength);
      break;
    case fastgltf::MeshoptCompressionMode::Indices:
      result = meshopt_decodeIndexSequence(out.data(), compressed.count, compressed.byteStride, bytes,
                                           compressed.byteLength);
      break;
    }
    if (result != 0) {
      return fail("data does not decode");
    }
    switch (compressed.filter) {
    case fastgltf::MeshoptCompressionFilter::None:
      break;
    case fastgltf::MeshoptCompressionFilter::Octahedral:
      meshopt_decodeFilterOct(out.data(), compressed.count, compressed.byteStride);
      break;
    case fastgltf::MeshoptCompressionFilter::Quaternion:
      meshopt_decodeFilterQuat(out.data(), compressed.count, compressed.byteStride);
      break;
    case fastgltf::MeshoptCompressionFilter::Exponential:
      meshopt_decodeFilterExp(out.data(), compressed.count, compressed.byteStride);
      break;
    }
  }
  return decoded;
}

// fastgltf's buffer data adapter for iterateAccessor: a compressed view's decoded bytes, or the
// view's own.
struct DecodedViewAdapter {
  const DecodedViews *decoded = nullptr;

  fastgltf::span<const std::byte> operator()(const fastgltf::Asset &asset, std::size_t view) const {
    if (const std::vector<std::byte> &bytes = (*decoded)[view]; !bytes.empty()) {
      return fastgltf::span<const std::byte>{bytes.data(), bytes.size()};
    }
    return fastgltf::DefaultBufferDataAdapter{}(asset, view);
  }
};

// The UV set a slot reads: its texture info's texCoord, or KHR_texture_transform's override of it.
// `info` is one of fastgltf's optional texture-info types.
template <typename OptionalInfo> std::size_t texCoordOf(const OptionalInfo &info) {
  if (!info.has_value()) {
    return 0;
  }
  if (info->transform != nullptr && info->transform->texCoordIndex.has_value()) {
    return *info->transform->texCoordIndex;
  }
  return info->texCoordIndex;
}

// A slot's UV set and KHR_texture_transform, or set 0 untransformed. Meshes keep two sets, so a
// slot that reads a later one reads set 0 (the material's import warns).
template <typename OptionalInfo> renderer::TextureTransform transformOf(const OptionalInfo &info) {
  const auto texCoord = static_cast<std::uint32_t>(texCoordOf(info) == 1 ? 1 : 0);
  if (!info.has_value() || info->transform == nullptr) {
    return {.texCoord = texCoord};
  }
  const fastgltf::TextureTransform &transform = *info->transform;
  return {.offset = {transform.uvOffset[0], transform.uvOffset[1]},
          .rotation = transform.rotation,
          .scale = {transform.uvScale[0], transform.uvScale[1]},
          .texCoord = texCoord};
}

// The highest UV set any of a material's texture slots reads.
std::size_t highestTexCoord(const fastgltf::Material &material) {
  return std::max({texCoordOf(material.pbrData.baseColorTexture), texCoordOf(material.pbrData.metallicRoughnessTexture),
                   texCoordOf(material.normalTexture), texCoordOf(material.occlusionTexture),
                   texCoordOf(material.emissiveTexture)});
}

std::string nameOr(std::string_view name, const char *kind, std::size_t index) {
  return name.empty() ? std::format("{} {}", kind, index) : std::string{name};
}

renderer::TextureWrap toWrap(fastgltf::Wrap wrap) noexcept {
  switch (wrap) {
  case fastgltf::Wrap::ClampToEdge:
    return renderer::TextureWrap::ClampToEdge;
  case fastgltf::Wrap::MirroredRepeat:
    return renderer::TextureWrap::MirroredRepeat;
  case fastgltf::Wrap::Repeat:
    return renderer::TextureWrap::Repeat;
  }
  return renderer::TextureWrap::Repeat;
}

// The image a texture refers to, or -1. A KHR_texture_basisu image comes first: the extension
// makes the texture's own source the fallback for clients that do not read KTX2. Other extension
// images (WebP, DDS) are not read.
std::int32_t imageOfTexture(const fastgltf::Asset &asset, std::size_t textureIndex) {
  if (textureIndex >= asset.textures.size()) {
    return -1;
  }
  const fastgltf::Texture &texture = asset.textures[textureIndex];
  if (texture.basisuImageIndex.has_value()) {
    return static_cast<std::int32_t>(*texture.basisuImageIndex);
  }
  return texture.imageIndex.has_value() ? static_cast<std::int32_t>(*texture.imageIndex) : -1;
}

// Whether the bytes are a KTX2 file, by its 12-byte identifier (the KTX 2.0 specification).
bool isKtx2(std::span<const std::byte> bytes) {
  constexpr std::array<unsigned char, 12> Identifier{0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
  return bytes.size() >= Identifier.size() &&
         std::ranges::equal(bytes.first(Identifier.size()), Identifier,
                            [](std::byte a, unsigned char b) { return a == std::byte{b}; });
}

// `info` is one of fastgltf's optional texture-info types, which share has_value and ->.
template <typename OptionalInfo> std::int32_t imageOf(const fastgltf::Asset &asset, const OptionalInfo &info) {
  return info.has_value() ? imageOfTexture(asset, info->textureIndex) : -1;
}

// The encoded bytes of an image: embedded in a buffer, loaded from its file by the parser, or
// read from the file next to the glTF.
core::Result<std::vector<std::byte>> imageBytes(const fastgltf::Asset &asset, const fastgltf::Image &image,
                                                const std::filesystem::path &directory) {
  return std::visit(
      [&](const auto &source) -> core::Result<std::vector<std::byte>> {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, fastgltf::sources::Array>) {
          return std::vector<std::byte>(source.bytes.begin(), source.bytes.end());
        } else if constexpr (std::is_same_v<Source, fastgltf::sources::Vector>) {
          return source.bytes;
        } else if constexpr (std::is_same_v<Source, fastgltf::sources::ByteView>) {
          return std::vector<std::byte>(source.bytes.begin(), source.bytes.end());
        } else if constexpr (std::is_same_v<Source, fastgltf::sources::BufferView>) {
          const fastgltf::BufferView &view = asset.bufferViews[source.bufferViewIndex];
          const fastgltf::Buffer &buffer = asset.buffers[view.bufferIndex];
          const auto *bytes = std::get_if<fastgltf::sources::Array>(&buffer.data);
          if (bytes == nullptr || view.byteOffset + view.byteLength > bytes->bytes.size()) {
            return std::unexpected(core::Error{"image buffer view is not loaded", core::ErrorCategory::Io});
          }
          const auto *begin = bytes->bytes.data() + view.byteOffset;
          return std::vector<std::byte>(begin, begin + view.byteLength);
        } else if constexpr (std::is_same_v<Source, fastgltf::sources::URI>) {
          if (!source.uri.isLocalPath()) {
            return std::unexpected(core::Error{"image URI is not a local file", core::ErrorCategory::Io});
          }
          return core::readFile(directory / source.uri.fspath());
        } else {
          return std::unexpected(core::Error{"image has no data", core::ErrorCategory::Io});
        }
      },
      image.data);
}

// A primitive without normals gets flat ones, which the glTF specification asks for: its
// triangles no longer share corners, so the vertices and indices from the primitive's start are
// rewritten as one corner per index.
void flattenNormals(renderer::MeshData &data, std::uint32_t firstVertex, std::uint32_t firstIndex) {
  const std::span<const std::uint32_t> indices{data.indices.begin() + firstIndex, data.indices.end()};
  const bool skinned = !data.skin.empty();
  if (skinned) {
    data.skin.resize(data.vertices.size()); // this primitive may have had no weights of its own
  }
  std::vector<renderer::Vertex> corners;
  std::vector<renderer::SkinWeights> cornerSkin;
  corners.reserve(indices.size());
  for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
    const std::array<std::uint32_t, 3> triangle{indices[i], indices[i + 1], indices[i + 2]};
    const glm::vec3 edge1 = data.vertices[triangle[1]].position - data.vertices[triangle[0]].position;
    const glm::vec3 edge2 = data.vertices[triangle[2]].position - data.vertices[triangle[0]].position;
    const glm::vec3 cross = glm::cross(edge1, edge2);
    const float length = glm::length(cross);
    const glm::vec3 normal = length > 0.0f ? cross / length : glm::vec3{0.0f, 1.0f, 0.0f};
    for (const std::uint32_t index : triangle) {
      corners.push_back(data.vertices[index]);
      corners.back().normal = normal;
      if (skinned) {
        cornerSkin.push_back(data.skin[index]);
      }
    }
  }
  data.vertices.resize(firstVertex);
  data.vertices.insert(data.vertices.end(), corners.begin(), corners.end());
  if (skinned) {
    data.skin.resize(firstVertex);
    data.skin.insert(data.skin.end(), cornerSkin.begin(), cornerSkin.end());
  }
  for (std::size_t i = 0; i < indices.size(); ++i) {
    data.indices[firstIndex + i] = firstVertex + static_cast<std::uint32_t>(i);
  }
}

// The node hierarchy of the default scene, or of every root node without one, walked parents
// before children. The full import and the structure-only read both go through here, so they
// agree on every node's index, which is what a model prefab derives its entities' identities from.
struct NodeWalk {
  // By glTF node index, the path from the model's root: what joints and channels name their nodes
  // by (ADR-0010).
  std::vector<std::string> paths;
  // By glTF node index, whether the node is in the scene.
  std::vector<bool> visited;
  // Names that siblings share, which makes their paths ambiguous.
  std::vector<std::string> duplicateNames;
};

NodeWalk walkNodes(const fastgltf::Asset &asset, Model &model, std::vector<std::int32_t> &meshIndices,
                   std::vector<std::int32_t> &skinIndices) {
  std::vector<std::size_t> roots;
  if (asset.defaultScene.has_value() && *asset.defaultScene < asset.scenes.size()) {
    roots.assign(asset.scenes[*asset.defaultScene].nodeIndices.begin(),
                 asset.scenes[*asset.defaultScene].nodeIndices.end());
  } else if (!asset.scenes.empty()) {
    roots.assign(asset.scenes[0].nodeIndices.begin(), asset.scenes[0].nodeIndices.end());
  } else {
    std::vector<bool> isChild(asset.nodes.size(), false);
    for (const fastgltf::Node &node : asset.nodes) {
      for (const std::size_t child : node.children) {
        isChild[child] = true;
      }
    }
    for (std::size_t n = 0; n < asset.nodes.size(); ++n) {
      if (!isChild[n]) {
        roots.push_back(n);
      }
    }
  }
  NodeWalk walk{.paths = std::vector<std::string>(asset.nodes.size()),
                .visited = std::vector<bool>(asset.nodes.size(), false),
                .duplicateNames = {}};
  const auto visit = [&](auto &self, std::size_t nodeIndex, std::int32_t parent, const std::string &parentPath,
                         std::vector<std::string> &siblings) -> void {
    if (nodeIndex >= asset.nodes.size() || walk.visited[nodeIndex]) {
      return;
    }
    walk.visited[nodeIndex] = true;
    const fastgltf::Node &node = asset.nodes[nodeIndex];
    ModelNode out;
    out.name = nameOr(node.name, "node", nodeIndex);
    out.parent = parent;
    if (std::ranges::find(siblings, out.name) != siblings.end()) {
      walk.duplicateNames.push_back(out.name);
    }
    siblings.push_back(out.name);
    walk.paths[nodeIndex] = parentPath.empty() ? out.name : parentPath + "/" + out.name;
    if (const auto *trs = std::get_if<fastgltf::TRS>(&node.transform)) {
      out.position = {trs->translation[0], trs->translation[1], trs->translation[2]};
      out.rotation = glm::quat{trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
      out.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    }
    if (node.meshIndex.has_value() && *node.meshIndex < asset.meshes.size()) {
      // The node's own weights override the mesh's; a mesh with targets and no weights starts at zero.
      const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
      const std::size_t targets = mesh.primitives.empty() ? 0 : mesh.primitives.front().targets.size();
      if (targets > 0) {
        const auto &given = !node.weights.empty() ? node.weights : mesh.weights;
        out.morphWeights.assign(std::min<std::size_t>(targets, renderer::MaxMorphTargets), 0.0f);
        for (std::size_t t = 0; t < out.morphWeights.size() && t < given.size(); ++t) {
          out.morphWeights[t] = given[t];
        }
      }
    }
    const auto index = static_cast<std::int32_t>(model.nodes.size());
    model.nodes.push_back(std::move(out));
    meshIndices.push_back(node.meshIndex.has_value() ? static_cast<std::int32_t>(*node.meshIndex) : -1);
    skinIndices.push_back(
        node.meshIndex.has_value() && node.skinIndex.has_value() ? static_cast<std::int32_t>(*node.skinIndex) : -1);
    std::vector<std::string> children;
    for (const std::size_t child : node.children) {
      self(self, child, index, walk.paths[nodeIndex], children);
    }
  };
  std::vector<std::string> rootNames;
  for (const std::size_t root : roots) {
    visit(visit, root, -1, std::string{}, rootNames);
  }
  return walk;
}

} // namespace

namespace {

// The "events" array of an animation's extras: [{"time": 0.4, "name": "footstep", "argument": "left"}].
void readAnimationEvents(simdjson::dom::object *extras, std::size_t index, fastgltf::Category category, void *user) {
  if (category != fastgltf::Category::Animations || extras == nullptr) {
    return;
  }
  simdjson::dom::array events;
  if ((*extras)["events"].get_array().get(events) != simdjson::SUCCESS) {
    return;
  }
  auto &out = *static_cast<std::map<std::size_t, std::vector<AnimationEvent>> *>(user);
  for (simdjson::dom::element element : events) {
    simdjson::dom::object object;
    double time = 0.0;
    std::string_view name;
    if (element.get_object().get(object) != simdjson::SUCCESS ||
        object["time"].get_double().get(time) != simdjson::SUCCESS ||
        object["name"].get_string().get(name) != simdjson::SUCCESS) {
      continue;
    }
    std::string_view argument;
    if (object["argument"].get_string().get(argument) != simdjson::SUCCESS) {
      argument = {};
    }
    out[index].push_back(
        {.time = static_cast<float>(time), .name = std::string{name}, .argument = std::string{argument}});
  }
}

} // namespace

core::Result<GltfImport> importGltf(const std::filesystem::path &path) {
  SONNET_ZONE();
  auto buffer = fastgltf::GltfDataBuffer::FromPath(path);
  if (buffer.error() != fastgltf::Error::None) {
    return std::unexpected(gltfError(path, "reading", buffer.error()));
  }
  fastgltf::Parser parser{ReadExtensions};
  std::map<std::size_t, std::vector<AnimationEvent>> animationEvents;
  parser.setExtrasParseCallback(readAnimationEvents);
  parser.setUserPointer(&animationEvents);
  const auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages |
                       fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::GenerateMeshIndices;
  auto loaded = parser.loadGltf(buffer.get(), path.parent_path(), options);
  if (loaded.error() != fastgltf::Error::None) {
    return std::unexpected(parseError(path, loaded.error()));
  }
  const fastgltf::Asset &asset = loaded.get();
  GltfImport import;
  if (const std::string unread = unreadExtensions(asset.extensionsUsed); !unread.empty()) {
    SONNET_LOG_WARN("{}: uses {}, which the importer does not read; the model may not look as authored", path.string(),
                    unread);
  }
  // Compressed views are decoded before any accessor is read; every read below passes `views`.
  const auto decoded = decodeCompressedViews(asset, path);
  if (!decoded) {
    return std::unexpected(decoded.error());
  }
  const DecodedViewAdapter views{.decoded = &*decoded};

  // Meshes: one submesh per triangle primitive, with flat normals and tangents generated where a
  // primitive has none.
  for (std::size_t m = 0; m < asset.meshes.size(); ++m) {
    const fastgltf::Mesh &mesh = asset.meshes[m];
    GltfMesh out{.name = nameOr(mesh.name, "mesh", m), .data = {}, .materials = {}};
    bool needTangents = false;
    bool warnedTargets = false;
    std::vector<std::vector<renderer::MorphDelta>> morphTargets; // by target, then vertex
    for (const fastgltf::Primitive &primitive : mesh.primitives) {
      bool needFlatNormals = false;
      if (primitive.type != fastgltf::PrimitiveType::Triangles) {
        SONNET_LOG_WARN("{}: mesh \"{}\" has a primitive that is not a triangle list, skipped", path.string(),
                        out.name);
        continue;
      }
      const auto position = primitive.findAttribute("POSITION");
      if (position == primitive.attributes.end() || !primitive.indicesAccessor.has_value()) {
        SONNET_LOG_WARN("{}: mesh \"{}\" has a primitive without positions or indices, skipped", path.string(),
                        out.name);
        continue;
      }
      const auto base = static_cast<std::uint32_t>(out.data.vertices.size());
      const fastgltf::Accessor &positions = asset.accessors[position->accessorIndex];
      out.data.vertices.resize(base + positions.count);
      fastgltf::iterateAccessorWithIndex<glm::vec3>(
          asset, positions,
          [&](glm::vec3 value, std::size_t i) {
            out.data.vertices[base + i].position = value;
            out.data.vertices[base + i].normal = {0.0f, 1.0f, 0.0f};
          },
          views);
      if (const auto normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, asset.accessors[normal->accessorIndex],
            [&](glm::vec3 value, std::size_t i) { out.data.vertices[base + i].normal = value; }, views);
      } else {
        needFlatNormals = true;
      }
      if (const auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, asset.accessors[uv->accessorIndex],
            [&](glm::vec2 value, std::size_t i) { out.data.vertices[base + i].uv = value; }, views);
      }
      // The second set, which a material's slot may read instead (texCoord 1). A primitive without
      // one keeps zeros, and a slot that asks for it there gets the same texel everywhere.
      const fastgltf::Material *material =
          primitive.materialIndex.has_value() && *primitive.materialIndex < asset.materials.size()
              ? &asset.materials[*primitive.materialIndex]
              : nullptr;
      if (const auto uv1 = primitive.findAttribute("TEXCOORD_1"); uv1 != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, asset.accessors[uv1->accessorIndex],
            [&](glm::vec2 value, std::size_t i) { out.data.vertices[base + i].uv1 = value; }, views);
      } else if (material != nullptr && highestTexCoord(*material) >= 1) {
        SONNET_LOG_WARN("{}: mesh \"{}\" has a primitive without TEXCOORD_1 whose material reads it", path.string(),
                        out.name);
      }
      if (const auto tangent = primitive.findAttribute("TANGENT"); tangent != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[tangent->accessorIndex],
            [&](glm::vec4 value, std::size_t i) { out.data.vertices[base + i].tangent = value; }, views);
      } else {
        needTangents = true;
        if (material != nullptr && texCoordOf(material->normalTexture) == 1) {
          SONNET_LOG_WARN("{}: mesh \"{}\" has no tangents and a normal map on TEXCOORD_1; the generated tangents "
                          "follow TEXCOORD_0, so its bumps may point the wrong way",
                          path.string(), out.name);
        }
      }
      // The first set of joints and weights; a primitive without them keeps zero weights, which
      // the skinning pass leaves in the bind pose.
      const auto joints = primitive.findAttribute("JOINTS_0");
      const auto weights = primitive.findAttribute("WEIGHTS_0");
      if (joints != primitive.attributes.end() && weights != primitive.attributes.end()) {
        out.data.skin.resize(out.data.vertices.size());
        fastgltf::iterateAccessorWithIndex<glm::uvec4>(
            asset, asset.accessors[joints->accessorIndex],
            [&](glm::uvec4 value, std::size_t i) { out.data.skin[base + i].joints = value; }, views);
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[weights->accessorIndex],
            [&](glm::vec4 value, std::size_t i) { out.data.skin[base + i].weights = value; }, views);
      }
      // Morph targets: position, normal and tangent deltas, per target kept.
      const std::size_t targets = std::min<std::size_t>(primitive.targets.size(), renderer::MaxMorphTargets);
      if (primitive.targets.size() > renderer::MaxMorphTargets && !warnedTargets) {
        SONNET_LOG_WARN("{}: mesh \"{}\" has {} morph targets, the first {} are kept", path.string(), out.name,
                        primitive.targets.size(), renderer::MaxMorphTargets);
        warnedTargets = true;
      }
      if (targets > morphTargets.size()) {
        morphTargets.resize(targets);
      }
      for (std::size_t t = 0; t < morphTargets.size(); ++t) {
        morphTargets[t].resize(out.data.vertices.size()); // zeros where this primitive has no such target
        if (t >= targets) {
          continue;
        }
        const auto read = [&](const char *name, auto assign) {
          for (const auto &attribute : primitive.targets[t]) {
            if (attribute.name == name) {
              fastgltf::iterateAccessorWithIndex<glm::vec3>(
                  asset, asset.accessors[attribute.accessorIndex],
                  [&](glm::vec3 value, std::size_t i) { assign(morphTargets[t][base + i], value); }, views);
            }
          }
        };
        read("POSITION", [](renderer::MorphDelta &delta, glm::vec3 value) { delta.position = value; });
        read("NORMAL", [](renderer::MorphDelta &delta, glm::vec3 value) { delta.normal = value; });
        read("TANGENT", [](renderer::MorphDelta &delta, glm::vec3 value) { delta.tangent = value; });
      }
      const fastgltf::Accessor &indices = asset.accessors[*primitive.indicesAccessor];
      const auto firstIndex = static_cast<std::uint32_t>(out.data.indices.size());
      fastgltf::iterateAccessor<std::uint32_t>(
          asset, indices, [&](std::uint32_t index) { out.data.indices.push_back(base + index); }, views);
      if (needFlatNormals) {
        flattenNormals(out.data, base, firstIndex);
      }
      out.data.submeshes.push_back(renderer::Submesh{.firstIndex = firstIndex,
                                                     .indexCount = static_cast<std::uint32_t>(indices.count),
                                                     .materialSlot = static_cast<std::uint32_t>(out.materials.size())});
      out.materials.push_back(primitive.materialIndex.has_value() ? static_cast<std::int32_t>(*primitive.materialIndex)
                                                                  : -1);
    }
    if (!out.data.skin.empty()) {
      out.data.skin.resize(out.data.vertices.size()); // primitives after the last skinned one
    }
    if (!morphTargets.empty()) {
      out.data.morphTargetCount = static_cast<std::uint32_t>(morphTargets.size());
      for (std::vector<renderer::MorphDelta> &target : morphTargets) {
        target.resize(out.data.vertices.size()); // primitives after the last one with targets
        out.data.morphDeltas.insert(out.data.morphDeltas.end(), target.begin(), target.end());
      }
    }
    if (out.data.vertices.empty() || out.data.indices.empty()) {
      SONNET_LOG_WARN("{}: mesh \"{}\" has no usable primitives", path.string(), out.name);
    } else if (needTangents) {
      renderer::generateTangents(out.data);
    }
    if (!out.data.vertices.empty() && !out.data.indices.empty()) {
      renderer::buildShadowRange(out.data); // on the import's worker, with the rest (ADR-0025)
    }
    import.meshes.push_back(std::move(out));
  }

  // Images, each marked sRGB when any material reads it as colour, and KTX2 by its contents
  // rather than its MIME type, which an image in a file may leave out.
  for (std::size_t i = 0; i < asset.images.size(); ++i) {
    auto bytes = imageBytes(asset, asset.images[i], path.parent_path());
    if (!bytes) {
      SONNET_LOG_WARN("{}: image {}: {}", path.string(), i, bytes.error().message);
      bytes = std::vector<std::byte>{};
    }
    const bool ktx2 = isKtx2(*bytes);
    import.images.push_back(GltfImage{
        .name = nameOr(asset.images[i].name, "image", i), .bytes = std::move(*bytes), .srgb = false, .ktx2 = ktx2});
  }

  for (std::size_t m = 0; m < asset.materials.size(); ++m) {
    const fastgltf::Material &material = asset.materials[m];
    GltfMaterial out{.name = nameOr(material.name, "material", m), .source = {}};
    if (highestTexCoord(material) > 1) {
      SONNET_LOG_WARN("{}: material \"{}\" reads TEXCOORD_{}; meshes keep two UV sets, so it reads TEXCOORD_0",
                      path.string(), out.name, highestTexCoord(material));
    }
    const fastgltf::PBRData &pbr = material.pbrData;
    out.source.baseColor = {pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                            pbr.baseColorFactor[3]};
    out.source.metallic = pbr.metallicFactor;
    out.source.roughness = pbr.roughnessFactor;
    // KHR_materials_emissive_strength lifts the clamped factor above 1; emissive is unclamped.
    out.source.emissive =
        glm::vec3{material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]} *
        material.emissiveStrength;
    out.source.alphaCutoff = material.alphaCutoff;
    out.source.doubleSided = material.doubleSided;
    out.source.alphaMode = material.alphaMode == fastgltf::AlphaMode::Blend  ? renderer::AlphaMode::Blend
                           : material.alphaMode == fastgltf::AlphaMode::Mask ? renderer::AlphaMode::Mask
                                                                             : renderer::AlphaMode::Opaque;
    // Transmission until refraction exists (#254): blended, with the light the surface lets
    // through taken off its alpha, so clear glass shows what is behind it. The texture is ignored.
    if (material.transmission != nullptr && material.transmission->transmissionFactor > 0.0f) {
      out.source.alphaMode = renderer::AlphaMode::Blend;
      out.source.baseColor.a *= 1.0f - material.transmission->transmissionFactor;
    }
    out.baseColorImage = imageOf(asset, pbr.baseColorTexture);
    out.metallicRoughnessImage = imageOf(asset, pbr.metallicRoughnessTexture);
    out.normalImage = imageOf(asset, material.normalTexture);
    if (material.normalTexture.has_value()) {
      out.source.normalScale = material.normalTexture->scale;
    }
    out.occlusionImage = imageOf(asset, material.occlusionTexture);
    if (material.occlusionTexture.has_value()) {
      out.source.occlusionStrength = material.occlusionTexture->strength;
    }
    out.emissiveImage = imageOf(asset, material.emissiveTexture);
    out.source.baseColorTransform = transformOf(pbr.baseColorTexture);
    out.source.metallicRoughnessTransform = transformOf(pbr.metallicRoughnessTexture);
    out.source.normalTransform = transformOf(material.normalTexture);
    out.source.occlusionTransform = transformOf(material.occlusionTexture);
    out.source.emissiveTransform = transformOf(material.emissiveTexture);
    if (pbr.baseColorTexture.has_value() && pbr.baseColorTexture->textureIndex < asset.textures.size()) {
      const fastgltf::Texture &texture = asset.textures[pbr.baseColorTexture->textureIndex];
      if (texture.samplerIndex.has_value() && *texture.samplerIndex < asset.samplers.size()) {
        out.source.wrap = toWrap(asset.samplers[*texture.samplerIndex].wrapS);
      }
    }
    for (const std::int32_t colour : {out.baseColorImage, out.emissiveImage}) {
      if (colour >= 0 && static_cast<std::size_t>(colour) < import.images.size()) {
        import.images[static_cast<std::size_t>(colour)].srgb = true;
      }
    }
    import.materials.push_back(std::move(out));
  }

  const NodeWalk walk = walkNodes(asset, import.model, import.meshIndices, import.skinIndices);
  for (const std::string &name : walk.duplicateNames) {
    SONNET_LOG_WARN("{}: two nodes named \"{}\" under one parent; animations and skins bind the first", path.string(),
                    name);
  }
  const std::vector<std::string> &paths = walk.paths;
  const std::vector<bool> &visited = walk.visited;

  // Skins: the joints by path, with their inverse bind matrices (identity when the file has none).
  for (std::size_t s = 0; s < asset.skins.size(); ++s) {
    const fastgltf::Skin &skin = asset.skins[s];
    GltfSkin out{.name = nameOr(skin.name, "skin", s), .skin = {}};
    for (const std::size_t joint : skin.joints) {
      if (joint >= asset.nodes.size() || !visited[joint]) {
        SONNET_LOG_WARN("{}: skin \"{}\" has a joint outside the scene", path.string(), out.name);
        out.skin.joints.emplace_back();
        continue;
      }
      out.skin.joints.push_back(paths[joint]);
    }
    out.skin.inverseBindMatrices.assign(out.skin.joints.size(), glm::mat4{1.0f});
    if (skin.inverseBindMatrices.has_value()) {
      fastgltf::iterateAccessorWithIndex<glm::mat4>(
          asset, asset.accessors[*skin.inverseBindMatrices],
          [&](const glm::mat4 &value, std::size_t i) {
            if (i < out.skin.inverseBindMatrices.size()) {
              out.skin.inverseBindMatrices[i] = value;
            }
          },
          views);
    }
    import.skins.push_back(std::move(out));
  }

  // Animations: translation, rotation, scale and morph target weight channels, and the events
  // the file's extras name.
  for (std::size_t a = 0; a < asset.animations.size(); ++a) {
    const fastgltf::Animation &animation = asset.animations[a];
    GltfAnimation out{.name = nameOr(animation.name, "animation", a), .clip = {}};
    for (const fastgltf::AnimationChannel &channel : animation.channels) {
      if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= asset.nodes.size() || !visited[*channel.nodeIndex] ||
          channel.samplerIndex >= animation.samplers.size()) {
        continue;
      }
      const fastgltf::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
      AnimationChannel result;
      result.target = paths[*channel.nodeIndex];
      result.path = channel.path == fastgltf::AnimationPath::Translation ? AnimationPath::Translation
                    : channel.path == fastgltf::AnimationPath::Rotation  ? AnimationPath::Rotation
                    : channel.path == fastgltf::AnimationPath::Weights   ? AnimationPath::Weights
                                                                         : AnimationPath::Scale;
      result.interpolation = sampler.interpolation == fastgltf::AnimationInterpolation::Step ? Interpolation::Step
                             : sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline
                                 ? Interpolation::CubicSpline
                                 : Interpolation::Linear;
      fastgltf::iterateAccessor<float>(
          asset, asset.accessors[sampler.inputAccessor], [&](float time) { result.times.push_back(time); }, views);
      const fastgltf::Accessor &output = asset.accessors[sampler.outputAccessor];
      const std::size_t stride = result.interpolation == Interpolation::CubicSpline ? 3 : 1;
      if (result.path == AnimationPath::Rotation) {
        fastgltf::iterateAccessor<glm::vec4>(
            asset, output,
            [&](glm::vec4 value) { result.values.insert(result.values.end(), {value.x, value.y, value.z, value.w}); },
            views);
      } else if (result.path == AnimationPath::Weights) {
        fastgltf::iterateAccessor<float>(asset, output, [&](float value) { result.values.push_back(value); }, views);
        // The output holds one weight per target for each of the keys' values.
        const std::size_t values = result.times.size() * stride;
        result.weightCount = values == 0 ? 0 : static_cast<std::uint32_t>(result.values.size() / values);
      } else {
        fastgltf::iterateAccessor<glm::vec3>(
            asset, output,
            [&](glm::vec3 value) { result.values.insert(result.values.end(), {value.x, value.y, value.z}); }, views);
      }
      if (result.times.empty() || result.values.size() != result.times.size() * stride * width(result) ||
          width(result) == 0 || !std::ranges::is_sorted(result.times)) {
        SONNET_LOG_WARN("{}: animation \"{}\" has a malformed channel for \"{}\", skipped", path.string(), out.name,
                        result.target);
        continue;
      }
      out.clip.duration = std::max(out.clip.duration, result.times.back());
      out.clip.channels.push_back(std::move(result));
    }
    if (const auto events = animationEvents.find(a); events != animationEvents.end()) {
      out.clip.events = events->second;
      std::ranges::stable_sort(out.clip.events, {}, &AnimationEvent::time);
      for (const AnimationEvent &event : out.clip.events) {
        out.clip.duration = std::max(out.clip.duration, event.time);
      }
    }
    import.animations.push_back(std::move(out));
  }
  SONNET_LOG_DEBUG("{}: {} meshes, {} materials, {} images, {} nodes, {} skins, {} animations", path.string(),
                   import.meshes.size(), import.materials.size(), import.images.size(), import.model.nodes.size(),
                   import.skins.size(), import.animations.size());
  return import;
}

core::Result<GltfStructure> importGltfStructure(const std::filesystem::path &path) {
  SONNET_ZONE();
  auto buffer = fastgltf::GltfDataBuffer::FromPath(path);
  if (buffer.error() != fastgltf::Error::None) {
    return std::unexpected(gltfError(path, "reading", buffer.error()));
  }
  // No external buffers and no images: everything a hierarchy needs is in the JSON.
  fastgltf::Parser parser{ReadExtensions};
  auto loaded = parser.loadGltf(buffer.get(), path.parent_path(), fastgltf::Options::DecomposeNodeMatrices);
  if (loaded.error() != fastgltf::Error::None) {
    return std::unexpected(parseError(path, loaded.error()));
  }
  const fastgltf::Asset &asset = loaded.get();
  GltfStructure structure;
  static_cast<void>(walkNodes(asset, structure.model, structure.meshIndices, structure.skinIndices));
  structure.animationCount = asset.animations.size();
  return structure;
}

core::Result<std::vector<std::filesystem::path>> gltfDependencies(const std::filesystem::path &path) {
  auto buffer = fastgltf::GltfDataBuffer::FromPath(path);
  if (buffer.error() != fastgltf::Error::None) {
    return std::unexpected(gltfError(path, "reading", buffer.error()));
  }
  fastgltf::Parser parser{ReadExtensions};
  auto loaded = parser.loadGltf(buffer.get(), path.parent_path(), fastgltf::Options::None);
  if (loaded.error() != fastgltf::Error::None) {
    return std::unexpected(parseError(path, loaded.error()));
  }
  std::vector<std::filesystem::path> files;
  std::optional<core::Error> outside;
  const auto add = [&](const fastgltf::DataSource &data) {
    const auto *uri = std::get_if<fastgltf::sources::URI>(&data);
    if (uri == nullptr || !uri->uri.isLocalPath()) {
      return;
    }
    const std::filesystem::path file = uri->uri.fspath().lexically_normal();
    if (file.empty() || file.is_absolute() || file.has_root_name() || *file.begin() == "..") {
      outside = core::Error{std::format("{}: {} is outside the file's folder", path.string(), file.generic_string()),
                            core::ErrorCategory::Io};
      return;
    }
    if (std::ranges::find(files, file) == files.end()) {
      files.push_back(file);
    }
  };
  for (const fastgltf::Buffer &gltfBuffer : loaded->buffers) {
    add(gltfBuffer.data);
  }
  for (const fastgltf::Image &image : loaded->images) {
    add(image.data);
  }
  if (outside) {
    return std::unexpected(*outside);
  }
  return files;
}

} // namespace sonnet::assets
