#include <sonnet/assets/AssetDatabase.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>
#include <sonnet/renderer/Primitives.h>

#include <algorithm>
#include <format>
#include <span>
#include <system_error>

namespace sonnet::assets {

namespace {

using nlohmann::json;

// Version 2 lists a glTF file's skins and animations among its sub-assets; a version 1 sidecar of
// a glTF file is rebuilt on scan.
constexpr int SidecarVersion = 2;
constexpr std::chrono::milliseconds PollInterval{500};

enum class SourceKind {
  Unknown,
  Image,
  Ktx2,
  Hdr,
  Gltf,
  Material,
  Script,
  Sound,
};

// Lower-cased extension, with the double extension of material files.
SourceKind kindOf(const std::filesystem::path &file) {
  std::string extension = file.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string filename = file.filename().string();
  if (filename.ends_with(".material.json")) {
    return SourceKind::Material;
  }
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
      extension == ".bmp") {
    return SourceKind::Image;
  }
  if (extension == ".ktx2") {
    return SourceKind::Ktx2;
  }
  if (extension == ".hdr") {
    return SourceKind::Hdr;
  }
  if (extension == ".gltf" || extension == ".glb") {
    return SourceKind::Gltf;
  }
  if (extension == ".lua") {
    return SourceKind::Script;
  }
  if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac") {
    return SourceKind::Sound;
  }
  return SourceKind::Unknown;
}

AssetType typeOf(SourceKind kind) {
  switch (kind) {
  case SourceKind::Image:
  case SourceKind::Ktx2:
    return AssetType::Texture;
  case SourceKind::Hdr:
    return AssetType::Environment;
  case SourceKind::Gltf:
    return AssetType::Model;
  case SourceKind::Material:
    return AssetType::Material;
  case SourceKind::Script:
    return AssetType::Script;
  case SourceKind::Sound:
    return AssetType::Sound;
  case SourceKind::Unknown:
    break;
  }
  return AssetType::Texture;
}

std::string stemOf(const std::filesystem::path &file) {
  std::string name = file.filename().string();
  if (name.ends_with(".material.json")) {
    return name.substr(0, name.size() - std::string_view{".material.json"}.size());
  }
  return file.stem().string();
}

std::filesystem::file_time_type modificationTime(const std::filesystem::path &file) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(file, error);
  return error ? std::filesystem::file_time_type{} : time;
}

// A glTF sidecar's sub-asset list is keyed on the file's content rather than its modification
// time, so a sidecar committed with a project stays valid in every checkout.
std::string contentHash(const std::filesystem::path &file) {
  const auto bytes = core::readFile(file);
  if (!bytes) {
    return {};
  }
  std::uint64_t hash = 0xCBF29CE484222325ull;
  for (const std::byte byte : *bytes) {
    hash ^= std::to_integer<std::uint64_t>(byte);
    hash *= 0x100000001B3ull;
  }
  return std::format("{:016x}", hash);
}

// A whole file, or a logged failure the caller does not retry: the project-mode counterpart of
// `bundlePayload`, so the two modes read the same way at the call site.
std::optional<std::vector<std::byte>> readOptionalFile(const std::filesystem::path &path, const core::Uuid &uuid,
                                                       std::unordered_map<core::Uuid, bool> &failed) {
  auto bytes = core::readFile(path);
  if (!bytes) {
    SONNET_LOG_ERROR("{}", bytes.error().toString());
    failed[uuid] = true;
    return std::nullopt;
  }
  return std::move(*bytes);
}

core::Result<json> readJsonFile(const std::filesystem::path &path) {
  const auto bytes = core::readFile(path);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  json document = json::parse(bytes->begin(), bytes->end(), nullptr, false);
  if (document.is_discarded()) {
    return std::unexpected(core::Error{std::format("{}: not valid JSON", path.string()), core::ErrorCategory::Io});
  }
  return document;
}

// A model's nodes name their meshes and skins by index into the file, and its clips are the file's
// in order; the database hands all of them out by identities derived from the model's, as the
// sub-assets are registered.
void assignModelIdentities(const core::Uuid &uuid, Model &model, std::span<const std::int32_t> meshIndices,
                           std::span<const std::int32_t> skinIndices, std::size_t animationCount) {
  for (std::size_t n = 0; n < model.nodes.size(); ++n) {
    const std::int32_t meshIndex = meshIndices[n];
    const std::int32_t skinIndex = skinIndices[n];
    model.nodes[n].mesh = meshIndex >= 0 ? core::Uuid::derive(uuid, std::format("mesh/{}", meshIndex)) : core::Uuid{};
    model.nodes[n].skin = skinIndex >= 0 ? core::Uuid::derive(uuid, std::format("skin/{}", skinIndex)) : core::Uuid{};
  }
  model.animations.clear();
  for (std::size_t i = 0; i < animationCount; ++i) {
    model.animations.push_back(core::Uuid::derive(uuid, std::format("animation/{}", i)));
  }
}

} // namespace

AssetDatabase::AssetDatabase(renderer::Renderer &renderer, core::JobSystem &jobs) : m_renderer(renderer), m_jobs(jobs) {
  registerBuiltins();
}

AssetDatabase::~AssetDatabase() {
  close();
  // The built-in meshes and the placeholder outlive projects.
  for (auto &[uuid, handle] : m_meshes) {
    m_renderer.destroyMesh(handle);
  }
  if (m_placeholder) {
    m_renderer.destroyTexture(m_placeholder);
  }
}

void AssetDatabase::registerBuiltins() {
  const std::array<std::pair<core::Uuid, const char *>, 5> builtins{{{builtin::box(), "Box"},
                                                                     {builtin::sphere(), "Sphere"},
                                                                     {builtin::plane(), "Plane"},
                                                                     {builtin::cylinder(), "Cylinder"},
                                                                     {builtin::capsule(), "Capsule"}}};
  for (const auto &[uuid, name] : builtins) {
    m_assets[uuid] = AssetInfo{
        .uuid = uuid, .type = AssetType::Mesh, .source = "builtin", .name = name, .parent = {}, .materials = {}};
  }
}

void AssetDatabase::open(const std::filesystem::path &projectRoot, std::span<const std::string> roots) {
  SONNET_ZONE();
  close();
  m_projectRoot = std::filesystem::absolute(projectRoot).lexically_normal();
  m_roots.assign(roots.begin(), roots.end());
  std::vector<std::filesystem::path> files;
  for (const std::string &root : m_roots) {
    const std::filesystem::path directory = m_projectRoot / root;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
      continue;
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator{directory, error}) {
      if (entry.is_regular_file() && kindOf(entry.path()) != SourceKind::Unknown) {
        files.push_back(entry.path());
      }
    }
  }
  std::ranges::sort(files);
  for (const std::filesystem::path &file : files) {
    scanFile(file);
  }
  SONNET_LOG_INFO("asset database: {} assets in {}", m_assets.size() - 5, m_projectRoot.string());
}

