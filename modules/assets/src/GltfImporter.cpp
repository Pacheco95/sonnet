#include <sonnet/assets/Importers.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <format>
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

  // Meshes: one submesh per triangle primitive, tangents generated where a primitive has none.
  for (std::size_t m = 0; m < asset.meshes.size(); ++m) {
    const fastgltf::Mesh &mesh = asset.meshes[m];
    GltfMesh out{.name = nameOr(mesh.name, "mesh", m), .data = {}, .materials = {}};
    bool needTangents = false;
    for (const fastgltf::Primitive &primitive : mesh.primitives) {
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
        SONNET_LOG_WARN("{}: mesh \"{}\" has no normals, using up", path.string(), out.name);
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
      const fastgltf::Accessor &indices = asset.accessors[*primitive.indicesAccessor];
      const auto firstIndex = static_cast<std::uint32_t>(out.data.indices.size());
      fastgltf::iterateAccessor<std::uint32_t>(asset, indices,
                                               [&](std::uint32_t index) { out.data.indices.push_back(base + index); });
      out.data.submeshes.push_back(renderer::Submesh{.firstIndex = firstIndex,
                                                     .indexCount = static_cast<std::uint32_t>(indices.count),
                                                     .materialSlot = static_cast<std::uint32_t>(out.materials.size())});
      out.materials.push_back(primitive.materialIndex.has_value() ? static_cast<std::int32_t>(*primitive.materialIndex)
                                                                  : -1);
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
  const auto visit = [&](auto &self, std::size_t nodeIndex, std::int32_t parent) -> void {
    if (nodeIndex >= asset.nodes.size()) {
      return;
    }
    const fastgltf::Node &node = asset.nodes[nodeIndex];
    ModelNode out;
    out.name = nameOr(node.name, "node", nodeIndex);
    out.parent = parent;
    if (const auto *trs = std::get_if<fastgltf::TRS>(&node.transform)) {
      out.position = {trs->translation[0], trs->translation[1], trs->translation[2]};
      out.rotation = glm::quat{trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
      out.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
    }
    const auto index = static_cast<std::int32_t>(import.model.nodes.size());
    import.model.nodes.push_back(std::move(out));
    import.meshIndices.push_back(node.meshIndex.has_value() ? static_cast<std::int32_t>(*node.meshIndex) : -1);
    for (const std::size_t child : node.children) {
      self(self, child, index);
    }
  };
  for (const std::size_t root : roots) {
    visit(visit, root, -1);
  }
  SONNET_LOG_DEBUG("{}: {} meshes, {} materials, {} images, {} nodes", path.string(), import.meshes.size(),
                   import.materials.size(), import.images.size(), import.model.nodes.size());
  return import;
}

} // namespace sonnet::assets
