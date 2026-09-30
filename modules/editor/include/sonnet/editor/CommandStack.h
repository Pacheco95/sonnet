#pragma once

#include <sonnet/world/World.h>

#include <cstddef>
#include <memory>
#include <optional>
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

// The undo history (docs/editor.md). push applies the command and drops the redo list. It also
// tracks where the last save was, which is what makes the scene dirty.
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
  // Remembers the current position in the history as the saved state. The scene is clean exactly
  // when the history is back at that position, so undo and redo across it flip the state.
  void markSaved() noexcept {
    m_saved = m_done.size();
  }
  // Forgets the saved position: the scene counts as unsaved until the next markSaved.
  void markUnsaved() noexcept {
    m_saved.reset();
  }
  // False once the saved position is unreachable: a push discarded the redo branch it was in, or
  // the history limit trimmed the command it followed.
  [[nodiscard]] bool isSaved() const noexcept {
    return m_saved == m_done.size();
  }

private:
  std::vector<std::unique_ptr<ICommand>> m_done;
  std::vector<std::unique_ptr<ICommand>> m_undone;
  std::optional<std::size_t> m_saved{0}; // commands done at the last save; a new history is clean
};

} // namespace sonnet::editor
