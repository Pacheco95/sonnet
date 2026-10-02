#include <sonnet/editor/Recovery.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <system_error>

namespace sonnet::editor {

namespace {

using nlohmann::json;

constexpr std::string_view FileSuffix = ".recovery.scene.json";
constexpr std::string_view TemporarySuffix = ".tmp";
constexpr int FormatVersion = 1;

// Writes beside the target and renames over it: the target is the old file or the new one, never a
// torn one, whatever happens to the process in between.
core::Result<void> writeAtomic(const std::filesystem::path &file, const std::string &text) {
  std::filesystem::path temporary = file;
  temporary += TemporarySuffix;
  if (const auto written = core::writeFile(temporary, text); !written) {
    return written;
  }
  std::error_code error;
  std::filesystem::rename(temporary, file, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return std::unexpected(core::Error{std::format("cannot replace {}", file.string()), core::ErrorCategory::Io});
  }
  return {};
}

bool hasSuffix(const std::filesystem::path &file, std::string_view suffix) {
  return file.filename().string().ends_with(suffix);
}

std::string newSession() {
  static std::atomic<unsigned> s_counter{0};
  const auto since = std::chrono::system_clock::now().time_since_epoch();
  return std::format("{:x}{:x}", std::chrono::duration_cast<std::chrono::milliseconds>(since).count(),
                     s_counter.fetch_add(1));
}

std::uint64_t fnv1a(std::string_view text) {
  std::uint64_t hash = 14695981039346656037ull;
  for (const char c : text) {
    hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
  }
  return hash;
}

} // namespace

Recovery::Recovery(std::filesystem::path directory) : m_directory(std::move(directory)), m_session(newSession()) {
}

std::filesystem::path Recovery::fileOf(std::uint64_t tab) const {
  return m_directory / std::format("{}-{}{}", m_session, tab, FileSuffix);
}

std::vector<std::filesystem::path> Recovery::files() const {
  std::vector<std::filesystem::path> found;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
    if (entry.is_regular_file(error) && hasSuffix(entry.path(), FileSuffix)) {
      found.push_back(entry.path());
    }
  }
  std::ranges::sort(found);
  return found;
}

std::filesystem::path Recovery::quarantineDirectory() {
  if (m_quarantine.empty()) {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
    m_quarantine = quarantineRoot(m_directory) / std::to_string(seconds.count());
  }
  std::error_code error;
  std::filesystem::create_directories(m_quarantine, error);
  return m_quarantine;
}

void Recovery::quarantine(const std::filesystem::path &file) {
  std::erase_if(m_found, [&](const Entry &entry) { return entry.file == file; });
  std::error_code error;
  if (!std::filesystem::exists(file, error)) {
    return;
  }
  const std::filesystem::path target = quarantineDirectory() / file.filename();
  std::filesystem::rename(file, target, error);
  if (error) {
    SONNET_LOG_ERROR("recovery: cannot quarantine {}: {}", file.string(), error.message());
    return;
  }
  SONNET_LOG_WARN("recovery: {} is not restored, it was moved to {}", file.filename().string(), m_quarantine.string());
}

core::Result<Recovery::Previous> Recovery::begin() {
  m_found.clear();
  m_written.clear();
  m_restoring = false;
  std::error_code error;
  std::filesystem::create_directories(m_directory, error);
  if (error) {
    return std::unexpected(core::Error{std::format("cannot create {}: {}", m_directory.string(), error.message()),
                                       core::ErrorCategory::Io});
  }
  // A write that was interrupted leaves only its temporary file, which was never the recovery file.
  for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
    if (entry.is_regular_file(error) && hasSuffix(entry.path(), TemporarySuffix)) {
      std::filesystem::remove(entry.path(), error);
    }
  }

  const bool lockLeft = std::filesystem::exists(lockFile(m_directory), error);
  Previous previous = lockLeft ? Previous::Crashed : Previous::Clean;
  if (std::filesystem::exists(markerFile(m_directory), error)) {
    // The previous run died restoring this set: a recovery set is tried once. A marker that cannot
    // be read names nothing, so every file is suspect.
    previous = Previous::RestoreCrashed;
    std::vector<std::filesystem::path> suspects;
    const auto bytes = core::readFile(markerFile(m_directory));
    const json marker = bytes ? assets::parseJson(*bytes) : json{json::value_t::discarded};
    if (marker.is_object() && marker.contains("files") && marker["files"].is_array()) {
      for (const json &name : marker["files"]) {
        if (name.is_string()) {
          suspects.push_back(m_directory / std::filesystem::path{name.get<std::string>()}.filename());
        }
      }
    } else {
      suspects = files();
    }
    for (const std::filesystem::path &file : suspects) {
      quarantine(file);
    }
    std::filesystem::remove(markerFile(m_directory), error);
    SONNET_LOG_WARN("recovery: the last restore did not survive; its scenes are in {}", quarantineDirectory().string());
  } else {
    for (const std::filesystem::path &file : files()) {
      const auto bytes = core::readFile(file);
      const json document = bytes ? assets::parseJson(*bytes) : json{json::value_t::discarded};
      // A scene document with what recovery adds under one key, which the scene loader ignores:
      // a quarantined file opens by hand like any scene.
      if (!document.is_object() || !document.contains("recovery") || !document["recovery"].is_object() ||
          !document.contains("entities") || !document["entities"].is_array()) {
        quarantine(file);
        continue;
      }
      Entry entry;
      entry.file = file;
      entry.scenePath = document["recovery"].value("path", std::string{});
      entry.scene = document;
      entry.scene.erase("recovery");
      m_found.push_back(std::move(entry));
    }
  }

  if (const auto locked = core::writeFile(lockFile(m_directory), m_session); !locked) {
    return std::unexpected(locked.error());
  }
  m_started = true;
  m_decided = m_found.empty();
  m_lastAutosave = std::chrono::steady_clock::now();
  return previous;
}

