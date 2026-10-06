#pragma once

#include <sonnet/core/Error.h>
#include <sonnet/core/FileLock.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace sonnet::editor {

// The files behind the editor's crash recovery (docs/editor.md, "Crash recovery"), for one project:
// the autosaved scenes, the session lock, the restore marker and the quarantine. It knows nothing
// of tabs or the world; the editor hands it scene JSON and acts on what it finds.
//
// Several editors may have one project open, so everything is per session, and a session is live
// while its lock file is locked (core::FileLock: the OS drops the lock with the process, however it
// ends). A session whose lock file can be locked, or that has none, is stale: that editor died.
//
// The state machine, over one directory:
//   begin()          takes this session's lock, then adopts every stale session by taking its lock
//                    (two editors starting together cannot both adopt one, the loser sees it live).
//                    A live peer's files and marker are never listed, quarantined or deleted. A
//                    stale session's restore marker means that restore died, so the files the marker
//                    names are quarantined and nothing is offered.
//   found()          the scenes of the adopted sessions on offer. Files that do not parse are
//                    quarantined by begin().
//   startRestore()   writes this session's marker. discard() and keep() make the decision otherwise.
//   settled(now)     true once the restored scenes have run 10 seconds or 600 frames.
//   finishRestore()  removes the marker, and with it the recovery set that was restored.
//   end()            a normal exit: removes this session's files and lock.
// Autosave is not allowed (`autosaveAllowed`) until the decision is made and no restore is
// pending, so a crash never overwrites the set being restored.
class Recovery {
public:
  static constexpr std::chrono::seconds AutosaveInterval{30};
  static constexpr std::chrono::seconds SettleTime{10};
  static constexpr int SettleFrames = 600;

  // What the last session left, as begin() found it.
  enum class Previous : std::uint8_t {
    Clean,          // no lock: it ended normally
    Crashed,        // the lock was left behind
    RestoreCrashed, // the restore marker was left behind
  };

  struct Entry {
    std::filesystem::path file;      // the recovery file
    std::filesystem::path scenePath; // the scene's own file, empty when it had none
    nlohmann::json scene;
    std::string session; // the stale session that wrote it
  };

  explicit Recovery(std::filesystem::path directory);
  Recovery(const Recovery &) = delete;
  Recovery &operator=(const Recovery &) = delete;

  // Starts this session and reads what the sessions that died left. Nothing is deleted but quarantined
  // files are moved; an unwritable directory, or a filesystem that cannot lock, is reported by the
  // result and leaves the editor without recovery.
  [[nodiscard]] core::Result<Previous> begin();
  [[nodiscard]] const std::vector<Entry> &found() const noexcept {
    return m_found;
  }
  [[nodiscard]] const std::filesystem::path &directory() const noexcept {
    return m_directory;
  }
  [[nodiscard]] static std::filesystem::path quarantineRoot(const std::filesystem::path &directory) {
    return directory / "quarantine";
  }
  [[nodiscard]] static std::filesystem::path lockFile(const std::filesystem::path &directory,
                                                      const std::string &session) {
    return directory / ("session-" + session + ".lock");
  }
  [[nodiscard]] static std::filesystem::path markerFile(const std::filesystem::path &directory,
                                                        const std::string &session) {
    return directory / ("recovering-" + session + ".json");
  }
  // This session's own.
  [[nodiscard]] std::filesystem::path lockFile() const {
    return lockFile(m_directory, m_session);
  }
  [[nodiscard]] std::filesystem::path markerFile() const {
    return markerFile(m_directory, m_session);
  }
  [[nodiscard]] const std::string &session() const noexcept {
    return m_session;
  }

  // The decision about what begin() found. Restore writes the marker first (and fails without
  // having loaded anything when it cannot); Discard deletes the files; Keep leaves them for the
  // next start. A scene that cannot be loaded is quarantined by `quarantine`.
  [[nodiscard]] core::Result<void> startRestore();
  void discard();
  void keep();
  void quarantine(const std::filesystem::path &file);
  // Counts one frame of the restored scenes running normally; true once they have run long enough.
  [[nodiscard]] bool settled(std::chrono::steady_clock::time_point now);
  // The restored scenes are safe: the marker and the recovery set that was restored are removed.
  void finishRestore();
  [[nodiscard]] bool restoring() const noexcept {
    return m_restoring;
  }
  [[nodiscard]] bool decided() const noexcept {
    return m_decided;
  }
  [[nodiscard]] bool autosaveAllowed() const noexcept {
    return m_started && m_decided && !m_restoring;
  }

  // Autosave. `autosaveDue` is true every AutosaveInterval once allowed; `autosaved` restarts it.
  [[nodiscard]] bool autosaveDue(std::chrono::steady_clock::time_point now) const noexcept;
  void autosaved(std::chrono::steady_clock::time_point now) noexcept {
    m_lastAutosave = now;
  }
  // Writes the scene of tab `tab` through a temporary file and a rename, so a crash mid-write leaves
  // the previous file whole. `scenePath` is the scene's own file, empty when it has none.
  [[nodiscard]] core::Result<void> write(std::uint64_t tab, const std::filesystem::path &scenePath,
                                         const nlohmann::json &scene);
  [[nodiscard]] bool has(std::uint64_t tab) const {
    return m_written.contains(tab);
  }
  void remove(std::uint64_t tab);
  // A normal exit, or the end of this project's session: this session's files and the lock go.
  void end();

  // The recovery file of `tab` in this session.
  [[nodiscard]] std::filesystem::path fileOf(std::uint64_t tab) const;

private:
  // A stale session this one has locked until the decision about its files is final.
  struct Adopted {
    std::string session;
    core::FileLock lock;
  };

  [[nodiscard]] std::filesystem::path quarantineDirectory();
  [[nodiscard]] std::vector<std::filesystem::path> filesOf(const std::string &session) const;
  // Releases the adopted sessions' locks; `remove` deletes their lock files too (a final decision).
  void releaseAdopted(bool remove);

  std::filesystem::path m_directory;
  std::string m_session;
  core::FileLock m_lock;
  std::vector<Adopted> m_adopted;
  std::filesystem::path m_quarantine;
  std::vector<Entry> m_found;
  std::unordered_map<std::uint64_t, std::filesystem::path> m_written;
  std::chrono::steady_clock::time_point m_lastAutosave{};
  std::chrono::steady_clock::time_point m_restoreStart{};
  int m_restoreFrames{0};
  bool m_started{false};
  bool m_decided{true};
  bool m_restoring{false};
};

// The directory under the per-user recovery directory for the project at `root`, or for no project:
// the folder's name for the eyes and a hash of its path to tell two projects of the same name apart.
[[nodiscard]] std::string recoveryKey(const std::filesystem::path &projectRoot);

} // namespace sonnet::editor
