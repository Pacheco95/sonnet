#include <sonnet/editor/CommandStack.h>

#include <sonnet/core/Log.h>

namespace sonnet::editor {

void CommandStack::push(std::unique_ptr<ICommand> command, world::World &world) {
  command->apply(world);
  SONNET_LOG_DEBUG("{}", command->description());
  m_undone.clear();
  m_done.push_back(std::move(command));
  if (m_done.size() > Limit) {
    m_done.erase(m_done.begin());
  }
  ++m_revision;
}

bool CommandStack::undo(world::World &world) {
  if (m_done.empty()) {
    return false;
  }
  std::unique_ptr<ICommand> command = std::move(m_done.back());
  m_done.pop_back();
  command->revert(world);
  SONNET_LOG_DEBUG("undo {}", command->description());
  m_undone.push_back(std::move(command));
  ++m_revision;
  return true;
}

bool CommandStack::redo(world::World &world) {
  if (m_undone.empty()) {
    return false;
  }
  std::unique_ptr<ICommand> command = std::move(m_undone.back());
  m_undone.pop_back();
  command->apply(world);
  SONNET_LOG_DEBUG("redo {}", command->description());
  m_done.push_back(std::move(command));
  ++m_revision;
  return true;
}

void CommandStack::clear() {
  m_done.clear();
  m_undone.clear();
  ++m_revision;
}

std::string_view CommandStack::undoDescription() const {
  return m_done.empty() ? std::string_view{} : m_done.back()->description();
}

std::string_view CommandStack::redoDescription() const {
  return m_undone.empty() ? std::string_view{} : m_undone.back()->description();
}

} // namespace sonnet::editor
