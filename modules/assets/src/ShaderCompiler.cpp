#include <sonnet/assets/ShaderCompiler.h>

#include <sonnet/core/File.h>
#include <sonnet/core/Log.h>
#include <sonnet/core/Profile.h>

#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <format>
#include <regex>
#include <string>

namespace sonnet::assets {

// The global session holds the core module and is slow to create; one per compiler.
struct ShaderCompiler::Session {
  Slang::ComPtr<slang::IGlobalSession> global;
  std::vector<std::string> includeDirectories;
};

namespace {

std::string blobText(slang::IBlob *blob) {
  if (blob == nullptr || blob->getBufferSize() == 0) {
    return {};
  }
  std::string text{static_cast<const char *>(blob->getBufferPointer()), blob->getBufferSize()};
  while (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
    text.pop_back();
  }
  // Slang writes `file(line): message`; `file:line:column` is what the log panel links to
  // (docs/conventions.md, "Logging").
  static const std::regex location{R"(^(.*?)\((\d+)\): )", std::regex::multiline};
  return std::regex_replace(text, location, "$1:$2:1: ");
}

core::Error compileError(const std::filesystem::path &file, std::string_view what, const std::string &diagnostics) {
  return core::Error{diagnostics.empty() ? std::format("{}: {}", file.string(), what) : diagnostics,
                     core::ErrorCategory::Shader};
}

} // namespace

ShaderCompiler::ShaderCompiler(std::vector<std::filesystem::path> includeDirectories)
    : m_session(std::make_unique<Session>()) {
  SONNET_ZONE();
  for (const std::filesystem::path &directory : includeDirectories) {
    m_session->includeDirectories.push_back(directory.string());
  }
  if (SLANG_FAILED(slang::createGlobalSession(m_session->global.writeRef()))) {
    throw core::Exception{"the Slang compiler could not be initialised", core::ErrorCategory::Shader};
  }
  SONNET_LOG_DEBUG("Slang {} ready for runtime compilation", m_session->global->getBuildTagString());
}

ShaderCompiler::~ShaderCompiler() = default;

core::Result<std::vector<std::byte>> ShaderCompiler::compile(const std::filesystem::path &file, bool debug) {
  SONNET_ZONE();
  const auto source = core::readFile(file);
  if (!source) {
    return std::unexpected(source.error());
  }

  // The same options as cmake/SonnetShaders.cmake gives slangc.
  const std::array options{
      slang::CompilerOptionEntry{slang::CompilerOptionName::VulkanUseEntryPointName,
                                 {slang::CompilerOptionValueKind::Int, 1}},
      slang::CompilerOptionEntry{slang::CompilerOptionName::GLSLForceScalarLayout,
                                 {slang::CompilerOptionValueKind::Int, 1}},
      slang::CompilerOptionEntry{slang::CompilerOptionName::MatrixLayoutColumn,
                                 {slang::CompilerOptionValueKind::Int, 1}},
      slang::CompilerOptionEntry{
          slang::CompilerOptionName::Optimization,
          {slang::CompilerOptionValueKind::Int,
           static_cast<int32_t>(debug ? SLANG_OPTIMIZATION_LEVEL_NONE : SLANG_OPTIMIZATION_LEVEL_HIGH)}},
      slang::CompilerOptionEntry{
          slang::CompilerOptionName::DebugInformation,
          {slang::CompilerOptionValueKind::Int,
           static_cast<int32_t>(debug ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE)}},
  };
  slang::TargetDesc target{};
  target.format = SLANG_SPIRV;
  target.profile = m_session->global->findProfile("spirv_1_6");
  target.forceGLSLScalarBufferLayout = true;
  target.compilerOptionEntries = options.data();
  target.compilerOptionEntryCount = static_cast<std::uint32_t>(options.size());

  std::vector<std::string> searchPaths = m_session->includeDirectories;
  searchPaths.push_back(file.parent_path().string());
  std::vector<const char *> searchPathPointers;
  for (const std::string &path : searchPaths) {
    searchPathPointers.push_back(path.c_str());
  }
  slang::SessionDesc desc{};
  desc.targets = &target;
  desc.targetCount = 1;
  desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
  desc.searchPaths = searchPathPointers.data();
  desc.searchPathCount = static_cast<SlangInt>(searchPathPointers.size());

  Slang::ComPtr<slang::ISession> session;
  if (SLANG_FAILED(m_session->global->createSession(desc, session.writeRef()))) {
    return std::unexpected(compileError(file, "the compiler session could not be created", {}));
  }

  const std::string text{reinterpret_cast<const char *>(source->data()), source->size()};
  Slang::ComPtr<slang::IBlob> sourceBlob;
  // A string blob keeps its own copy of the text.
  struct StringBlob final : public ISlangBlob {
    std::string data;
    std::uint32_t references{1};
    explicit StringBlob(std::string text) : data(std::move(text)) {
    }
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(const SlangUUID &uuid, void **out) override {
      if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangBlob::getTypeGuid()) {
        *out = this;
        addRef();
        return SLANG_OK;
      }
      return SLANG_E_NO_INTERFACE;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override {
      return ++references;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
      const std::uint32_t remaining = --references;
      if (remaining == 0) {
        delete this;
      }
      return remaining;
    }
    SLANG_NO_THROW const void *SLANG_MCALL getBufferPointer() override {
      return data.data();
    }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override {
      return data.size();
    }
  };
  sourceBlob.attach(new StringBlob{text});

  const std::string moduleName = file.stem().string();
  const std::string path = file.string();
  Slang::ComPtr<slang::IBlob> diagnostics;
  slang::IModule *module =
      session->loadModuleFromSource(moduleName.c_str(), path.c_str(), sourceBlob, diagnostics.writeRef());
  if (module == nullptr) {
    return std::unexpected(compileError(file, "compilation failed", blobText(diagnostics)));
  }
  if (const std::string warnings = blobText(diagnostics); !warnings.empty()) {
    SONNET_LOG_WARN("{}", warnings);
  }

  std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
  std::vector<slang::IComponentType *> components{module};
  for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); ++i) {
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(module->getDefinedEntryPoint(i, entryPoint.writeRef()))) {
      return std::unexpected(compileError(file, std::format("entry point {} could not be read", i), {}));
    }
    components.push_back(entryPoint);
    entryPoints.push_back(std::move(entryPoint));
  }
  if (entryPoints.empty()) {
    return std::unexpected(compileError(file, "no [shader(...)] entry points", {}));
  }
  Slang::ComPtr<slang::IComponentType> composite;
  diagnostics = nullptr;
  if (SLANG_FAILED(session->createCompositeComponentType(components.data(), static_cast<SlangInt>(components.size()),
                                                         composite.writeRef(), diagnostics.writeRef()))) {
    return std::unexpected(compileError(file, "the entry points could not be combined", blobText(diagnostics)));
  }
  Slang::ComPtr<slang::IComponentType> linked;
  diagnostics = nullptr;
  if (SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef()))) {
    return std::unexpected(compileError(file, "linking failed", blobText(diagnostics)));
  }
  Slang::ComPtr<slang::IBlob> code;
  diagnostics = nullptr;
  if (SLANG_FAILED(linked->getTargetCode(0, code.writeRef(), diagnostics.writeRef()))) {
    return std::unexpected(compileError(file, "code generation failed", blobText(diagnostics)));
  }
  const auto *bytes = static_cast<const std::byte *>(code->getBufferPointer());
  std::vector<std::byte> spirv(bytes, bytes + code->getBufferSize());
  SONNET_LOG_DEBUG("compiled {} ({} entry points, {} bytes)", file.filename().string(), entryPoints.size(),
                   spirv.size());
  return spirv;
}

} // namespace sonnet::assets
