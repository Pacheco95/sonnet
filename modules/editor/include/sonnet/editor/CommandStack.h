#pragma once

#include <sonnet/world/World.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace sonnet::editor {

// One reversible edit of the world. Commands refer to entities by identity, never by flecs id,
// so undoing a delete and redoing it keep working across the recreated entities.
class ICommand {
public:
  virtual ~ICommand() = default;

  virtual void apply(world::World &world) = 0;
  virtual void revert(world::World &world) = 0;
  [[nodiscard]] virtual std::string_view description() const = 0;
};

// The undo history (docs/editor.md). push applies the command and drops the redo list.
class CommandStack {
public:
  static constexpr std::size_t Limit = 256;

  void push(std::unique_ptr<ICommand> command, world::World &world);
  bool undo(world::World &world);
  bool redo(world::World &world);
  void clear();

  [[nodiscard]] bool canUndo() const noexcept {
    return !m_done.empty();
  }
  [[nodiscard]] bool canRedo() const noexcept {
    return !m_undone.empty();
  }
  [[nodiscard]] std::string_view undoDescription() const;
  [[nodiscard]] std::string_view redoDescription() const;
  [[nodiscard]] std::size_t size() const noexcept {
    return m_done.size();
  }
  // Moves on every push, undo and redo; comparing it with the value at the last save tells
  // whether the scene is dirty.
  [[nodiscard]] std::uint64_t revision() const noexcept {
    return m_revision;
  }

private:
  std::vector<std::unique_ptr<ICommand>> m_done;
  std::vector<std::unique_ptr<ICommand>> m_undone;
  std::uint64_t m_revision{0};
};

} // namespace sonnet::editor