void AssetDatabase::close() {
  // An import in flight names files and a cache this database is about to forget, and its publish
  // would write maps that are about to be cleared. Finish them first rather than racing them.
  waitForLoads();
  if (m_bundle) {
    // A cooked asset has no file record to unload through, so everything the bundle loaded is
    // released here; the built-in meshes come back on demand.
    for (const auto &[uuid, texture] : m_textures) {
      m_renderer.destroyTexture(texture.handle);
    }
    for (const auto &[uuid, environment] : m_environments) {
      m_renderer.destroyEnvironment(environment);
    }
    for (const auto &[uuid, mesh] : m_meshes) {
      m_renderer.destroyMesh(mesh);
    }
    for (const auto &[uuid, material] : m_materials) {
      m_renderer.destroyMaterial(material.handle);
    }
    m_textures.clear();
    m_environments.clear();
    m_meshes.clear();
    m_materials.clear();
    m_meshData.clear();
    m_models.clear();
    m_skins.clear();
    m_animations.clear();
    m_scripts.clear();
    m_sounds.clear();
    m_failed.clear();
  }
  std::vector<core::Uuid> files;
  for (const auto &[uuid, record] : m_files) {
    files.push_back(uuid);
  }
  for (const core::Uuid &uuid : files) {
    unloadFile(uuid);
  }
  std::erase_if(m_assets, [](const auto &entry) { return entry.second.source != "builtin"; });
  m_files.clear();
  m_bundle.reset();
  m_projectRoot.clear();
  m_roots.clear();
}

core::Result<void> AssetDatabase::openBundle(const std::filesystem::path &file) {
  SONNET_ZONE();
  close();
  auto bundle = Bundle::open(file);
  if (!bundle) {
    return std::unexpected(bundle.error());
  }
  for (const BundleAsset &asset : bundle->assets()) {
    // The source is where the asset came from, and a cooked one came from the bundle; nothing
    // in this mode opens it, and the browser and the inspector show it as the origin.
    m_assets[asset.uuid] = AssetInfo{.uuid = asset.uuid,
                                     .type = asset.type,
                                     .source = file,
                                     .name = asset.name,
                                     .parent = asset.parent,
                                     .materials = asset.materials};
  }
  m_bundle = std::move(*bundle);
  SONNET_LOG_INFO("asset database: {} cooked assets from {}", m_bundle->assets().size(), file.string());
  return {};
}

std::optional<std::vector<std::byte>> AssetDatabase::bundlePayload(const core::Uuid &uuid) {
  auto payload = m_bundle->read(uuid);
  if (!payload) {
    SONNET_LOG_ERROR("{}", payload.error().toString());
    m_failed[uuid] = true;
    return std::nullopt;
  }
  return std::move(*payload);
}

core::Result<json> AssetDatabase::readSidecar(const std::filesystem::path &sidecar) const {
  auto document = readJsonFile(sidecar);
  if (!document) {
    return document;
  }
  if (!document->is_object() || !document->contains("uuid") || !(*document)["uuid"].is_string() ||
      !core::Uuid::parse((*document)["uuid"].get_ref<const std::string &>())) {
    return std::unexpected(
        core::Error{std::format("{}: not a sidecar with a uuid", sidecar.string()), core::ErrorCategory::Io});
  }
  return document;
}

core::Result<void> AssetDatabase::writeSidecar(const core::Uuid &uuid) {
  const AssetInfo &info = m_assets.at(uuid);
  const FileRecord &record = m_files.at(uuid);
  json document{
      {"version", SidecarVersion},
      {"uuid", uuid.toString()},
      {"type", std::string{toString(info.type)}},
      {"settings", record.settings},
  };
  if (info.type == AssetType::Model) {
    document["sourceHash"] = record.sourceHash;
    json subAssets = json::array();
    for (const auto &[subUuid, sub] : m_assets) {
      if (sub.parent == uuid) {
        json materials = json::array();
        for (const core::Uuid &material : sub.materials) {
          materials.push_back(material.toString());
        }
        subAssets.push_back(json{{"uuid", subUuid.toString()},
                                 {"type", std::string{toString(sub.type)}},
                                 {"name", sub.name},
                                 {"materials", std::move(materials)}});
      }
    }
    document["subAssets"] = std::move(subAssets);
  }
  return core::writeFile(record.sidecar, document.dump(2) + "\n");
}

void AssetDatabase::registerGltfSubAssets(const core::Uuid &uuid, const json &subAssets) {
  const AssetInfo &parent = m_assets.at(uuid);
  for (const json &entry : subAssets) {
    if (!entry.is_object() || !entry.contains("uuid") || !entry.contains("type")) {
      continue;
    }
    const auto subUuid = core::Uuid::parse(entry["uuid"].get<std::string>());
    if (!subUuid) {
      continue;
    }
    const std::optional<AssetType> type = assetTypeFromString(entry["type"].get<std::string>());
    if (!type) {
      continue;
    }
    AssetInfo info{.uuid = *subUuid,
                   .type = *type,
                   .source = parent.source,
                   .name = entry.value("name", std::string{}),
                   .parent = uuid,
                   .materials = {}};
    if (entry.contains("materials") && entry["materials"].is_array()) {
      for (const json &material : entry["materials"]) {
        info.materials.push_back(material.is_string()
                                     ? core::Uuid::parse(material.get<std::string>()).value_or(core::Uuid{})
                                     : core::Uuid{});
      }
    }
    m_assets[*subUuid] = std::move(info);
  }
}

core::Result<json> AssetDatabase::listGltfSubAssets(const core::Uuid &uuid, const std::filesystem::path &file) {
  auto import = importGltf(file);
  if (!import) {
    return std::unexpected(import.error());
  }
  json subAssets = json::array();
  const auto add = [&](const char *kind, std::size_t index, std::string name, const char *type, json materials) {
    subAssets.push_back(json{{"uuid", core::Uuid::derive(uuid, std::format("{}/{}", kind, index)).toString()},
                             {"type", type},
                             {"name", std::move(name)},
                             {"materials", std::move(materials)}});
  };
  for (std::size_t i = 0; i < import->meshes.size(); ++i) {
    json materials = json::array();
    for (const std::int32_t material : import->meshes[i].materials) {
      materials.push_back(material >= 0 ? core::Uuid::derive(uuid, std::format("material/{}", material)).toString()
                                        : core::Uuid{}.toString());
    }
    add("mesh", i, import->meshes[i].name, "Mesh", std::move(materials));
  }
  for (std::size_t i = 0; i < import->materials.size(); ++i) {
    add("material", i, import->materials[i].name, "Material", json::array());
  }
  for (std::size_t i = 0; i < import->images.size(); ++i) {
    add("image", i, import->images[i].name, "Texture", json::array());
  }
  for (std::size_t i = 0; i < import->skins.size(); ++i) {
    add("skin", i, import->skins[i].name, "Skin", json::array());
  }
  for (std::size_t i = 0; i < import->animations.size(); ++i) {
    add("animation", i, import->animations[i].name, "Animation", json::array());
  }
  return subAssets;
}

