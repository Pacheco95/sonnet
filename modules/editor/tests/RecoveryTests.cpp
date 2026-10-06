#include <sonnet/editor/Recovery.h>

#include <sonnet/core/File.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>

using namespace sonnet;
using namespace std::chrono_literals;
using Previous = editor::Recovery::Previous;

namespace {

std::filesystem::path scratch(const char *name) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_editor_tests" / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::size_t countFiles(const std::filesystem::path &directory, std::string_view suffix = ".recovery.scene.json") {
  std::size_t count = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    count += (entry.is_regular_file() && entry.path().filename().string().ends_with(suffix)) ? 1u : 0u;
  }
  return count;
}

std::size_t markers(const std::filesystem::path &directory) {
  std::size_t count = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    count += entry.path().filename().string().starts_with("recovering-") ? 1u : 0u;
  }
  return count;
}

std::size_t quarantined(const std::filesystem::path &directory) {
  std::size_t count = 0;
  const std::filesystem::path root = editor::Recovery::quarantineRoot(directory);
  if (!std::filesystem::exists(root)) {
    return 0;
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    count += entry.is_regular_file() ? 1u : 0u;
  }
  return count;
}

const nlohmann::json Scene{{"version", 2}, {"entities", nlohmann::json::array()}, {"marker", "recovered"}};

} // namespace

TEST_CASE("autosave is due every thirty seconds once the restore decision is made", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_interval");
  editor::Recovery recovery{directory};
  const auto start = std::chrono::steady_clock::now();
  REQUIRE(!recovery.autosaveAllowed()); // not begun
  REQUIRE(recovery.begin().value() == Previous::Clean);
  REQUIRE(recovery.autosaveAllowed());
  REQUIRE(!recovery.autosaveDue(start + 29s));
  REQUIRE(recovery.autosaveDue(start + 31s));
  recovery.autosaved(start + 31s);
  REQUIRE(!recovery.autosaveDue(start + 60s));
  REQUIRE(recovery.autosaveDue(start + 62s));
  recovery.end();
}

TEST_CASE("a recovery file is written whole or not at all", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_atomic");
  {
    editor::Recovery recovery{directory};
    REQUIRE(recovery.begin().has_value());
    REQUIRE(recovery.write(1, "/scenes/a.scene.json", Scene).has_value());
    REQUIRE(countFiles(directory, ".tmp") == 0); // the temporary file was renamed away
    REQUIRE(countFiles(directory) == 1);
    // Rewriting replaces the file in place.
    REQUIRE(recovery.write(1, "/scenes/a.scene.json", Scene).has_value());
    REQUIRE(countFiles(directory) == 1);
    // A crash mid-write left a torn temporary file: the previous recovery file is untouched, the
    // next session throws the remains away and offers the whole one.
    std::ofstream{recovery.fileOf(1).string() + ".tmp"} << "{\"scene\": {\"vers";
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(countFiles(directory, ".tmp") == 0);
  REQUIRE(next.found().size() == 1);
  REQUIRE(next.found()[0].scene == Scene);
  REQUIRE(next.found()[0].scenePath == std::filesystem::path{"/scenes/a.scene.json"});
}

TEST_CASE("a clean exit leaves nothing to recover", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_clean");
  {
    editor::Recovery recovery{directory};
    REQUIRE(recovery.begin().value() == Previous::Clean);
    REQUIRE(std::filesystem::exists(recovery.lockFile()));
    REQUIRE(recovery.write(1, {}, Scene).has_value());
    REQUIRE(recovery.write(2, {}, Scene).has_value());
    recovery.remove(2); // a saved or closed tab
    REQUIRE(countFiles(directory) == 1);
    recovery.end();
  }
  REQUIRE(countFiles(directory) == 0);
  REQUIRE(countFiles(directory, ".lock") == 0);
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Clean);
  REQUIRE(next.found().empty());
  REQUIRE(next.decided());
}

