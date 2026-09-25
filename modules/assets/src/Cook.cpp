#include <sonnet/assets/Cook.h>

#include <sonnet/assets/AssetDatabase.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <algorithm>
#include <format>
#include <optional>

namespace sonnet::assets {

namespace {

using nlohmann::json;

// The name the player looks for beside its binary when it is started without an argument.
constexpr const char *BundleFileName = "game.sbundle";

[[nodiscard]] std::vector<std::byte> textBytes(std::string_view text) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return {data, data + text.size()};
}

// A texture is cooked the moment it is loaded, into `.sonnet/cache/<uuid>.ktx2`; a source that
// is already KTX2 is used as it is and never reaches the cache (docs/assets.md, "Textures").
[[nodiscard]] std::optional<std::vector<std::byte>> cookedTexture(AssetDatabase &database, const AssetInfo &info) {
  if (!database.texture(info.uuid)) {
    return std::nullopt;
  }
  const std::filesystem::path cached = database.cacheDirectory() / (info.uuid.toString() + ".ktx2");
  if (auto bytes = core::readFile(cached)) {
    return std::move(*bytes);
  }
  if (auto bytes = core::readFile(info.source)) {
    return std::move(*bytes);
  }
  return std::nullopt;
}

} // namespace

core::Result<CookReport> cook(AssetDatabase &database, const Project &project, const CookOptions &options) {
  SONNET_ZONE();
  if (database.projectRoot() != project.root) {
    return std::unexpected(core::Error{
        std::format("the database is open on {}, not on {}", database.projectRoot().string(), project.root.string()),
        core::ErrorCategory::Io});
  }

  const auto scenes = project.files(".scene.json");
  std::optional<std::filesystem::path> selectedScene;
  if (options.scene) {
    const std::filesystem::path requested =
        (options.scene->is_absolute() ? *options.scene : project.resolve(options.scene->generic_string()))
            .lexically_normal();
    if (std::ranges::find(scenes, requested) == scenes.end()) {
      return std::unexpected(core::Error{std::format("{}: scene is not in the project", options.scene->string()),
                                         core::ErrorCategory::Io});
    }
    selectedScene = requested;
  }

  CookReport report;
  report.bundle = options.outputDirectory / BundleFileName;
  auto writer = BundleWriter::create(
      report.bundle, {.name = project.name,
                      .engineVersion = {},
                      .platform = options.platform,
                      .startScene = selectedScene ? project.relative(*selectedScene) : project.startScene});
  if (!writer) {
    return std::unexpected(writer.error());
  }

  for (const AssetInfo *info : database.assets()) {
    // The five primitives are registered by every database, cooked or not; there is nothing of
    // them to put in a bundle.
    if (info->source == "builtin") {
      continue;
    }
    std::optional<std::vector<std::byte>> payload;
    switch (info->type) {
    case AssetType::Mesh:
      if (const renderer::MeshData *data = database.meshData(info->uuid)) {
        MeshCookStatistics statistics;
        payload = encodeMesh(cookMesh(*data, &statistics));
        report.meshes.verticesBefore += statistics.verticesBefore;
        report.meshes.verticesAfter += statistics.verticesAfter;
        report.meshes.cacheMissesBefore += statistics.cacheMissesBefore;
        report.meshes.cacheMissesAfter += statistics.cacheMissesAfter;
      }
      break;
    case AssetType::Texture:
      payload = cookedTexture(database, *info);
      break;
    case AssetType::Material:
      if (const MaterialSource *source = database.materialSource(info->uuid)) {
        payload = encodeJson(saveMaterial(*source));
      }
      break;
    case AssetType::Environment:
      // The player is handed the decoded map: the .hdr decoder is import-side work.
      if (const auto bytes = core::readFile(info->source)) {
        if (const auto map = importHdr(*bytes)) {
          payload = encodeTexture(*map);
        }
      }
      break;
    case AssetType::Model:
      if (const Model *model = database.model(info->uuid)) {
        payload = encodeModel(*model);
      }
      break;
    case AssetType::Script:
      if (const ScriptSource *script = database.script(info->uuid)) {
        payload = textBytes(script->code);
      }
      break;
    case AssetType::Sound:
      if (const SoundSource *sound = database.sound(info->uuid)) {
        payload = sound->bytes;
      }
      break;
    case AssetType::Skin:
      if (const Skin *skin = database.skin(info->uuid)) {
        payload = encodeSkin(*skin);
      }
      break;
    case AssetType::Animation:
      if (const AnimationClip *clip = database.animation(info->uuid)) {
        payload = encodeAnimation(*clip);
      }
      break;
    }
    if (!payload) {
      // One asset that will not cook leaves a hole in the bundle, not a failed export: the
      // player logs a missing asset and carries on, as the editor does.
      report.warnings.push_back(std::format("{} \"{}\" could not be cooked", toString(info->type), info->name));
      continue;
    }
    const auto added = writer->addAsset({.uuid = info->uuid,
                                         .type = info->type,
                                         .name = info->name,
                                         .parent = info->parent,
                                         .materials = info->materials},
                                        *payload);
    if (!added) {
      return std::unexpected(added.error());
    }
    ++report.assetCount;
  }

  // Scenes and prefabs keep their project-relative paths, which is how project.json names the
  // start scene and how the editor lists them.
  for (const std::string_view suffix : {".scene.json", ".prefab.json"}) {
    for (const std::filesystem::path &file : suffix == ".scene.json" ? scenes : project.files(suffix)) {
      if (selectedScene && suffix == ".scene.json" && file != *selectedScene) {
        continue;
      }
      const auto bytes = core::readFile(file);
      if (!bytes) {
        report.warnings.push_back(bytes.error().message);
        continue;
      }
      const json document = parseJson(*bytes);
      if (document.is_discarded()) {
        report.warnings.push_back(std::format("{}: not valid JSON", file.string()));
        continue;
      }
      const auto added = writer->addFile(project.relative(file), encodeJson(document));
      if (!added) {
        return std::unexpected(added.error());
      }
      ++report.fileCount;
    }
  }

  report.bytes = writer->bytesWritten();
  if (const auto finished = writer->finish(); !finished) {
    return std::unexpected(finished.error());
  }
  SONNET_LOG_INFO("cooked \"{}\" for {}: {} assets, {} scenes and prefabs, {} warnings", project.name,
                  toString(options.platform), report.assetCount, report.fileCount, report.warnings.size());
  for (const std::string &warning : report.warnings) {
    SONNET_LOG_WARN("{}", warning);
  }
  return report;
}

} // namespace sonnet::assets