void AssetDatabase::scanFile(const std::filesystem::path &file) {
  const SourceKind kind = kindOf(file);
  const std::filesystem::path sidecar = file.string() + ".meta";
  const std::filesystem::file_time_type sourceTime = modificationTime(file);
  json document;
  bool rewrite = false;
  if (auto existing = readSidecar(sidecar)) {
    document = std::move(*existing);
  } else {
    document = json{{"version", SidecarVersion},
                    {"uuid", core::Uuid::generate().toString()},
                    {"settings", kind == SourceKind::Image ? TextureSettings{}.toJson() : json::object()}};
    rewrite = true;
  }
  const core::Uuid uuid = core::Uuid::parse(document["uuid"].get<std::string>()).value();
  if (m_assets.contains(uuid)) {
    SONNET_LOG_ERROR("{}: identity {} is already used by {}, skipped", file.string(), uuid.toString(),
                     m_assets[uuid].source.string());
    return;
  }
  m_assets[uuid] = AssetInfo{
      .uuid = uuid, .type = typeOf(kind), .source = file, .name = stemOf(file), .parent = {}, .materials = {}};
  m_files[uuid] = FileRecord{.sidecar = sidecar,
                             .sourceTime = sourceTime,
                             .sourceHash = {},
                             .settings = document.contains("settings") ? document["settings"] : json::object()};
  if (kind == SourceKind::Gltf) {
    // The sub-asset list is kept in the sidecar so a scan does not parse every glTF file; it is
    // rebuilt when the file's content changed since.
    m_files[uuid].sourceHash = contentHash(file);
    const bool stale = !document.contains("subAssets") || document.value("version", 1) < SidecarVersion ||
                       document.value("sourceHash", std::string{}) != m_files[uuid].sourceHash;
    if (stale) {
      if (auto subAssets = listGltfSubAssets(uuid, file)) {
        document["subAssets"] = std::move(*subAssets);
        rewrite = true;
      } else {
        SONNET_LOG_ERROR("{}", subAssets.error().toString());
      }
    }
    if (document.contains("subAssets")) {
      registerGltfSubAssets(uuid, document["subAssets"]);
    }
  }
  if (rewrite) {
    if (const auto written = writeSidecar(uuid); !written) {
      SONNET_LOG_ERROR("{}", written.error().toString());
    } else {
      SONNET_LOG_DEBUG("wrote {}", sidecar.string());
    }
  }
}

const AssetInfo *AssetDatabase::find(const core::Uuid &uuid) const {
  const auto it = m_assets.find(uuid);
  return it != m_assets.end() ? &it->second : nullptr;
}

const AssetInfo *AssetDatabase::findByPath(const std::filesystem::path &source) const {
  const std::filesystem::path normalised = std::filesystem::absolute(source).lexically_normal();
  for (const auto &[uuid, info] : m_assets) {
    if (info.parent.isNil() && info.source == normalised) {
      return &info;
    }
  }
  return nullptr;
}

std::vector<const AssetInfo *> AssetDatabase::assets(std::optional<AssetType> type) const {
  std::vector<const AssetInfo *> result;
  for (const auto &[uuid, info] : m_assets) {
    if (!type || info.type == *type) {
      result.push_back(&info);
    }
  }
  std::ranges::sort(result, [](const AssetInfo *a, const AssetInfo *b) {
    return a->name != b->name ? a->name < b->name : a->uuid < b->uuid;
  });
  return result;
}

// ---- Loading ----

renderer::TextureHandle AssetDatabase::uploadTexture(const core::Uuid &uuid, const renderer::TextureData &data,
                                                     const std::string &name) {
  const renderer::TextureHandle handle = m_renderer.createTexture(data, name);
  if (handle) {
    m_textures[uuid] = LoadedTexture{handle};
  }
  return handle;
}

std::optional<AssetDatabase::TextureRequest> AssetDatabase::textureRequest(const core::Uuid &uuid,
                                                                           const AssetInfo &info) {
  const auto record = m_files.find(uuid);
  if (record == m_files.end()) {
    return std::nullopt;
  }
  return TextureRequest{.uuid = uuid,
                        .source = info.source,
                        .sidecar = record->second.sidecar,
                        .cache = cacheDirectory(),
                        .name = info.name,
                        .sourceTime = record->second.sourceTime,
                        .settings = TextureSettings::fromJson(record->second.settings),
                        .blockCompression = m_renderer.blockCompressionSupported()};
}

std::optional<renderer::TextureData> AssetDatabase::importFileTexture(const TextureRequest &request) {
  SONNET_ZONE();
  if (kindOf(request.source) == SourceKind::Ktx2) {
    const auto bytes = core::readFile(request.source);
    if (!bytes) {
      SONNET_LOG_ERROR("{}", bytes.error().toString());
      return std::nullopt;
    }
    auto data = readKtx2(*bytes, request.blockCompression);
    if (!data) {
      SONNET_LOG_ERROR("{}: {}", request.source.string(), data.error().toString());
      return std::nullopt;
    }
    return std::move(*data);
  }
  // Cooked on demand into the cache, keyed by identity; stale when the source or its settings
  // are newer (docs/assets.md, "Source and cooked").
  const std::filesystem::path cooked = request.cache / (request.uuid.toString() + ".ktx2");
  const auto cookedTime = modificationTime(cooked);
  const bool fresh = cookedTime != std::filesystem::file_time_type{} && cookedTime >= request.sourceTime &&
                     cookedTime >= modificationTime(request.sidecar);
  if (!fresh) {
    const auto bytes = core::readFile(request.source);
    if (!bytes) {
      SONNET_LOG_ERROR("{}", bytes.error().toString());
      return std::nullopt;
    }
    const auto imported = importImage(*bytes, request.settings);
    if (!imported) {
      SONNET_LOG_ERROR("{}: {}", request.source.string(), imported.error().toString());
      return std::nullopt;
    }
    const auto ktx = cookKtx2(*imported, request.settings.compress);
    if (!ktx) {
      SONNET_LOG_ERROR("{}: {}", request.source.string(), ktx.error().toString());
      return std::nullopt;
    }
    if (const auto written = core::writeFile(cooked, *ktx); !written) {
      SONNET_LOG_ERROR("{}", written.error().toString());
      return std::nullopt;
    }
    SONNET_LOG_INFO("cooked {} into {}", request.source.filename().string(), cooked.filename().string());
  }
  const auto bytes = core::readFile(cooked);
  if (!bytes) {
    SONNET_LOG_ERROR("{}", bytes.error().toString());
    return std::nullopt;
  }
  auto data = readKtx2(*bytes, request.blockCompression);
  if (!data) {
    SONNET_LOG_ERROR("{}: {}", cooked.string(), data.error().toString());
    return std::nullopt;
  }
  return std::move(*data);
}