TEST_CASE("a crash leaves the scenes on offer, and autosave waits for the answer", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_crash");
  {
    editor::Recovery recovery{directory};
    REQUIRE(recovery.begin().has_value());
    REQUIRE(recovery.write(1, {}, Scene).has_value());
    // No end(): the process died.
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(next.found().size() == 1);
  REQUIRE(next.found()[0].scene == Scene);
  REQUIRE(!next.decided());
  REQUIRE(!next.autosaveAllowed());
  REQUIRE(!next.autosaveDue(std::chrono::steady_clock::now() + 1h));
  const std::size_t before = countFiles(directory);

  SECTION("keeping leaves the files for the next start") {
    next.keep();
    REQUIRE(next.autosaveAllowed());
    REQUIRE(countFiles(directory) == before);
    next.end();
    editor::Recovery later{directory};
    // The kept session's lock file stayed with its files, so they are a crash's leftovers still.
    REQUIRE(later.begin().value() == Previous::Crashed);
    REQUIRE(later.found().size() == 1); // the kept scene is still offered
  }
  SECTION("discarding deletes them") {
    next.discard();
    REQUIRE(next.autosaveAllowed());
    REQUIRE(countFiles(directory) == 0);
  }
  next.end();
}

TEST_CASE("a restore that crashes is not offered again", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_loop");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
  }
  {
    editor::Recovery restoring{directory};
    REQUIRE(restoring.begin().value() == Previous::Crashed);
    REQUIRE(restoring.startRestore().has_value());
    REQUIRE(std::filesystem::exists(restoring.markerFile()));
    REQUIRE(restoring.restoring());
    REQUIRE(!restoring.autosaveAllowed()); // the set being restored is not overwritten
    // The editor died here, loading or in its first seconds.
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::RestoreCrashed);
  REQUIRE(next.found().empty());
  REQUIRE(next.decided());
  REQUIRE(countFiles(directory) == 0);
  REQUIRE(quarantined(directory) == 1);
  REQUIRE(markers(directory) == 0);
  next.end();

  // Tried at most once: the start after that is an ordinary one.
  editor::Recovery after{directory};
  REQUIRE(after.begin().value() == Previous::Clean);
  REQUIRE(after.found().empty());
  after.end();
  REQUIRE(quarantined(directory) == 1);
}

TEST_CASE("a restore that survives its first seconds clears the marker and the set", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_settle");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
  }
  editor::Recovery recovery{directory};
  REQUIRE(recovery.begin().value() == Previous::Crashed);
  REQUIRE(recovery.startRestore().has_value());
  const auto now = std::chrono::steady_clock::now();

  SECTION("after ten seconds") {
    REQUIRE(!recovery.settled(now));
    REQUIRE(recovery.settled(now + 11s));
  }
  SECTION("after six hundred frames") {
    for (int frame = 1; frame < editor::Recovery::SettleFrames; ++frame) {
      REQUIRE(!recovery.settled(now));
    }
    REQUIRE(recovery.settled(now));
  }
  recovery.finishRestore();
  REQUIRE(markers(directory) == 0);
  REQUIRE(countFiles(directory) == 0);
  REQUIRE(!recovery.restoring());
  REQUIRE(recovery.autosaveAllowed());
  recovery.end();
}

TEST_CASE("a file that does not parse is quarantined and the others are still offered", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_corrupt");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
    REQUIRE(crashed.write(2, {}, Scene).has_value());
    // A truncated file, and one that is valid JSON but not a recovery file.
    std::ofstream{directory / "torn.recovery.scene.json"} << "{\"version\": 2, \"entities\": [{\"nam";
    std::ofstream{directory / "other.recovery.scene.json"} << "[1, 2, 3]";
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(next.found().size() == 2);
  REQUIRE(quarantined(directory) == 2);
  REQUIRE(!std::filesystem::exists(directory / "torn.recovery.scene.json"));

  // A scene the editor cannot load is quarantined the same way, and leaves the other on offer.
  const std::filesystem::path bad = next.found()[0].file;
  next.quarantine(bad);
  REQUIRE(next.found().size() == 1);
  REQUIRE(quarantined(directory) == 3);

  // Nothing the session writes goes into the quarantine, and it is never offered.
  next.keep();
  REQUIRE(next.write(7, {}, Scene).has_value());
  REQUIRE(quarantined(directory) == 3);
  next.end();
  editor::Recovery later{directory};
  REQUIRE(later.begin().has_value());
  REQUIRE(later.found().size() == 1);
  later.end();
}

TEST_CASE("projects of the same name keep their recovery files apart", "[editor][recovery]") {
  REQUIRE(editor::recoveryKey("/home/a/game") != editor::recoveryKey("/home/b/game"));
  REQUIRE(editor::recoveryKey("/home/a/game") == editor::recoveryKey("/home/a/game/"));
  REQUIRE(editor::recoveryKey({}) == "no-project");
  REQUIRE(editor::recoveryKey("/home/a/my game").find(' ') == std::string::npos);
}

