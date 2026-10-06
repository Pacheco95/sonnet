#include <sonnet/editor/Recovery.h>

#include <sonnet/assets/Json.h>
#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <iterator>
#include <mutex>
#include <random>
#include <set>
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

// Random, so two editors started in the same millisecond cannot share a session (and a lock file).
std::string newSession() {
  static std::mutex s_mutex;
  static std::mt19937_64 s_random{std::random_device{}()};
  const std::lock_guard guard{s_mutex};
  return std::format("{:016x}", s_random());
}

// The session a file or marker belongs to, or empty for a name that belongs to none. A recovery file
// is <session>-<tab>.recovery.scene.json, with its temporary file's suffix on top while it is written.
std::string sessionOf(const std::string &name) {
  const auto between = [&](std::string_view prefix, std::string_view suffix) -> std::string {
    if (name.size() > prefix.size() + suffix.size() && name.starts_with(prefix) && name.ends_with(suffix)) {
      return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    }
    return {};
  };
  if (std::string session = between("session-", ".lock"); !session.empty()) {
    return session;
  }
  if (std::string session = between("recovering-", ".json"); !session.empty()) {
    return session;
  }
  std::string_view recovery = name;
  if (recovery.ends_with(TemporarySuffix)) {
    recovery.remove_suffix(TemporarySuffix.size());
  }
  if (recovery.size() > FileSuffix.size() && recovery.ends_with(FileSuffix)) {
    const std::string_view stem = recovery.substr(0, recovery.size() - FileSuffix.size());
    return std::string{stem.substr(0, stem.find('-'))};
  }
  return {};
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

std::vector<std::filesystem::path> Recovery::filesOf(const std::string &session) const {
  std::vector<std::filesystem::path> found;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
    if (entry.is_regular_file(error) && hasSuffix(entry.path(), FileSuffix) &&
        sessionOf(entry.path().filename().string()) == session) {
      found.push_back(entry.path());
    }
  }
  std::ranges::sort(found);
  return found;
}

