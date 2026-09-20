#include <sonnet/assets/Bundle.h>

#include "BinaryIo.h"

#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/core/Version.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace sonnet::assets {

namespace {

using detail::ByteReader;
using detail::ByteWriter;
using nlohmann::json;

constexpr std::string_view Magic = "SONNETBN";
constexpr std::uint64_t HeaderSize = 32;

[[nodiscard]] core::Error ioError(const std::filesystem::path &path, std::string_view what) {
  return core::Error{std::format("{}: {}", path.string(), what), core::ErrorCategory::Io};
}

[[nodiscard]] core::Error payloadError(std::string_view what) {
  return core::Error{std::format("cooked payload: {}", what), core::ErrorCategory::Io};
}

// Braced json initialisation builds an array, so the value form is the one to use here: a slot
// without a material is null, everything else the canonical text.
[[nodiscard]] json uuidJson(const core::Uuid &uuid) {
  return uuid.isNil() ? json(nullptr) : json(uuid.toString());
}

[[nodiscard]] core::Uuid uuidFrom(const json &value) {
  if (!value.is_string()) {
    return {};
  }
  return core::Uuid::parse(value.get<std::string>()).value_or(core::Uuid{});
}

} // namespace

std::string_view toString(CookPlatform platform) noexcept {
  switch (platform) {
  case CookPlatform::Windows:
    return "windows";
  case CookPlatform::Linux:
    return "linux";
  case CookPlatform::MacOS:
    return "macos";
  }
  return "linux";
}

std::optional<CookPlatform> cookPlatformFromString(std::string_view name) noexcept {
  if (name == "windows") {
    return CookPlatform::Windows;
  }
  if (name == "linux") {
    return CookPlatform::Linux;
  }
  if (name == "macos") {
    return CookPlatform::MacOS;
  }
  return std::nullopt;
}

CookPlatform hostPlatform() noexcept {
#if defined(_WIN32)
  return CookPlatform::Windows;
#elif defined(__APPLE__)
  return CookPlatform::MacOS;
#else
  return CookPlatform::Linux;
#endif
}

// --- Bundle ----------------------------------------------------------------------------------

core::Result<Bundle> Bundle::open(const std::filesystem::path &file) {
  SONNET_ZONE();
  Bundle bundle;
  bundle.m_path = file;
  bundle.m_file.open(file, std::ios::binary);
  if (!bundle.m_file) {
    return std::unexpected(ioError(file, "cannot open the bundle"));
  }

  std::array<char, HeaderSize> header{};
  bundle.m_file.read(header.data(), static_cast<std::streamsize>(header.size()));
  if (bundle.m_file.gcount() != static_cast<std::streamsize>(header.size())) {
    return std::unexpected(ioError(file, "not a bundle: the header is short"));
  }
  if (std::string_view{header.data(), Magic.size()} != Magic) {
    return std::unexpected(ioError(file, "not a bundle: wrong magic"));
  }
  const ByteReader fields{std::as_bytes(std::span{header}).subspan(Magic.size())};
  ByteReader reader = fields;
  const std::uint32_t version = reader.u32();
  if (version != BundleVersion) {
    return std::unexpected(ioError(file, std::format("bundle version {} is not version {}", version, BundleVersion)));
  }
  static_cast<void>(reader.u32()); // reserved
  const std::uint64_t indexOffset = reader.u64();
  const std::uint64_t indexSize = reader.u64();
  if (!reader.ok()) {
    return std::unexpected(ioError(file, "the header is short"));
  }

  const auto indexBytes = bundle.readSpan({.offset = indexOffset, .size = indexSize});
  if (!indexBytes) {
    return std::unexpected(indexBytes.error());
  }
  const json index = json::from_cbor(*indexBytes, true, false);
  if (index.is_discarded() || !index.is_object()) {
    return std::unexpected(ioError(file, "the index is not readable"));
  }

  const json &manifest = index.value("manifest", json::object());
  bundle.m_manifest.name = manifest.value("name", std::string{});
  bundle.m_manifest.engineVersion = manifest.value("engineVersion", std::string{});
  bundle.m_manifest.platform =
      cookPlatformFromString(manifest.value("platform", std::string{"linux"})).value_or(CookPlatform::Linux);
  bundle.m_manifest.startScene = manifest.value("startScene", std::string{});

  for (const json &entry : index.value("assets", json::array())) {
    BundleAsset asset;
    asset.uuid = uuidFrom(entry.value("uuid", json{}));
    asset.type = assetTypeFromString(entry.value("type", std::string{})).value_or(AssetType::Texture);
    asset.name = entry.value("name", std::string{});
    asset.parent = uuidFrom(entry.value("parent", json{}));
    for (const json &material : entry.value("materials", json::array())) {
      asset.materials.push_back(uuidFrom(material));
    }
    if (asset.uuid.isNil()) {
      continue;
    }
    bundle.m_assetSpans[asset.uuid] = {.offset = entry.value("offset", std::uint64_t{0}),
                                       .size = entry.value("size", std::uint64_t{0})};
    bundle.m_assets.push_back(std::move(asset));
  }
  for (const auto &[path, entry] : index.value("files", json::object()).items()) {
    bundle.m_fileSpans[path] = {.offset = entry.value("offset", std::uint64_t{0}),
                                .size = entry.value("size", std::uint64_t{0})};
  }
  SONNET_LOG_INFO("opened bundle \"{}\" ({} assets, {} files, cooked for {} by {})", bundle.m_manifest.name,
                  bundle.m_assets.size(), bundle.m_fileSpans.size(), toString(bundle.m_manifest.platform),
                  bundle.m_manifest.engineVersion);
  return bundle;
}

