#include <sonnet/assets/Importers.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <variant>

namespace sonnet::assets {

namespace {

core::Error gltfError(const std::filesystem::path &path, std::string_view what, fastgltf::Error error) {
  return core::Error{std::format("{}: {}: {}", path.string(), what, fastgltf::getErrorMessage(error)),
                     core::ErrorCategory::Io};
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

// The image a texture refers to, or -1: the KTX2 and other extension images are not read.
std::int32_t imageOfTexture(const fastgltf::Asset &asset, std::size_t textureIndex) {
  if (textureIndex >= asset.textures.size()) {
    return -1;
  }
  const fastgltf::Texture &texture = asset.textures[textureIndex];
  return texture.imageIndex.has_value() ? static_cast<std::int32_t>(*texture.imageIndex) : -1;
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

} // namespace

core::Result<GltfImport> importGltf(const std::filesystem::path &path) {
  SONNET_ZONE();
  auto buffer = fastgltf::GltfDataBuffer::FromPath(path);
  if (buffer.error() != fastgltf::Error::None) {
    return std::unexpected(gltfError(path, "reading", buffer.error()));
  }
  fastgltf::Parser parser;
  const auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages |
                       fastgltf::Options::DecomposeNodeMatrices | fastgltf::Options::GenerateMeshIndices;
  auto loaded = parser.loadGltf(buffer.get(), path.parent_path(), options);
  if (loaded.error() != fastgltf::Error::None) {
    return std::unexpected(gltfError(path, "parsing", loaded.error()));
  }
  const fastgltf::Asset &asset = loaded.get();
  GltfImport import;

  // Meshes: one submesh per triangle primitive, with flat normals and tangents generated where a
  // primitive has none.
  for (std::size_t m = 0; m < asset.meshes.size(); ++m) {
    const fastgltf::Mesh &mesh = asset.meshes[m];
    GltfMesh out{.name = nameOr(mesh.name, "mesh", m), .data = {}, .materials = {}};
    bool needTangents = false;
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
      fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, positions, [&](glm::vec3 value, std::size_t i) {
        out.data.vertices[base + i].position = value;
        out.data.vertices[base + i].normal = {0.0f, 1.0f, 0.0f};
      });
      if (const auto normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, asset.accessors[normal->accessorIndex],
            [&](glm::vec3 value, std::size_t i) { out.data.vertices[base + i].normal = value; });
      } else {
        needFlatNormals = true;
      }
      if (const auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
            asset, asset.accessors[uv->accessorIndex],
            [&](glm::vec2 value, std::size_t i) { out.data.vertices[base + i].uv = value; });
      }
      if (const auto tangent = primitive.findAttribute("TANGENT"); tangent != primitive.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[tangent->accessorIndex],
            [&](glm::vec4 value, std::size_t i) { out.data.vertices[base + i].tangent = value; });
      } else {
        needTangents = true;
      }
      // The first set of joints and weights; a primitive without them keeps zero weights, which
      // the skinning pass leaves in the bind pose.
      const auto joints = primitive.findAttribute("JOINTS_0");
      const auto weights = primitive.findAttribute("WEIGHTS_0");
      if (joints != primitive.attributes.end() && weights != primitive.attributes.end()) {
        out.data.skin.resize(out.data.vertices.size());
        fastgltf::iterateAccessorWithIndex<glm::uvec4>(
            asset, asset.accessors[joints->accessorIndex],
            [&](glm::uvec4 value, std::size_t i) { out.data.skin[base + i].joints = value; });
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, asset.accessors[weights->accessorIndex],
            [&](glm::vec4 value, std::size_t i) { out.data.skin[base + i].weights = value; });
      }
      const fastgltf::Accessor &indices = asset.accessors[*primitive.indicesAccessor];
      const auto firstIndex = static_cast<std::uint32_t>(out.data.indices.size());
      fastgltf::iterateAccessor<std::uint32_t>(asset, indices,
                                               [&](std::uint32_t index) { out.data.indices.push_back(base + index); });
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
    if (out.data.vertices.empty() || out.data.indices.empty()) {
      SONNET_LOG_WARN("{}: mesh \"{}\" has no usable primitives", path.string(), out.name);
    } else if (needTangents) {
      renderer::generateTangents(out.data);
    }
    import.meshes.push_back(std::move(out));
  }

  // Images, each marked sRGB when any material reads it as colour.
  for (std::size_t i = 0; i < asset.images.size(); ++i) {
    auto bytes = imageBytes(asset, asset.images[i], path.parent_path());
    if (!bytes) {
      SONNET_LOG_WARN("{}: image {}: {}", path.string(), i, bytes.error().message);
      bytes = std::vector<std::byte>{};
    }
    import.images.push_back(
        GltfImage{.name = nameOr(asset.images[i].name, "image", i), .bytes = std::move(*bytes), .srgb = false});
  }

  for (std::size_t m = 0; m < asset.materials.size(); ++m) {
    const fastgltf::Material &material = asset.materials[m];
    GltfMaterial out{.name = nameOr(material.name, "material", m), .source = {}};
    const fastgltf::PBRData &pbr = material.pbrData;
    out.source.baseColor = {pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2],
                            pbr.baseColorFactor[3]};
    out.source.metallic = pbr.metallicFactor;
    out.source.roughness = pbr.roughnessFactor;
    out.source.emissive = {material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]};
    out.source.alphaCutoff = material.alphaCutoff;
    out.source.doubleSided = material.doubleSided;
    out.source.alphaMode = material.alphaMode == fastgltf::AlphaMode::Blend  ? renderer::AlphaMode::Blend
                           : material.alphaMode == fastgltf::AlphaMode::Mask ? renderer::AlphaMode::Mask
                                                                             : renderer::AlphaMode::Opaque;
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

  // The node hierarchy of the default scene, or of every root node without one.
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
  // Every visited node's path from the model's root, by glTF node index: what joints and channels
  // name their nodes by (ADR-0010). Siblings with one name make the path ambiguous.
  std::vector<std::string> paths(asset.nodes.size());
  std::vector<bool> visited(asset.nodes.size(), false);
  const auto visit = [&](auto &self, std::size_t nodeIndex, std::int32_t parent, const std::string &parentPath,
                         std::vector<std::string> &siblings) -> void {
    if (nodeIndex >= asset.nodes.size() || visited[nodeIndex]) {
      return;
    }
    visited[nodeIndex] = true;
    const fastgltf::Node &node = asset.nodes[nodeIndex];
    ModelNode out;
    out.name = nameOr(node.name, "node", nodeIndex);
    out.parent = parent;
    if (std::ranges::find(siblings, out.name) != siblings.end()) {
      SONNET_LOG_WARN("{}: two nodes named \"{}\" under one parent; animations and skins bind the first", path.string(),
                      out.name);
    }
    siblings.push_back(out.name);
    paths[nodeIndex] = parentPath.empty() ? out.name : parentPath + "/" + out.name;
    if (const auto *trs = std::get_if<fastgltf::TRS>(&node.transform)) {
      out.position = {trs->translation[0], trs->translation[1], trs->translation[2]};
      out.rotation = glm::quat{trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
      out.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    }
    const auto index = static_cast<std::int32_t>(import.model.nodes.size());
    import.model.nodes.push_back(std::move(out));
    import.meshIndices.push_back(node.meshIndex.has_value() ? static_cast<std::int32_t>(*node.meshIndex) : -1);
    import.skinIndices.push_back(
        node.meshIndex.has_value() && node.skinIndex.has_value() ? static_cast<std::int32_t>(*node.skinIndex) : -1);
    std::vector<std::string> children;
    for (const std::size_t child : node.children) {
      self(self, child, index, paths[nodeIndex], children);
    }
  };
  std::vector<std::string> rootNames;
  for (const std::size_t root : roots) {
    visit(visit, root, -1, std::string{}, rootNames);
  }

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
      fastgltf::iterateAccessorWithIndex<glm::mat4>(asset, asset.accessors[*skin.inverseBindMatrices],
                                                    [&](const glm::mat4 &value, std::size_t i) {
                                                      if (i < out.skin.inverseBindMatrices.size()) {
                                                        out.skin.inverseBindMatrices[i] = value;
                                                      }
                                                    });
    }
    import.skins.push_back(std::move(out));
  }

  // Animations: translation, rotation and scale channels; morph target weights are not played.
  for (std::size_t a = 0; a < asset.animations.size(); ++a) {
    const fastgltf::Animation &animation = asset.animations[a];
    GltfAnimation out{.name = nameOr(animation.name, "animation", a), .clip = {}};
    bool warnedWeights = false;
    for (const fastgltf::AnimationChannel &channel : animation.channels) {
      if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= asset.nodes.size() || !visited[*channel.nodeIndex] ||
          channel.samplerIndex >= animation.samplers.size()) {
        continue;
      }
      if (channel.path == fastgltf::AnimationPath::Weights) {
        if (!warnedWeights) {
          SONNET_LOG_WARN("{}: animation \"{}\" drives morph target weights, which are not played", path.string(),
                          out.name);
          warnedWeights = true;
        }
        continue;
      }
      const fastgltf::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
      AnimationChannel result;
      result.target = paths[*channel.nodeIndex];
      result.path = channel.path == fastgltf::AnimationPath::Translation ? AnimationPath::Translation
                    : channel.path == fastgltf::AnimationPath::Rotation  ? AnimationPath::Rotation
                                                                         : AnimationPath::Scale;
      result.interpolation = sampler.interpolation == fastgltf::AnimationInterpolation::Step ? Interpolation::Step
                             : sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline
                                 ? Interpolation::CubicSpline
                                 : Interpolation::Linear;
      fastgltf::iterateAccessor<float>(asset, asset.accessors[sampler.inputAccessor],
                                       [&](float time) { result.times.push_back(time); });
      const fastgltf::Accessor &output = asset.accessors[sampler.outputAccessor];
      if (result.path == AnimationPath::Rotation) {
        fastgltf::iterateAccessor<glm::vec4>(asset, output, [&](glm::vec4 value) { result.values.push_back(value); });
      } else {
        fastgltf::iterateAccessor<glm::vec3>(asset, output,
                                             [&](glm::vec3 value) { result.values.emplace_back(value, 0.0f); });
      }
      const std::size_t stride = result.interpolation == Interpolation::CubicSpline ? 3 : 1;
      if (result.times.empty() || result.values.size() != result.times.size() * stride ||
          !std::ranges::is_sorted(result.times)) {
        SONNET_LOG_WARN("{}: animation \"{}\" has a malformed channel for \"{}\", skipped", path.string(), out.name,
                        result.target);
        continue;
      }
      out.clip.duration = std::max(out.clip.duration, result.times.back());
      out.clip.channels.push_back(std::move(result));
    }
    import.animations.push_back(std::move(out));
  }
  SONNET_LOG_DEBUG("{}: {} meshes, {} materials, {} images, {} nodes, {} skins, {} animations", path.string(),
                   import.meshes.size(), import.materials.size(), import.images.size(), import.model.nodes.size(),
                   import.skins.size(), import.animations.size());
  return import;
}

} // namespace sonnet::assets
