#include "AssetTestSupport.h"

#include <sonnet/assets/ShaderCompiler.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace sonnet;
using namespace sonnet::assets;

namespace {

bool isSpirv(const std::vector<std::byte> &bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  std::uint32_t magic = 0;
  std::memcpy(&magic, bytes.data(), sizeof(magic));
  return magic == 0x07230203u;
}

} // namespace

TEST_CASE("the runtime compiler builds the engine shaders from their sources", "[assets][shader]") {
  const std::filesystem::path engineShaders{SONNET_ENGINE_SHADER_DIR};
  ShaderCompiler compiler{{engineShaders}};
  const auto forward = compiler.compile(engineShaders / "forward.slang", true);
  REQUIRE(forward.has_value());
  REQUIRE(isSpirv(*forward));
  // A file with several entry points and a compute file compile alike.
  REQUIRE(compiler.compile(engineShaders / "post.slang", false).has_value());
  REQUIRE(compiler.compile(engineShaders / "cluster.slang", false).has_value());
}

TEST_CASE("a broken shader reports its file, line and column", "[assets][shader]") {
  const std::filesystem::path directory = test::freshDirectory("sonnet_assets_shader");
  const std::filesystem::path file = directory / "broken.slang";
  REQUIRE(core::writeFile(file, std::string_view{"import sonnet;\n\n[shader(\"vertex\")]\nfloat4 vertexMain() : "
                                                 "SV_Position {\n  return undefinedThing;\n}\n"})
              .has_value());
  ShaderCompiler compiler{{std::filesystem::path{SONNET_ENGINE_SHADER_DIR}}};
  const auto result = compiler.compile(file, true);
  REQUIRE(!result.has_value());
  REQUIRE(result.error().category == core::ErrorCategory::Shader);
  REQUIRE(result.error().message.contains("broken.slang:5:"));
  REQUIRE(result.error().message.contains("undefinedThing"));
  // A file without entry points is an error too, and a missing file an Io error.
  REQUIRE(core::writeFile(file, std::string_view{"module broken;\n"}).has_value());
  REQUIRE(!compiler.compile(file, true).has_value());
  REQUIRE(compiler.compile(directory / "missing.slang", true).error().category == core::ErrorCategory::Io);
  std::filesystem::remove_all(directory);
}