renderer::TextureHandle AssetDatabase::loadFileTexture(const core::Uuid &uuid, const AssetInfo &info) {
  const std::optional<TextureRequest> request = textureRequest(uuid, info);
  if (!request) {
    return {};
  }
  const std::optional<renderer::TextureData> data = importFileTexture(*request);
  if (!data) {
    return {};
  }
  return uploadTexture(uuid, *data, info.name);
}

renderer::MaterialDesc AssetDatabase::resolve(const MaterialSource &source) {
  renderer::MaterialDesc desc;
  desc.baseColor = source.baseColor;
  desc.emissive = source.emissive;
  desc.metallic = source.metallic;
  desc.roughness = source.roughness;
  desc.normalScale = source.normalScale;
  desc.occlusionStrength = source.occlusionStrength;
  desc.alphaCutoff = source.alphaCutoff;
  // Requested, not loaded: resolving a material must not import its textures on the frame that
  // first draws it. A slot still importing reads the renderer's fallback, which means "no effect",
  // except the base colour, whose fallback would show the material's untextured colour as if it
  // were finished; publishing the texture resolves the material again.
  desc.baseColorTexture = requestTexture(source.baseColorTexture);
  if (!desc.baseColorTexture && texturePending(source.baseColorTexture)) {
    desc.baseColorTexture = placeholderTexture();
  }
  desc.metallicRoughnessTexture = requestTexture(source.metallicRoughnessTexture);
  desc.normalTexture = requestTexture(source.normalTexture);
  desc.occlusionTexture = requestTexture(source.occlusionTexture);
  desc.emissiveTexture = requestTexture(source.emissiveTexture);
  desc.alphaMode = source.alphaMode;
  desc.wrap = source.wrap;
  desc.doubleSided = source.doubleSided;
  return desc;
}

std::optional<AssetDatabase::GltfRequest> AssetDatabase::gltfRequest(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Model) {
    return std::nullopt;
  }
  const auto record = m_files.find(uuid);
  if (record == m_files.end()) {
    return std::nullopt;
  }
  return GltfRequest{.uuid = uuid,
                     .source = info->source,
                     .cache = cacheDirectory(),
                     .name = info->name,
                     .sourceTime = record->second.sourceTime,
                     .blockCompression = m_renderer.blockCompressionSupported()};
}

AssetDatabase::GltfLoad AssetDatabase::importGltfFiles(const GltfRequest &request) {
  SONNET_ZONE();
  GltfLoad load;
  auto import = importGltf(request.source);
  if (!import) {
    SONNET_LOG_ERROR("{}", import.error().toString());
    return load;
  }
  // Images, cooked into the cache like file textures, so materials can resolve them. Decoding
  // only: the renderer objects are created by publishGltf on the main thread.
  for (std::size_t i = 0; i < import->images.size(); ++i) {
    const core::Uuid imageUuid = core::Uuid::derive(request.uuid, std::format("image/{}", i));
    load.imageUuids.push_back(imageUuid);
    load.images.emplace_back();
    const GltfImage &image = import->images[i];
    if (image.bytes.empty()) {
      continue;
    }
    const std::filesystem::path cooked = request.cache / (imageUuid.toString() + ".ktx2");
    const auto cookedTime = modificationTime(cooked);
    if (cookedTime == std::filesystem::file_time_type{} || cookedTime < request.sourceTime) {
      const auto imported = importImage(image.bytes, TextureSettings{.srgb = image.srgb});
      if (!imported) {
        SONNET_LOG_ERROR("{}: image \"{}\": {}", request.source.string(), image.name, imported.error().toString());
        continue;
      }
      const auto ktx = cookKtx2(*imported, true);
      if (!ktx || !core::writeFile(cooked, *ktx)) {
        SONNET_LOG_ERROR("{}: image \"{}\": cooking failed", request.source.string(), image.name);
        continue;
      }
    }
    const auto bytes = core::readFile(cooked);
    auto data = bytes ? readKtx2(*bytes, request.blockCompression) : std::unexpected(bytes.error());
    if (!data) {
      SONNET_LOG_ERROR("{}: image \"{}\": {}", request.source.string(), image.name, data.error().toString());
      continue;
    }
    load.images.back() = std::move(*data);
  }
  load.import = std::move(*import);
  return load;
}

bool AssetDatabase::publishGltf(const GltfRequest &request, GltfLoad &&load) {
  SONNET_ZONE();
  const core::Uuid &uuid = request.uuid;
  // Marked before anything resolves: a material resolving an image that failed to decode must not
  // request the file it is being published from.
  m_gltfLoaded[uuid] = false;
  if (!load.import) {
    return false;
  }
  GltfImport &import = *load.import;
  for (std::size_t i = 0; i < load.images.size(); ++i) {
    if (!load.images[i]) {
      continue;
    }
    static_cast<void>(
        uploadTexture(load.imageUuids[i], *load.images[i], std::format("{}/{}", request.name, import.images[i].name)));
  }
  const auto imageUuid = [&](std::int32_t index) {
    return index >= 0 && static_cast<std::size_t>(index) < load.imageUuids.size()
               ? load.imageUuids[static_cast<std::size_t>(index)]
               : core::Uuid{};
  };
  for (std::size_t i = 0; i < import.materials.size(); ++i) {
    const core::Uuid materialUuid = core::Uuid::derive(uuid, std::format("material/{}", i));
    GltfMaterial &material = import.materials[i];
    material.source.baseColorTexture = imageUuid(material.baseColorImage);
    material.source.metallicRoughnessTexture = imageUuid(material.metallicRoughnessImage);
    material.source.normalTexture = imageUuid(material.normalImage);
    material.source.occlusionTexture = imageUuid(material.occlusionImage);
    material.source.emissiveTexture = imageUuid(material.emissiveImage);
    auto existing = m_materials.find(materialUuid);
    if (existing != m_materials.end()) {
      existing->second.source = material.source;
      m_renderer.updateMaterial(existing->second.handle, resolve(material.source));
    } else {
      const renderer::MaterialHandle handle =
          m_renderer.createMaterial(resolve(material.source), std::format("{}/{}", request.name, material.name));
      m_materials[materialUuid] = LoadedMaterial{handle, material.source};
    }
  }
  for (std::size_t i = 0; i < import.meshes.size(); ++i) {
    const core::Uuid meshUuid = core::Uuid::derive(uuid, std::format("mesh/{}", i));
    GltfMesh &mesh = import.meshes[i];
    if (mesh.data.vertices.empty() || mesh.data.indices.empty()) {
      continue;
    }
    m_meshes[meshUuid] = m_renderer.createMesh(mesh.data, std::format("{}/{}", request.name, mesh.name));
    m_meshData[meshUuid] = std::move(mesh.data);
  }
  for (std::size_t i = 0; i < import.skins.size(); ++i) {
    Skin &skin = m_skins[core::Uuid::derive(uuid, std::format("skin/{}", i))];
    skin = std::move(import.skins[i].skin);
    skin.revision = ++m_revision;
  }
  for (std::size_t i = 0; i < import.animations.size(); ++i) {
    AnimationClip &clip = m_animations[core::Uuid::derive(uuid, std::format("animation/{}", i))];
    clip = std::move(import.animations[i].clip);
    clip.revision = ++m_revision;
  }
  // model() may have read this hierarchy from the JSON already, through the same walk, so it is
  // the same one; keep it where it is. Replacing it with an equal copy would free the nodes under
  // any caller holding one, and a caller asking for a node's mesh is how this import started. A
  // re-import erases the entry first, so a changed file still gets the new hierarchy.
  if (!m_models.contains(uuid)) {
    Model model = std::move(import.model);
    assignModelIdentities(uuid, model, import.meshIndices, import.skinIndices, import.animations.size());
    m_models[uuid] = std::move(model);
  }
  m_gltfLoaded[uuid] = true;
  return true;
}

