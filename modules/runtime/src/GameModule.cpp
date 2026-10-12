#include <sonnet/runtime/GameModule.h>

#include <sonnet/scripting/ScriptRuntime.h>

#include <format>
#include <string>

namespace sonnet::runtime {

void GameRegistry::bindLua(const std::function<void(sol::state_view)> &bind) {
  m_scripts.bindLua(bind);
}

core::Result<void> checkGameModule(std::string_view owner, std::string_view needed, const GameModule *linked,
                                   std::string_view binary) {
  if (needed.empty() || (linked != nullptr && linked->name == needed)) {
    return {};
  }
  const std::string built = linked == nullptr ? std::string{"without one"} : std::format("with \"{}\"", linked->name);
  return std::unexpected(
      core::Error{std::format("{} needs the game module \"{}\"; this {} was built {}", owner, needed, binary, built),
                  core::ErrorCategory::Io});
}

} // namespace sonnet::runtime
