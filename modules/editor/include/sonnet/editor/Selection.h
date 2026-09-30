#pragma once

#include <sonnet/world/World.h>

#include <sonnet/core/Uuid.h>

#include <cstdint>
#include <span>
#include <vector>

namespace sonnet::editor {

// The selected entities by identity, so a selection survives undo, play mode and reloads. The
// last selected is the primary one: what the inspector shows and the gizmo moves.
class Selection {
public:
  enum class Mode {
    Replace,
    Toggle,
    Add,
  };

  void select(core::Uuid uuid, Mode mode = Mode::Replace);
  void deselect(core::Uuid uuid);
  void clear();
  [[nodiscard]] bool contains(core::Uuid uuid) const;
  [[nodiscard]] bool empty() const noexcept {
    return m_items.empty();
  }
  [[nodiscard]] std::span<const core::Uuid> items() const noexcept {
    return m_items;
  }
  [[nodiscard]] core::Uuid primary() const;
  // Drops what no longer exists in the world.
  void prune(const world::World &world);
  // Counts the calls that touched the selection (even one that left it as it was), so a panel can tell that
  // the user selected something since it last looked.
  [[nodiscard]] std::uint64_t revision() const noexcept {
    return m_revision;
  }

private:
  std::vector<core::Uuid> m_items;
  std::uint64_t m_revision = 0;
};

} // namespace sonnet::editor