bool AssetDatabase::loadGltf(const core::Uuid &uuid) {
  SONNET_ZONE();
  finishRequest(uuid);
  if (m_gltfLoaded.contains(uuid)) {
    return m_gltfLoaded[uuid];
  }
  const std::optional<GltfRequest> request = gltfRequest(uuid);
  if (!request) {
    return false;
  }
  return publishGltf(*request, importGltfFiles(*request));
}

renderer::MeshHandle AssetDatabase::mesh(const core::Uuid &uuid) {
  if (const auto it = m_meshes.find(uuid); it != m_meshes.end()) {
    return it->second;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Mesh || m_failed.contains(uuid)) {
    return {};
  }
  if (info->source == "builtin") {
    renderer::MeshData data;
    if (uuid == builtin::box()) {
      data = renderer::primitives::box();
    } else if (uuid == builtin::sphere()) {
      data = renderer::primitives::sphere();
    } else if (uuid == builtin::plane()) {
      data = renderer::primitives::plane();
    } else if (uuid == builtin::cylinder()) {
      data = renderer::primitives::cylinder();
    } else {
      data = renderer::primitives::capsule();
    }
    m_meshes[uuid] = m_renderer.createMesh(data, info->name);
    m_meshData[uuid] = std::move(data);
    return m_meshes[uuid];
  }
  if (m_bundle) {
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return {};
    }
    auto data = decodeMesh(*payload);
    if (!data) {
      SONNET_LOG_ERROR("{}: {}", info->name, data.error().toString());
      m_failed[uuid] = true;
      return {};
    }
    m_meshes[uuid] = m_renderer.createMesh(*data, info->name);
    m_meshData[uuid] = std::move(*data);
    return m_meshes[uuid];
  }
  if (!info->parent.isNil() && loadGltf(info->parent)) {
    if (const auto it = m_meshes.find(uuid); it != m_meshes.end()) {
      return it->second;
    }
  }
  m_failed[uuid] = true;
  return {};
}

void AssetDatabase::schedule(const core::Uuid &uuid, std::function<void()> work) {
  m_pending[uuid] = PendingLoad{m_jobs.schedule("asset import", std::move(work))};
}

void AssetDatabase::finishRequest(const core::Uuid &file) {
  const auto it = m_pending.find(file);
  if (it == m_pending.end()) {
    return;
  }
  // A copy: the wait may run the publish, which erases the entry and the handle in it.
  const core::JobHandle job = it->second.job;
  m_jobs.wait(job);
  // The import scheduled its publish on the main thread, which is this one; running it is what
  // erases the entry, if the wait has not already.
  static_cast<void>(m_jobs.runMainThreadJobs());
}

bool AssetDatabase::texturePending(const core::Uuid &uuid) const {
  if (uuid.isNil()) {
    return false;
  }
  if (m_pending.contains(uuid)) {
    return true;
  }
  const AssetInfo *info = find(uuid);
  return info != nullptr && !info->parent.isNil() && m_pending.contains(info->parent);
}

renderer::TextureHandle AssetDatabase::placeholderTexture() {
  if (!m_placeholder) {
    m_placeholder =
        m_renderer.createTexture(renderer::solidTexture({128, 128, 128, 255}, rhi::Format::R8G8B8A8Srgb), "pending");
  }
  return m_placeholder;
}

void AssetDatabase::waitForLoads() {
  // Each pass finishes the imports in flight and runs the main-thread jobs they scheduled, which
  // is what erases them; a publish schedules nothing new, so the loop drains.
  while (!m_pending.empty()) {
    std::vector<core::JobHandle> jobs;
    jobs.reserve(m_pending.size());
    for (const auto &[uuid, pending] : m_pending) {
      jobs.push_back(pending.job);
    }
    m_jobs.wait(jobs);
    static_cast<void>(m_jobs.runMainThreadJobs());
  }
}

renderer::MeshHandle AssetDatabase::requestMesh(const core::Uuid &uuid) {
  if (const auto it = m_meshes.find(uuid); it != m_meshes.end()) {
    return it->second;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Mesh || m_failed.contains(uuid)) {
    return {};
  }
  // A built-in is already in memory and a bundle payload is a decode away; neither is worth a
  // round trip through the pool, and the synchronous path is what everything else already calls.
  if (info->source == "builtin" || m_bundle) {
    return mesh(uuid);
  }
  if (info->parent.isNil()) {
    return {};
  }
  // A glTF mesh arrives with the rest of its file, so the request is the file's.
  requestGltf(info->parent);
  return {};
}

void AssetDatabase::requestGltf(const core::Uuid &uuid) {
  if (m_gltfLoaded.contains(uuid) || m_pending.contains(uuid)) {
    return; // loaded, whether or not the asset asked for is in it, failed, or still importing
  }
  const std::optional<GltfRequest> request = gltfRequest(uuid);
  if (!request) {
    return;
  }
  const auto load = std::make_shared<GltfLoad>();
  schedule(uuid, [this, request = *request, load] {
    *load = importGltfFiles(request);
    m_jobs.scheduleOnMainThread("asset publish", [this, request, load] {
      m_pending.erase(request.uuid);
      const std::vector<core::Uuid> images = load->imageUuids;
      static_cast<void>(publishGltf(request, std::move(*load)));
      // The file's own materials resolved against its images as they were published; a material
      // file reading one of them showed the placeholder until now.
      for (const core::Uuid &image : images) {
        refreshMaterials(image);
      }
    });
  });
}

