#include <sonnet/assets/Asset.h>

#include <format>

namespace sonnet::assets {

namespace {

using nlohmann::json;

json vec4ToJson(const glm::vec4 &v) {
  return json::array({v.x, v.y, v.z, v.w});
}

json vec3ToJson(const glm::vec3 &v) {
  return json::array({v.x, v.y, v.z});
}

template <typename Vec> bool vecFromJson(const json &value, Vec &out) {
  if (!value.is_array() || value.size() != static_cast<std::size_t>(Vec::length())) {
    return false;
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (!value[i].is_number()) {
      return false;
    }
    out[static_cast<int>(i)] = value[i].get<float>();
  }
  return true;
}

std::string_view toString(renderer::AlphaMode mode) noexcept {
  switch (mode) {
  case renderer::AlphaMode::Opaque:
    return "Opaque";
  case renderer::AlphaMode::Mask:
    return "Mask";
  case renderer::AlphaMode::Blend:
    return "Blend";
  }
  return "Opaque";
}

std::string_view toString(renderer::TextureWrap wrap) noexcept {
  switch (wrap) {
  case renderer::TextureWrap::Repeat:
    return "Repeat";
  case renderer::TextureWrap::ClampToEdge:
    return "ClampToEdge";
  case renderer::TextureWrap::MirroredRepeat:
    return "MirroredRepeat";
  }
  return "Repeat";
}

core::Uuid uuidFromJson(const json &object, const char *key) {
  if (!object.contains(key) || !object[key].is_string()) {
    return {};
  }
  return core::Uuid::parse(object[key].get_ref<const std::string &>()).value_or(core::Uuid{});
}

// Fixed identities, version 8 like derived ones, so a built-in mesh is never mistaken for a file.
core::Uuid builtinUuid(const char *name) noexcept {
  return core::Uuid::derive(core::Uuid{}, std::format("builtin/{}", name));
}

} // namespace

std::string_view toString(AssetType type) noexcept {
  switch (type) {
  case AssetType::Texture:
    return "Texture";
  case AssetType::Mesh:
    return "Mesh";
  case AssetType::Material:
    return "Material";
  case AssetType::Model:
    return "Model";
  case AssetType::Environment:
    return "Environment";
  }
  return "?";
}

TextureSettings TextureSettings::fromJson(const json &object) {
  TextureSettings settings;
  if (object.is_object()) {
    settings.srgb = object.value("srgb", settings.srgb);
    settings.mipmaps = object.value("mipmaps", settings.mipmaps);
    settings.compress = object.value("compress", settings.compress);
  }
  return settings;
}

json TextureSettings::toJson() const {
  return json{{"srgb", srgb}, {"mipmaps", mipmaps}, {"compress", compress}};
}

json saveMaterial(const MaterialSource &material) {
  json textures = json::object();
  const auto put = [&](const char *key, const core::Uuid &uuid) {
    if (!uuid.isNil()) {
      textures[key] = uuid.toString();
    }
  };
  put("baseColor", material.baseColorTexture);
  put("metallicRoughness", material.metallicRoughnessTexture);
  put("normal", material.normalTexture);
  put("occlusion", material.occlusionTexture);
  put("emissive", material.emissiveTexture);
  return json{
      {"version", MaterialFileVersion},
      {"baseColor", vec4ToJson(material.baseColor)},
      {"emissive", vec3ToJson(material.emissive)},
      {"metallic", material.metallic},
      {"roughness", material.roughness},
      {"normalScale", material.normalScale},
      {"occlusionStrength", material.occlusionStrength},
      {"alphaCutoff", material.alphaCutoff},
      {"alphaMode", std::string{toString(material.alphaMode)}},
      {"wrap", std::string{toString(material.wrap)}},
      {"doubleSided", material.doubleSided},
      {"textures", std::move(textures)},
  };
}

core::Result<MaterialSource> loadMaterial(const json &document) {
  if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer()) {
    return std::unexpected(core::Error{"not a material: no version field", core::ErrorCategory::Io});
  }
  const int version = document["version"].get<int>();
  if (version > MaterialFileVersion || version < 1) {
    return std::unexpected(core::Error{
        std::format("material version {} is not supported by this engine's {}", version, MaterialFileVersion),
        core::ErrorCategory::Io});
  }
  MaterialSource material;
  if (document.contains("baseColor")) {
    vecFromJson(document["baseColor"], material.baseColor);
  }
  if (document.contains("emissive")) {
    vecFromJson(document["emissive"], material.emissive);
  }
  material.metallic = document.value("metallic", material.metallic);
  material.roughness = document.value("roughness", material.roughness);
  material.normalScale = document.value("normalScale", material.normalScale);
  material.occlusionStrength = document.value("occlusionStrength", material.occlusionStrength);
  material.alphaCutoff = document.value("alphaCutoff", material.alphaCutoff);
  material.doubleSided = document.value("doubleSided", material.doubleSided);
  const std::string alphaMode = document.value("alphaMode", std::string{"Opaque"});
  material.alphaMode = alphaMode == "Blend"  ? renderer::AlphaMode::Blend
                       : alphaMode == "Mask" ? renderer::AlphaMode::Mask
                                             : renderer::AlphaMode::Opaque;
  const std::string wrap = document.value("wrap", std::string{"Repeat"});
  material.wrap = wrap == "ClampToEdge"      ? renderer::TextureWrap::ClampToEdge
                  : wrap == "MirroredRepeat" ? renderer::TextureWrap::MirroredRepeat
                                             : renderer::TextureWrap::Repeat;
  if (document.contains("textures") && document["textures"].is_object()) {
    const json &textures = document["textures"];
    material.baseColorTexture = uuidFromJson(textures, "baseColor");
    material.metallicRoughnessTexture = uuidFromJson(textures, "metallicRoughness");
    material.normalTexture = uuidFromJson(textures, "normal");
    material.occlusionTexture = uuidFromJson(textures, "occlusion");
    material.emissiveTexture = uuidFromJson(textures, "emissive");
  }
  return material;
}

namespace builtin {

core::Uuid box() noexcept {
  static const core::Uuid uuid = builtinUuid("box");
  return uuid;
}
core::Uuid sphere() noexcept {
  static const core::Uuid uuid = builtinUuid("sphere");
  return uuid;
}
core::Uuid plane() noexcept {
  static const core::Uuid uuid = builtinUuid("plane");
  return uuid;
}
core::Uuid cylinder() noexcept {
  static const core::Uuid uuid = builtinUuid("cylinder");
  return uuid;
}
core::Uuid capsule() noexcept {
  static const core::Uuid uuid = builtinUuid("capsule");
  return uuid;
}

} // namespace builtin

} // namespace sonnet::assets
