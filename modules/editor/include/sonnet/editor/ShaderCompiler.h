#pragma once

#include <sonnet/core/Error.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace sonnet::editor {

// Compiles Slang entry-point files at runtime the way `sonnet_add_shaders` does at build time
// (docs/rendering.md, "Shaders"): one SPIR-V 1.6 module per file holding every entry point under
// its own name, scalar block layout, column-major matrices. For the editor's hot reload; the
// player ships only the SPIR-V the build produced.
class ShaderCompiler {
public:
  // `includeDirectories` is where `import sonnet` and the other modules resolve.
  explicit ShaderCompiler(const std::vector<std::filesystem::path> &includeDirectories);
  ~ShaderCompiler();
  ShaderCompiler(const ShaderCompiler &) = delete;
  ShaderCompiler &operator=(const ShaderCompiler &) = delete;

  // The module's SPIR-V, or the compiler's diagnostics with `file:line:column` locations, which
  // the editor's log links to.
  [[nodiscard]] core::Result<std::vector<std::byte>> compile(const std::filesystem::path &file, bool debug);

private:
  struct Session;
  std::unique_ptr<Session> m_session;
};

} // namespace sonnet::editor