renderer::TextureHandle AssetDatabase::requestTexture(const core::Uuid &uuid) {
  if (uuid.isNil()) {
    return {};
  }
  if (const auto it = m_textures.find(uuid); it != m_textures.end()) {
    return it->second.handle;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Texture || m_failed.contains(uuid)) {
    return {};
  }
  if (m_bundle) {
    return texture(uuid);
  }
  if (!info->parent.isNil()) {
    requestGltf(info->parent);
    return {};
  }
  if (m_pending.contains(uuid)) {
    return {};
  }
  const std::optional<TextureRequest> request = textureRequest(uuid, *info);
  if (!request) {
    return {};
  }
  const auto data = std::make_shared<std::optional<renderer::TextureData>>();
  schedule(uuid, [this, request = *request, data] {
    *data = importFileTexture(request);
    m_jobs.scheduleOnMainThread("asset publish", [this, request, data] {
      m_pending.erase(request.uuid);
      if (*data) {
        static_cast<void>(uploadTexture(request.uuid, **data, request.name));
      } else {
        m_failed[request.uuid] = true;
      }
      // Either way the placeholder goes: the texture, or the fallback for one that failed.
      refreshMaterials(request.uuid);
    });
  });
  return {};
}

renderer::TextureHandle AssetDatabase::texture(const core::Uuid &uuid) {
  if (uuid.isNil()) {
    return {};
  }
  if (const auto it = m_textures.find(uuid); it != m_textures.end()) {
    return it->second.handle;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Texture || m_failed.contains(uuid)) {
    return {};
  }
  renderer::TextureHandle handle;
  if (m_bundle) {
    // A cooked texture is the KTX2 the editor caches, sub-asset or file asset alike.
    if (const auto payload = bundlePayload(uuid)) {
      const auto data = readKtx2(*payload, m_renderer.blockCompressionSupported());
      if (data) {
        handle = uploadTexture(uuid, *data, info->name);
      } else {
        SONNET_LOG_ERROR("{}: {}", info->name, data.error().toString());
      }
    }
  } else if (!info->parent.isNil()) {
    if (loadGltf(info->parent)) {
      if (const auto it = m_textures.find(uuid); it != m_textures.end()) {
        handle = it->second.handle;
      }
    }
  } else {
    finishRequest(uuid);
    if (const auto it = m_textures.find(uuid); it != m_textures.end()) {
      return it->second.handle;
    }
    if (m_failed.contains(uuid)) {
      return {};
    }
    handle = loadFileTexture(uuid, *info);
  }
  if (!handle) {
    m_failed[uuid] = true;
  }
  return handle;
}

renderer::MaterialHandle AssetDatabase::material(const core::Uuid &uuid) {
  if (const auto it = m_materials.find(uuid); it != m_materials.end()) {
    return it->second.handle;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Material || m_failed.contains(uuid)) {
    return {};
  }
  if (m_bundle) {
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return {};
    }
    const auto document = decodeJson(*payload);
    const auto source = document ? loadMaterial(*document) : std::unexpected(document.error());
    if (!source) {
      SONNET_LOG_ERROR("{}: {}", info->name, source.error().toString());
      m_failed[uuid] = true;
      return {};
    }
    const renderer::MaterialHandle handle = m_renderer.createMaterial(resolve(*source), info->name);
    m_materials[uuid] = LoadedMaterial{handle, *source};
    return handle;
  }
  if (!info->parent.isNil()) {
    if (loadGltf(info->parent)) {
      if (const auto it = m_materials.find(uuid); it != m_materials.end()) {
        return it->second.handle;
      }
    }
    m_failed[uuid] = true;
    return {};
  }
  const auto document = readJsonFile(info->source);
  const auto source = document ? loadMaterial(*document) : std::unexpected(document.error());
  if (!source) {
    SONNET_LOG_ERROR("{}: {}", info->source.string(), source.error().toString());
    m_failed[uuid] = true;
    return {};
  }
  const renderer::MaterialHandle handle = m_renderer.createMaterial(resolve(*source), info->name);
  m_materials[uuid] = LoadedMaterial{handle, *source};
  return handle;
}

renderer::EnvironmentHandle AssetDatabase::environment(const core::Uuid &uuid) {
  if (const auto it = m_environments.find(uuid); it != m_environments.end()) {
    return it->second;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Environment || m_failed.contains(uuid)) {
    return {};
  }
  renderer::TextureData map;
  if (m_bundle) {
    // Cooked, an environment is its decoded RGBA16F map rather than the .hdr file it came from.
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return {};
    }
    auto decoded = decodeTexture(*payload);
    if (!decoded) {
      SONNET_LOG_ERROR("{}: {}", info->name, decoded.error().toString());
      m_failed[uuid] = true;
      return {};
    }
    map = std::move(*decoded);
  } else {
    const auto bytes = core::readFile(info->source);
    auto data = bytes ? importHdr(*bytes) : std::unexpected(bytes.error());
    if (!data) {
      SONNET_LOG_ERROR("{}: {}", info->source.string(), data.error().toString());
      m_failed[uuid] = true;
      return {};
    }
    map = std::move(*data);
  }
  const renderer::EnvironmentHandle handle = m_renderer.createEnvironment(map, info->name);
  if (!handle) {
    m_failed[uuid] = true;
    return {};
  }
  m_environments[uuid] = handle;
  return handle;
}

const Model *AssetDatabase::model(const core::Uuid &uuid) {
  if (const auto it = m_models.find(uuid); it != m_models.end()) {
    return &it->second;
  }
  if (m_bundle) {
    const AssetInfo *info = find(uuid);
    if (info == nullptr || info->type != AssetType::Model || m_failed.contains(uuid)) {
      return nullptr;
    }
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return nullptr;
    }
    auto model = decodeModel(*payload);
    if (!model) {
      SONNET_LOG_ERROR("{}: {}", info->name, model.error().toString());
      m_failed[uuid] = true;
      return nullptr;
    }
    m_models[uuid] = std::move(*model);
    return &m_models[uuid];
  }
  // A source glTF: the hierarchy from the JSON alone, which is what a prefab needs. Importing the
  // meshes and images here is what made opening a project with models stall (roadmap.md, "Scene
  // loading blocked the frame"); they arrive when something requests them.
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Model || m_failed.contains(uuid)) {
    return nullptr;
  }
  auto structure = importGltfStructure(info->source);
  if (!structure) {
    SONNET_LOG_ERROR("{}", structure.error().toString());
    m_failed[uuid] = true;
    return nullptr;
  }
  Model model = std::move(structure->model);
  assignModelIdentities(uuid, model, structure->meshIndices, structure->skinIndices, structure->animationCount);
  m_models[uuid] = std::move(model);
  return &m_models[uuid];
}

const renderer::MeshData *AssetDatabase::meshData(const core::Uuid &uuid) {
  if (!mesh(uuid)) {
    return nullptr;
  }
  const auto it = m_meshData.find(uuid);
  return it != m_meshData.end() ? &it->second : nullptr;
}