core::Result<void> Recovery::startRestore() {
  json files = json::array();
  for (const Entry &entry : m_found) {
    files.push_back(entry.file.filename().string());
  }
  const json marker{{"attempt", 1}, {"files", files}};
  if (const auto written = writeAtomic(markerFile(m_directory), marker.dump(2) + '\n'); !written) {
    return written;
  }
  m_restoring = true;
  m_decided = true;
  m_restoreFrames = 0;
  m_restoreStart = std::chrono::steady_clock::now();
  return {};
}

void Recovery::discard() {
  std::error_code error;
  for (const Entry &entry : m_found) {
    std::filesystem::remove(entry.file, error);
  }
  m_found.clear();
  m_decided = true;
}

void Recovery::keep() {
  m_found.clear();
  m_decided = true;
}

bool Recovery::settled(std::chrono::steady_clock::time_point now) {
  if (!m_restoring) {
    return false;
  }
  ++m_restoreFrames;
  return now - m_restoreStart >= SettleTime || m_restoreFrames >= SettleFrames;
}

void Recovery::finishRestore() {
  std::error_code error;
  for (const Entry &entry : m_found) {
    std::filesystem::remove(entry.file, error);
  }
  m_found.clear();
  std::filesystem::remove(markerFile(m_directory), error);
  m_restoring = false;
}

bool Recovery::autosaveDue(std::chrono::steady_clock::time_point now) const noexcept {
  return autosaveAllowed() && now - m_lastAutosave >= AutosaveInterval;
}

core::Result<void> Recovery::write(std::uint64_t tab, const std::filesystem::path &scenePath,
                                   const nlohmann::json &scene) {
  json document = scene;
  document["recovery"] = {{"version", FormatVersion}, {"path", scenePath.generic_string()}};
  const std::filesystem::path file = fileOf(tab);
  if (const auto written = writeAtomic(file, document.dump()); !written) {
    return written;
  }
  m_written[tab] = file;
  return {};
}

void Recovery::remove(std::uint64_t tab) {
  const auto written = m_written.find(tab);
  if (written == m_written.end()) {
    return;
  }
  std::error_code error;
  std::filesystem::remove(written->second, error);
  m_written.erase(written);
}

void Recovery::end() {
  if (!m_started) {
    return;
  }
  // A normal exit in the seconds after a restore is a restore that worked.
  if (m_restoring) {
    finishRestore();
  }
  std::error_code error;
  for (const auto &[tab, file] : m_written) {
    std::filesystem::remove(file, error);
  }
  m_written.clear();
  std::filesystem::remove(lockFile(m_directory), error);
  m_started = false;
}

std::string recoveryKey(const std::filesystem::path &projectRoot) {
  if (projectRoot.empty()) {
    return "no-project";
  }
  std::filesystem::path root = projectRoot.lexically_normal();
  if (!root.has_filename()) {
    root = root.parent_path(); // a trailing separator names the same folder
  }
  std::string name = root.filename().string();
  std::ranges::transform(name, name.begin(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' ? c : '_';
  });
  return std::format("{}-{:016x}", name, fnv1a(root.generic_string()));
}

} // namespace sonnet::editor
