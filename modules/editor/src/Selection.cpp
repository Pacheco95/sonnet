#include <sonnet/editor/Selection.h>

#include <algorithm>

namespace sonnet::editor {

void Selection::select(core::Uuid uuid, Mode mode) {
  if (uuid.isNil()) {
    if (mode == Mode::Replace) {
      clear();
    }
    return;
  }
  switch (mode) {
  case Mode::Replace:
    m_items.assign(1, uuid);
    break;
  case Mode::Toggle:
    if (contains(uuid)) {
      deselect(uuid);
    } else {
      m_items.push_back(uuid);
    }
    break;
  case Mode::Add:
    deselect(uuid); // moves it to the end, making it primary
    m_items.push_back(uuid);
    break;
  }
}

void Selection::deselect(core::Uuid uuid) {
  std::erase(m_items, uuid);
}

void Selection::clear() {
  m_items.clear();
}

bool Selection::contains(core::Uuid uuid) const {
  return std::ranges::find(m_items, uuid) != m_items.end();
}

core::Uuid Selection::primary() const {
  return m_items.empty() ? core::Uuid{} : m_items.back();
}

void Selection::prune(const world::World &world) {
  std::erase_if(m_items, [&](const core::Uuid &uuid) { return !world.find(uuid).is_valid(); });
}

} // namespace sonnet::editor