std::vector<std::string> Bundle::files() const {
  std::vector<std::string> paths;
  paths.reserve(m_fileSpans.size());
  for (const auto &[path, span] : m_fileSpans) {
    paths.push_back(path);
  }
  return paths;
}

bool Bundle::contains(const core::Uuid &uuid) const {
  return m_assetSpans.contains(uuid);
}

bool Bundle::contains(std::string_view path) const {
  return m_fileSpans.contains(path);
}

core::Result<std::vector<std::byte>> Bundle::read(const core::Uuid &uuid) const {
  const auto entry = m_assetSpans.find(uuid);
  if (entry == m_assetSpans.end()) {
    return std::unexpected(ioError(m_path, std::format("no asset {} in the bundle", uuid.toString())));
  }
  return readSpan(entry->second);
}

core::Result<std::vector<std::byte>> Bundle::read(std::string_view path) const {
  const auto entry = m_fileSpans.find(path);
  if (entry == m_fileSpans.end()) {
    return std::unexpected(ioError(m_path, std::format("no file \"{}\" in the bundle", path)));
  }
  return readSpan(entry->second);
}

core::Result<std::vector<std::byte>> Bundle::readSpan(const Span &span) const {
  std::vector<std::byte> bytes(span.size);
  if (span.size == 0) {
    return bytes;
  }
  m_file.clear();
  m_file.seekg(static_cast<std::streamoff>(span.offset));
  m_file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(span.size));
  if (m_file.gcount() != static_cast<std::streamsize>(span.size)) {
    return std::unexpected(ioError(m_path, "the bundle ends inside a payload"));
  }
  return bytes;
}

// --- BundleWriter ------------------------------------------------------------------------------

core::Result<BundleWriter> BundleWriter::create(const std::filesystem::path &file, BundleManifest manifest) {
  BundleWriter writer;
  writer.m_path = file;
  writer.m_manifest = std::move(manifest);
  if (writer.m_manifest.engineVersion.empty()) {
    writer.m_manifest.engineVersion = core::engineVersion().toString();
  }
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  writer.m_file.open(file, std::ios::binary | std::ios::trunc);
  if (!writer.m_file) {
    return std::unexpected(ioError(file, "cannot open the bundle for writing"));
  }
  // The header is written again by finish, once the index's place is known.
  const std::array<char, HeaderSize> blank{};
  writer.m_file.write(blank.data(), static_cast<std::streamsize>(blank.size()));
  writer.m_offset = HeaderSize;
  if (!writer.m_file) {
    return std::unexpected(ioError(file, "cannot write the bundle header"));
  }
  return writer;
}

core::Result<void> BundleWriter::append(std::span<const std::byte> payload, std::uint64_t &offset) {
  offset = m_offset;
  if (!payload.empty()) {
    m_file.write(reinterpret_cast<const char *>(payload.data()), static_cast<std::streamsize>(payload.size()));
  }
  if (!m_file) {
    return std::unexpected(ioError(m_path, "writing a payload failed"));
  }
  m_offset += payload.size();
  return {};
}

core::Result<void> BundleWriter::addAsset(const BundleAsset &asset, std::span<const std::byte> payload) {
  std::uint64_t offset = 0;
  if (auto written = append(payload, offset); !written) {
    return written;
  }
  json entry{{"uuid", asset.uuid.toString()},
             {"type", toString(asset.type)},
             {"name", asset.name},
             {"offset", offset},
             {"size", payload.size()}};
  if (!asset.parent.isNil()) {
    entry["parent"] = asset.parent.toString();
  }
  if (!asset.materials.empty()) {
    json materials = json::array();
    for (const core::Uuid &material : asset.materials) {
      materials.push_back(uuidJson(material));
    }
    entry["materials"] = std::move(materials);
  }
  m_assets.push_back(std::move(entry));
  return {};
}

