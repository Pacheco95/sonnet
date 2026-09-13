#include <sonnet/core/File.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("readFile returns the bytes of an existing file", "[core][file]") {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "sonnet_core_file_test.bin";
  {
    std::ofstream out{path, std::ios::binary};
    out << "abc";
    out.put('\0');
    out << "z";
  }
  const auto bytes = sonnet::core::readFile(path);
  std::filesystem::remove(path);
  REQUIRE(bytes.has_value());
  REQUIRE(bytes->size() == 5);
  REQUIRE(std::to_integer<char>((*bytes)[0]) == 'a');
  REQUIRE(std::to_integer<char>((*bytes)[3]) == '\0');
  REQUIRE(std::to_integer<char>((*bytes)[4]) == 'z');
}

TEST_CASE("readFile reports a missing file as an Io error", "[core][file]") {
  const auto bytes = sonnet::core::readFile("/nonexistent/sonnet/file.bin");
  REQUIRE(!bytes.has_value());
  REQUIRE(bytes.error().category == sonnet::core::ErrorCategory::Io);
  REQUIRE(bytes.error().message.contains("file.bin"));
}

TEST_CASE("writeFile creates the directories and readFile gets the bytes back", "[core][file]") {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sonnet_core_write_test";
  std::filesystem::remove_all(directory);
  const std::filesystem::path path = directory / "nested" / "file.txt";
  REQUIRE(sonnet::core::writeFile(path, std::string_view{"hello"}).has_value());
  const auto bytes = sonnet::core::readFile(path);
  REQUIRE(bytes.has_value());
  REQUIRE(bytes->size() == 5);
  REQUIRE(std::to_integer<char>((*bytes)[0]) == 'h');
  REQUIRE(sonnet::core::writeFile(path, std::string_view{}).has_value()); // truncates
  REQUIRE(sonnet::core::readFile(path)->empty());
  std::filesystem::remove_all(directory);
  // A regular file can never be created as a directory, on every platform; unlike a path such as
  // "/nonexistent-root-dir/...", which Windows resolves under the current drive's writable root.
  const std::filesystem::path blocker = std::filesystem::temp_directory_path() / "sonnet_core_write_blocker.bin";
  REQUIRE(sonnet::core::writeFile(blocker, std::string_view{"x"}).has_value());
  const auto failed = sonnet::core::writeFile(blocker / "sonnet" / "file.txt", std::string_view{"x"});
  std::filesystem::remove(blocker);
  REQUIRE(!failed.has_value());
  REQUIRE(failed.error().category == sonnet::core::ErrorCategory::Io);
}
