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