void Recovery::releaseAdopted(bool remove) {
  std::error_code error;
  for (Adopted &adopted : m_adopted) {
    adopted.lock.release(); // before the delete: Windows does not delete what it has open
    if (remove) {
      std::filesystem::remove(lockFile(m_directory, adopted.session), error);
    }
  }
  m_adopted.clear();
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
  m_adopted.clear();
  m_lock.release();
  m_restoring = false;
  std::error_code error;
  std::filesystem::create_directories(m_directory, error);
  if (error) {
    return std::unexpected(core::Error{std::format("cannot create {}: {}", m_directory.string(), error.message()),
                                       core::ErrorCategory::Io});
  }

  // This session first, so an editor starting together with this one finds it live. A starting editor
  // can be adopted as stale between creating its lock file and locking it, and its file deleted
  // as an empty session; then it takes another name.
  constexpr int Attempts = 8;
  for (int attempt = 0; attempt < Attempts && !m_lock.held(); ++attempt) {
    if (attempt > 0) {
      m_session = newSession();
    }
    auto locked = core::FileLock::tryLock(lockFile());
    if (!locked) {
      return std::unexpected(locked.error());
    }
    if (*locked && std::filesystem::exists(lockFile(), error)) {
      m_lock = std::move(**locked);
    }
  }
  if (!m_lock.held()) {
    return std::unexpected(
        core::Error{std::format("cannot lock a session in {}", m_directory.string()), core::ErrorCategory::Io});
  }

  // The sessions on disk besides this one, and which of them died: the ones whose lock file can be
  // locked, or that have none. A lock that cannot be tried is a live one, so nothing of it is touched.
  std::set<std::string> sessions;
  for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
    if (entry.is_regular_file(error)) {
      if (std::string session = sessionOf(entry.path().filename().string()); !session.empty()) {
        sessions.insert(std::move(session));
      }
    }
  }
  sessions.erase(m_session);
  for (const std::string &session : sessions) {
    auto taken = core::FileLock::tryLock(lockFile(m_directory, session));
    if (taken && *taken) {
      m_adopted.push_back({session, std::move(**taken)});
    }
  }
  // A write that was interrupted leaves only its temporary file, which was never the recovery file.
  for (const Adopted &adopted : m_adopted) {
    for (const auto &entry : std::filesystem::directory_iterator(m_directory, error)) {
      if (entry.is_regular_file(error) && hasSuffix(entry.path(), TemporarySuffix) &&
          sessionOf(entry.path().filename().string()) == adopted.session) {
        std::filesystem::remove(entry.path(), error);
      }
    }
  }

  Previous previous = m_adopted.empty() ? Previous::Clean : Previous::Crashed;
  for (const Adopted &adopted : m_adopted) {
    if (!std::filesystem::exists(markerFile(m_directory, adopted.session), error)) {
      continue;
    }
    // That session died restoring a set: a recovery set is tried once. A marker that cannot be read
    // names nothing, so every file of the sessions that were adopted is suspect. Only adopted files
    // are touched: a live editor's are not ours.
    previous = Previous::RestoreCrashed;
    std::vector<std::filesystem::path> suspects;
    const auto bytes = core::readFile(markerFile(m_directory, adopted.session));
    const json marker = bytes ? assets::parseJson(*bytes) : json{json::value_t::discarded};
    if (marker.is_object() && marker.contains("files") && marker["files"].is_array()) {
      for (const json &name : marker["files"]) {
        const std::filesystem::path file =
            name.is_string() ? std::filesystem::path{name.get<std::string>()}.filename() : std::filesystem::path{};
        const std::string owner = sessionOf(file.filename().string());
        if (!file.empty() && std::ranges::any_of(m_adopted, [&](const Adopted &a) { return a.session == owner; })) {
          suspects.push_back(m_directory / file);
        }
      }
    } else {
      for (const Adopted &other : m_adopted) {
        std::ranges::copy(filesOf(other.session), std::back_inserter(suspects));
      }
    }
    for (const std::filesystem::path &file : suspects) {
      quarantine(file);
    }
    std::filesystem::remove(markerFile(m_directory, adopted.session), error);
    SONNET_LOG_WARN("recovery: the last restore did not survive; its scenes are in {}", quarantineDirectory().string());
  }

  if (previous == Previous::RestoreCrashed) {
    // Nothing is offered; what is left of a session stays for the next start, its lock file
    // too, and a session with nothing left goes.
    for (Adopted &adopted : m_adopted) {
      adopted.lock.release();
      if (filesOf(adopted.session).empty()) {
        std::filesystem::remove(lockFile(m_directory, adopted.session), error);
      }
    }
    m_adopted.clear();
  } else {
    for (Adopted &adopted : m_adopted) {
      for (const std::filesystem::path &file : filesOf(adopted.session)) {
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
        entry.session = adopted.session;
        m_found.push_back(std::move(entry));
      }
    }
    // A session that offers nothing (it died before it wrote, or all its files were quarantined) has
    // nothing left to decide.
    std::erase_if(m_adopted, [&](Adopted &adopted) {
      if (std::ranges::any_of(m_found, [&](const Entry &entry) { return entry.session == adopted.session; })) {
        return false;
      }
      adopted.lock.release();
      std::filesystem::remove(lockFile(m_directory, adopted.session), error);
      return true;
    });
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
  if (const auto written = writeAtomic(markerFile(), marker.dump(2) + '\n'); !written) {
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
  releaseAdopted(true);
  m_decided = true;
}

void Recovery::keep() {
  m_found.clear();
  releaseAdopted(false); // the files and their lock files stay, so the next start offers them again
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
  releaseAdopted(true);
  std::filesystem::remove(markerFile(), error);
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
  m_found.clear();
  releaseAdopted(false); // an answer never given leaves what was offered for the next start
  m_lock.release();
  std::filesystem::remove(lockFile(), error);
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