core::Result<void> BundleWriter::addFile(std::string path, std::span<const std::byte> payload) {
  std::uint64_t offset = 0;
  if (auto written = append(payload, offset); !written) {
    return written;
  }
  m_files[std::move(path)] = json{{"offset", offset}, {"size", payload.size()}};
  return {};
}

core::Result<void> BundleWriter::finish() {
  SONNET_ZONE();
  const json index{
      {"manifest", json{{"name", m_manifest.name},
                        {"engineVersion", m_manifest.engineVersion},
                        {"platform", toString(m_manifest.platform)},
                        {"startScene", m_manifest.startScene}}},
      {"assets", m_assets},
      {"files", m_files},
  };
  const std::vector<std::uint8_t> indexBytes = json::to_cbor(index);
  const std::uint64_t indexOffset = m_offset;
  m_file.write(reinterpret_cast<const char *>(indexBytes.data()), static_cast<std::streamsize>(indexBytes.size()));
  m_offset += indexBytes.size();

  ByteWriter header;
  header.tag(Magic.substr(0, 4));
  header.tag(Magic.substr(4, 4));
  header.u32(BundleVersion);
  header.u32(0); // reserved
  header.u64(indexOffset);
  header.u64(indexBytes.size());
  const std::vector<std::byte> headerBytes = header.take();
  m_file.seekp(0);
  m_file.write(reinterpret_cast<const char *>(headerBytes.data()), static_cast<std::streamsize>(headerBytes.size()));
  m_file.close();
  if (!m_file) {
    return std::unexpected(ioError(m_path, "writing the bundle failed"));
  }
  SONNET_LOG_INFO("wrote {} ({} assets, {} files, {} bytes)", m_path.string(), m_assets.size(), m_files.size(),
                  m_offset);
  return {};
}

// --- Payloads ----------------------------------------------------------------------------------

std::vector<std::byte> encodeMesh(const renderer::MeshData &mesh) {
  ByteWriter writer;
  writer.tag("MESH");
  writer.array(std::span{mesh.vertices});
  writer.array(std::span{mesh.indices});
  writer.array(std::span{mesh.submeshes});
  writer.array(std::span{mesh.skin});
  return writer.take();
}

core::Result<renderer::MeshData> decodeMesh(std::span<const std::byte> payload) {
  ByteReader reader{payload};
  if (!reader.tag("MESH")) {
    return std::unexpected(payloadError("not a mesh"));
  }
  renderer::MeshData mesh;
  reader.array(mesh.vertices);
  reader.array(mesh.indices);
  reader.array(mesh.submeshes);
  reader.array(mesh.skin);
  if (!reader.ok()) {
    return std::unexpected(payloadError("the mesh is truncated"));
  }
  return mesh;
}

std::vector<std::byte> encodeTexture(const renderer::TextureData &texture) {
  ByteWriter writer;
  writer.tag("TEX2");
  writer.u32(texture.size.x);
  writer.u32(texture.size.y);
  writer.u32(static_cast<std::uint32_t>(texture.format));
  writer.u32(texture.mipLevels);
  writer.u32(texture.cube ? 1u : 0u);
  writer.array(std::span{texture.data});
  return writer.take();
}

core::Result<renderer::TextureData> decodeTexture(std::span<const std::byte> payload) {
  ByteReader reader{payload};
  if (!reader.tag("TEX2")) {
    return std::unexpected(payloadError("not a texture"));
  }
  renderer::TextureData texture;
  texture.size.x = reader.u32();
  texture.size.y = reader.u32();
  texture.format = static_cast<rhi::Format>(reader.u32());
  texture.mipLevels = reader.u32();
  texture.cube = reader.u32() != 0;
  reader.array(texture.data);
  if (!reader.ok()) {
    return std::unexpected(payloadError("the texture is truncated"));
  }
  if (texture.data.size() != texture.expectedSize()) {
    return std::unexpected(payloadError("the texture data does not match its description"));
  }
  return texture;
}

std::vector<std::byte> encodeSkin(const Skin &skin) {
  ByteWriter writer;
  writer.tag("SKIN");
  writer.u32(static_cast<std::uint32_t>(skin.joints.size()));
  for (const std::string &joint : skin.joints) {
    writer.string(joint);
  }
  writer.array(std::span{skin.inverseBindMatrices});
  return writer.take();
}