const ScriptSource *AssetDatabase::script(const core::Uuid &uuid) {
  if (const auto it = m_scripts.find(uuid); it != m_scripts.end()) {
    return &it->second;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Script || m_failed.contains(uuid)) {
    return nullptr;
  }
  const auto bytes = m_bundle ? bundlePayload(uuid) : readOptionalFile(info->source, uuid, m_failed);
  if (!bytes) {
    return nullptr;
  }
  ScriptSource &source = m_scripts[uuid];
  source.code.assign(reinterpret_cast<const char *>(bytes->data()), bytes->size());
  source.revision = ++m_revision;
  return &source;
}

const SoundSource *AssetDatabase::sound(const core::Uuid &uuid) {
  if (const auto it = m_sounds.find(uuid); it != m_sounds.end()) {
    return &it->second;
  }
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Sound || m_failed.contains(uuid)) {
    return nullptr;
  }
  auto bytes = m_bundle ? bundlePayload(uuid) : readOptionalFile(info->source, uuid, m_failed);
  if (!bytes) {
    return nullptr;
  }
  SoundSource &source = m_sounds[uuid];
  source.bytes = std::move(*bytes);
  source.revision = ++m_revision;
  return &source;
}

const Skin *AssetDatabase::skin(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Skin) {
    return nullptr;
  }
  if (m_bundle) {
    if (const auto it = m_skins.find(uuid); it != m_skins.end()) {
      return &it->second;
    }
    if (m_failed.contains(uuid)) {
      return nullptr;
    }
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return nullptr;
    }
    auto skin = decodeSkin(*payload);
    if (!skin) {
      SONNET_LOG_ERROR("{}: {}", info->name, skin.error().toString());
      m_failed[uuid] = true;
      return nullptr;
    }
    skin->revision = ++m_revision;
    m_skins[uuid] = std::move(*skin);
    return &m_skins[uuid];
  }
  if (!loadGltf(info->parent)) {
    return nullptr;
  }
  const auto it = m_skins.find(uuid);
  return it != m_skins.end() ? &it->second : nullptr;
}

const AnimationClip *AssetDatabase::animation(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Animation) {
    return nullptr;
  }
  if (m_bundle) {
    if (const auto it = m_animations.find(uuid); it != m_animations.end()) {
      return &it->second;
    }
    if (m_failed.contains(uuid)) {
      return nullptr;
    }
    const auto payload = bundlePayload(uuid);
    if (!payload) {
      return nullptr;
    }
    auto clip = decodeAnimation(*payload);
    if (!clip) {
      SONNET_LOG_ERROR("{}: {}", info->name, clip.error().toString());
      m_failed[uuid] = true;
      return nullptr;
    }
    clip->revision = ++m_revision;
    m_animations[uuid] = std::move(*clip);
    return &m_animations[uuid];
  }
  if (!loadGltf(info->parent)) {
    return nullptr;
  }
  const auto it = m_animations.find(uuid);
  return it != m_animations.end() ? &it->second : nullptr;
}

const Skin *AssetDatabase::requestSkin(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Skin) {
    return nullptr;
  }
  if (m_bundle) {
    return skin(uuid); // a decode away, as requestMesh reasons
  }
  if (const auto it = m_skins.find(uuid); it != m_skins.end()) {
    return &it->second;
  }
  requestGltf(info->parent);
  return nullptr;
}

const AnimationClip *AssetDatabase::requestAnimation(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Animation) {
    return nullptr;
  }
  if (m_bundle) {
    return animation(uuid);
  }
  if (const auto it = m_animations.find(uuid); it != m_animations.end()) {
    return &it->second;
  }
  requestGltf(info->parent);
  return nullptr;
}

// ---- Materials ----

const MaterialSource *AssetDatabase::materialSource(const core::Uuid &uuid) {
  if (!material(uuid)) {
    return nullptr;
  }
  return &m_materials.at(uuid).source;
}

void AssetDatabase::setMaterialSource(const core::Uuid &uuid, const MaterialSource &source) {
  if (!material(uuid)) {
    return;
  }
  LoadedMaterial &loaded = m_materials.at(uuid);
  loaded.source = source;
  m_renderer.updateMaterial(loaded.handle, resolve(source));
}

core::Result<void> AssetDatabase::saveMaterial(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Material || !info->parent.isNil()) {
    return std::unexpected(core::Error{"not a material file", core::ErrorCategory::Io});
  }
  const MaterialSource *source = materialSource(uuid);
  if (source == nullptr) {
    return std::unexpected(
        core::Error{std::format("{} is not loaded", info->source.string()), core::ErrorCategory::Io});
  }
  const auto written = core::writeFile(info->source, assets::saveMaterial(*source).dump(2) + "\n");
  if (written) {
    m_files.at(uuid).sourceTime = modificationTime(info->source); // not a change to reload
  }
  return written;
}

core::Result<core::Uuid> AssetDatabase::createMaterial(const std::filesystem::path &file,
                                                       const MaterialSource &source) {
  if (!isOpen()) {
    return std::unexpected(core::Error{"no project is open", core::ErrorCategory::Io});
  }
  const std::filesystem::path path = std::filesystem::absolute(file).lexically_normal();
  if (kindOf(path) != SourceKind::Material) {
    return std::unexpected(
        core::Error{std::format("{}: a material file ends in .material.json", path.string()), core::ErrorCategory::Io});
  }
  if (const auto written = core::writeFile(path, assets::saveMaterial(source).dump(2) + "\n"); !written) {
    return std::unexpected(written.error());
  }
  scanFile(path);
  const AssetInfo *info = findByPath(path);
  if (info == nullptr) {
    return std::unexpected(core::Error{std::format("{}: not registered", path.string()), core::ErrorCategory::Io});
  }
  return info->uuid;
}

core::Result<core::Uuid> AssetDatabase::createScript(const std::filesystem::path &file, std::string_view code) {
  if (!isOpen()) {
    return std::unexpected(core::Error{"no project is open", core::ErrorCategory::Io});
  }
  const std::filesystem::path path = std::filesystem::absolute(file).lexically_normal();
  if (kindOf(path) != SourceKind::Script) {
    return std::unexpected(
        core::Error{std::format("{}: a script file ends in .lua", path.string()), core::ErrorCategory::Io});
  }
  if (const auto written = core::writeFile(path, code); !written) {
    return std::unexpected(written.error());
  }
  scanFile(path);
  const AssetInfo *info = findByPath(path);
  if (info == nullptr) {
    return std::unexpected(core::Error{std::format("{}: not registered", path.string()), core::ErrorCategory::Io});
  }
  return info->uuid;
}

// ---- Settings and reload ----

TextureSettings AssetDatabase::textureSettings(const core::Uuid &uuid) const {
  const auto it = m_files.find(uuid);
  return it != m_files.end() ? TextureSettings::fromJson(it->second.settings) : TextureSettings{};
}