TEST_CASE("a second editor on a running project is offered nothing and leaves the first alone", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_two");
  auto first = std::make_unique<editor::Recovery>(directory);
  REQUIRE(first->begin().value() == Previous::Clean);
  REQUIRE(first->write(1, {}, Scene).has_value());
  REQUIRE(first->write(2, "/scenes/b.scene.json", Scene).has_value());
  {
    editor::Recovery second{directory};
    REQUIRE(second.begin().value() == Previous::Clean); // the first one's lock is not a crash
    REQUIRE(second.found().empty());
    REQUIRE(second.decided());
    REQUIRE(second.autosaveAllowed());
    REQUIRE(second.write(1, {}, Scene).has_value());
    REQUIRE(countFiles(directory) == 3);
    // Its answer and its normal exit do not reach the first's files or lock.
    second.discard();
    second.end();
  }
  REQUIRE(countFiles(directory) == 2);
  REQUIRE(countFiles(directory, ".lock") == 1);

  // The first one dying afterwards is still found, though the other has exited normally since.
  first.reset(); // kill -9: the lock goes with the process, the files stay
  REQUIRE(countFiles(directory) == 2);
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(next.found().size() == 2);
  next.end();
}

TEST_CASE("a crash is still found after another editor ran and exited normally", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_crash_then_peer");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
  }
  {
    // Another editor opens the project and keeps the offer unanswered for the moment: it adopted the
    // crashed session, so it is the one that decides, and answering Open without restoring leaves it.
    editor::Recovery peer{directory};
    REQUIRE(peer.begin().value() == Previous::Crashed);
    REQUIRE(peer.found().size() == 1);
    peer.keep();
    peer.end();
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(next.found().size() == 1);
  next.end();
}

TEST_CASE("one restore's marker does not quarantine another editor's files", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_marker_peer");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
  }
  editor::Recovery restoring{directory};
  REQUIRE(restoring.begin().value() == Previous::Crashed);
  REQUIRE(restoring.startRestore().has_value());
  REQUIRE(markers(directory) == 1);

  {
    // A second editor starts while the first is restoring (its own scene open, say).
    editor::Recovery peer{directory};
    REQUIRE(peer.begin().value() == Previous::Clean); // the marker is a live editor's: ignored
    REQUIRE(peer.found().empty());
    REQUIRE(quarantined(directory) == 0);
    REQUIRE(markers(directory) == 1);
    REQUIRE(countFiles(directory) >= 1);
    peer.end();
  }
  REQUIRE(markers(directory) == 1);
  REQUIRE(quarantined(directory) == 0);

  restoring.finishRestore();
  REQUIRE(markers(directory) == 0);
  restoring.end();
}

TEST_CASE("two editors starting for one crashed session are not both offered it", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_race");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
  }
  editor::Recovery winner{directory};
  editor::Recovery loser{directory};
  REQUIRE(winner.begin().value() == Previous::Crashed);
  REQUIRE(winner.found().size() == 1);
  // The winner holds the crashed session's lock until it has answered, so the loser sees it live.
  REQUIRE(loser.begin().value() == Previous::Clean);
  REQUIRE(loser.found().empty());
  REQUIRE(countFiles(directory) == 1);

  SECTION("an answer that leaves the files hands them to the next start") {
    winner.keep();
    editor::Recovery later{directory};
    REQUIRE(later.begin().value() == Previous::Crashed);
    REQUIRE(later.found().size() == 1);
  }
  SECTION("discarding deletes them for good") {
    winner.discard();
    REQUIRE(countFiles(directory) == 0);
    editor::Recovery later{directory};
    REQUIRE(later.begin().value() == Previous::Clean);
    REQUIRE(later.found().empty());
  }
}

TEST_CASE("files whose session left no lock file are stale", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_no_lock");
  {
    editor::Recovery crashed{directory};
    REQUIRE(crashed.begin().has_value());
    REQUIRE(crashed.write(1, {}, Scene).has_value());
    // The lock file removed by hand, or a Discard that could not delete the files it was asked to.
    std::filesystem::remove(crashed.lockFile());
  }
  editor::Recovery next{directory};
  REQUIRE(next.begin().value() == Previous::Crashed);
  REQUIRE(next.found().size() == 1);
  next.discard();
  REQUIRE(countFiles(directory) == 0);
  next.end();
  REQUIRE(countFiles(directory, ".lock") == 0);
}

TEST_CASE("two sessions never share a name, even when started together", "[editor][recovery]") {
  const std::filesystem::path directory = scratch("recovery_names");
  std::set<std::string> names;
  for (int i = 0; i < 1000; ++i) {
    names.insert(editor::Recovery{directory}.session());
  }
  REQUIRE(names.size() == 1000);
}