core::Result<Skin> decodeSkin(std::span<const std::byte> payload) {
  ByteReader reader{payload};
  if (!reader.tag("SKIN")) {
    return std::unexpected(payloadError("not a skin"));
  }
  Skin skin;
  const std::uint32_t joints = reader.u32();
  // A string is at least its four-byte length, so a count past that cannot be honest.
  if (!reader.ok() || joints > reader.remaining() / sizeof(std::uint32_t)) {
    return std::unexpected(payloadError("the skin is truncated"));
  }
  skin.joints.reserve(joints);
  for (std::uint32_t joint = 0; joint < joints; ++joint) {
    skin.joints.push_back(reader.string());
  }
  reader.array(skin.inverseBindMatrices);
  if (!reader.ok()) {
    return std::unexpected(payloadError("the skin is truncated"));
  }
  return skin;
}

std::vector<std::byte> encodeAnimation(const AnimationClip &clip) {
  ByteWriter writer;
  writer.tag("ANIM");
  writer.f32(clip.duration);
  writer.u32(static_cast<std::uint32_t>(clip.channels.size()));
  for (const AnimationChannel &channel : clip.channels) {
    writer.string(channel.target);
    writer.u32(static_cast<std::uint32_t>(channel.path));
    writer.u32(static_cast<std::uint32_t>(channel.interpolation));
    writer.array(std::span{channel.times});
    writer.array(std::span{channel.values});
  }
  return writer.take();
}

core::Result<AnimationClip> decodeAnimation(std::span<const std::byte> payload) {
  ByteReader reader{payload};
  if (!reader.tag("ANIM")) {
    return std::unexpected(payloadError("not an animation clip"));
  }
  AnimationClip clip;
  clip.duration = reader.f32();
  const std::uint32_t channels = reader.u32();
  if (!reader.ok() || channels > reader.remaining() / sizeof(std::uint32_t)) {
    return std::unexpected(payloadError("the clip is truncated"));
  }
  clip.channels.resize(channels);
  for (AnimationChannel &channel : clip.channels) {
    channel.target = reader.string();
    channel.path = static_cast<AnimationPath>(reader.u32());
    channel.interpolation = static_cast<Interpolation>(reader.u32());
    reader.array(channel.times);
    reader.array(channel.values);
  }
  if (!reader.ok()) {
    return std::unexpected(payloadError("the clip is truncated"));
  }
  return clip;
}

std::vector<std::byte> encodeModel(const Model &model) {
  ByteWriter writer;
  writer.tag("MODL");
  writer.u32(static_cast<std::uint32_t>(model.nodes.size()));
  for (const ModelNode &node : model.nodes) {
    writer.string(node.name);
    writer.pod(node.parent);
    writer.pod(node.position);
    writer.pod(node.rotation);
    writer.pod(node.scale);
    writer.pod(node.mesh.bytes());
    writer.pod(node.skin.bytes());
  }
  writer.u32(static_cast<std::uint32_t>(model.animations.size()));
  for (const core::Uuid &animation : model.animations) {
    writer.pod(animation.bytes());
  }
  return writer.take();
}

core::Result<Model> decodeModel(std::span<const std::byte> payload) {
  ByteReader reader{payload};
  if (!reader.tag("MODL")) {
    return std::unexpected(payloadError("not a model"));
  }
  Model model;
  const std::uint32_t nodes = reader.u32();
  if (!reader.ok() || nodes > reader.remaining() / sizeof(std::uint32_t)) {
    return std::unexpected(payloadError("the model is truncated"));
  }
  model.nodes.resize(nodes);
  for (ModelNode &node : model.nodes) {
    node.name = reader.string();
    node.parent = reader.pod<std::int32_t>();
    node.position = reader.pod<glm::vec3>();
    node.rotation = reader.pod<glm::quat>();
    node.scale = reader.pod<glm::vec3>();
    node.mesh = core::Uuid{reader.pod<core::Uuid::Bytes>()};
    node.skin = core::Uuid{reader.pod<core::Uuid::Bytes>()};
  }
  const std::uint32_t animations = reader.u32();
  if (!reader.ok() || animations > reader.remaining() / sizeof(core::Uuid::Bytes)) {
    return std::unexpected(payloadError("the model is truncated"));
  }
  model.animations.reserve(animations);
  for (std::uint32_t animation = 0; animation < animations; ++animation) {
    model.animations.emplace_back(reader.pod<core::Uuid::Bytes>());
  }
  if (!reader.ok()) {
    return std::unexpected(payloadError("the model is truncated"));
  }
  return model;
}

std::vector<std::byte> encodeJson(const json &document) {
  const std::vector<std::uint8_t> cbor = json::to_cbor(document);
  const auto *bytes = reinterpret_cast<const std::byte *>(cbor.data());
  return {bytes, bytes + cbor.size()};
}

core::Result<json> decodeJson(std::span<const std::byte> payload) {
  const json document = json::from_cbor(payload, true, false);
  if (document.is_discarded()) {
    return std::unexpected(payloadError("not readable as CBOR"));
  }
  return document;
}

} // namespace sonnet::assets