core::Result<void> AssetDatabase::setTextureSettings(const core::Uuid &uuid, const TextureSettings &settings) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr || info->type != AssetType::Texture || !m_files.contains(uuid)) {
    return std::unexpected(core::Error{"not a texture file", core::ErrorCategory::Io});
  }
  m_files.at(uuid).settings = settings.toJson();
  if (const auto written = writeSidecar(uuid); !written) {
    return written;
  }
  return reimport(uuid);
}

void AssetDatabase::unloadTexture(const core::Uuid &uuid) {
  if (const auto it = m_textures.find(uuid); it != m_textures.end()) {
    m_renderer.destroyTexture(it->second.handle);
    m_textures.erase(it);
  }
}

void AssetDatabase::unloadFile(const core::Uuid &uuid) {
  const AssetInfo *info = find(uuid);
  if (info == nullptr) {
    return;
  }
  m_failed.erase(uuid);
  switch (info->type) {
  case AssetType::Texture:
    unloadTexture(uuid);
    break;
  case AssetType::Environment:
    if (const auto it = m_environments.find(uuid); it != m_environments.end()) {
      m_renderer.destroyEnvironment(it->second);
      m_environments.erase(it);
    }
    break;
  case AssetType::Material:
    // File materials keep their handle across reloads, so scenes and draw lists stay valid.
    break;
  case AssetType::Model: {
    std::vector<core::Uuid> subAssets;
    for (const auto &[subUuid, sub] : m_assets) {
      if (sub.parent == uuid) {
        subAssets.push_back(subUuid);
      }
    }
    for (const core::Uuid &subUuid : subAssets) {
      m_failed.erase(subUuid);
      unloadTexture(subUuid);
      if (const auto it = m_meshes.find(subUuid); it != m_meshes.end()) {
        m_renderer.destroyMesh(it->second);
        m_meshes.erase(it);
      }
      m_meshData.erase(subUuid);
      m_skins.erase(subUuid);
      m_animations.erase(subUuid);
    }
    m_models.erase(uuid);
    m_gltfLoaded.erase(uuid);
    break;
  }
  case AssetType::Mesh:
  case AssetType::Skin:
  case AssetType::Animation:
    break;
  case AssetType::Script:
    m_scripts.erase(uuid);
    break;
  case AssetType::Sound:
    m_sounds.erase(uuid);
    break;
  }
}

void AssetDatabase::refreshMaterials(const core::Uuid &textureUuid) {
  for (auto &[uuid, loaded] : m_materials) {
    const MaterialSource &s = loaded.source;
    if (s.baseColorTexture == textureUuid || s.metallicRoughnessTexture == textureUuid ||
        s.normalTexture == textureUuid || s.occlusionTexture == textureUuid || s.emissiveTexture == textureUuid) {
      m_renderer.updateMaterial(loaded.handle, resolve(s));
    }
  }
}

core::Result<void> AssetDatabase::reimport(const core::Uuid &requested) {
  SONNET_ZONE();
  const AssetInfo *info = find(requested);
  if (info == nullptr) {
    return std::unexpected(core::Error{std::format("unknown asset {}", requested.toString()), core::ErrorCategory::Io});
  }
  const core::Uuid uuid = info->parent.isNil() ? requested : info->parent;
  info = find(uuid);
  // A request in flight may have read the file before it changed; publish it first, so the
  // re-import below replaces it rather than the other way round.
  finishRequest(uuid);
  const auto record = m_files.find(uuid);
  if (record == m_files.end()) {
    return std::unexpected(core::Error{std::format("{} has no source file", info->name), core::ErrorCategory::Io});
  }
  record->second.sourceTime = modificationTime(info->source);
  if (info->type == AssetType::Model) {
    record->second.sourceHash = contentHash(info->source);
  }
  const AssetType type = info->type;
  const bool wasLoaded = m_textures.contains(uuid) || m_environments.contains(uuid) || m_gltfLoaded.contains(uuid) ||
                         m_materials.contains(uuid) || m_scripts.contains(uuid) || m_sounds.contains(uuid);
  unloadFile(uuid);
  if (type == AssetType::Model) {
    // The sub-asset list may have changed with the file.
    if (auto subAssets = listGltfSubAssets(uuid, info->source)) {
      registerGltfSubAssets(uuid, *subAssets);
    } else {
      return std::unexpected(subAssets.error());
    }
    if (const auto written = writeSidecar(uuid); !written) {
      SONNET_LOG_WARN("{}", written.error().toString());
    }
  }
  if (!wasLoaded) {
    return {};
  }
  switch (type) {
  case AssetType::Texture:
    if (!texture(uuid)) {
      return std::unexpected(
          core::Error{std::format("{}: re-import failed", info->source.string()), core::ErrorCategory::Io});
    }
    refreshMaterials(uuid);
    break;
  case AssetType::Environment:
    if (!environment(uuid)) {
      return std::unexpected(
          core::Error{std::format("{}: re-import failed", info->source.string()), core::ErrorCategory::Io});
    }
    break;
  case AssetType::Material: {
    const auto document = readJsonFile(info->source);
    const auto source = document ? loadMaterial(*document) : std::unexpected(document.error());
    if (!source) {
      return std::unexpected(source.error());
    }
    setMaterialSource(uuid, *source);
    break;
  }
  case AssetType::Model:
    if (!loadGltf(uuid)) {
      return std::unexpected(
          core::Error{std::format("{}: re-import failed", info->source.string()), core::ErrorCategory::Io});
    }
    for (const auto &[subUuid, sub] : m_assets) {
      if (sub.parent == uuid && sub.type == AssetType::Texture) {
        refreshMaterials(subUuid);
      }
    }
    break;
  case AssetType::Mesh:
  case AssetType::Skin:
  case AssetType::Animation:
    break;
  case AssetType::Script:
    if (script(uuid) == nullptr) {
      return std::unexpected(
          core::Error{std::format("{}: re-import failed", info->source.string()), core::ErrorCategory::Io});
    }
    break;
  case AssetType::Sound:
    if (sound(uuid) == nullptr) {
      return std::unexpected(
          core::Error{std::format("{}: re-import failed", info->source.string()), core::ErrorCategory::Io});
    }
    break;
  }
  SONNET_LOG_INFO("re-imported {}", info->source.filename().string());
  return {};
}

std::vector<core::Uuid> AssetDatabase::pollChanges() {
  std::vector<core::Uuid> changed;
  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastPoll < PollInterval) {
    return changed;
  }
  m_lastPoll = now;
  for (const auto &[uuid, record] : m_files) {
    const AssetInfo &info = m_assets.at(uuid);
    const auto time = modificationTime(info.source);
    if (time != std::filesystem::file_time_type{} && time != record.sourceTime) {
      changed.push_back(uuid);
    }
  }
  for (const core::Uuid &uuid : changed) {
    if (const auto result = reimport(uuid); !result) {
      SONNET_LOG_ERROR("{}", result.error().toString());
    }
  }
  return changed;
}

} // namespace sonnet::assets
