#pragma once

#include <sonnet/core/Assert.h>
#include <sonnet/core/Handle.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace sonnet::core {

// Slot map that hands out generation-checked handles. Slots are reused in LIFO order and every
// reuse bumps the generation, so a handle to a released object never resolves to its successor.
// Storage is a vector of optionals: T does not need to be default-constructible.
template <typename T, typename Tag> class HandlePool {
public:
  using HandleType = Handle<Tag>;

  template <typename... Args> [[nodiscard]] HandleType emplace(Args &&...args) {
    std::uint32_t index;
    if (m_free.empty()) {
      SONNET_ASSERT(m_slots.size() < HandleType::InvalidIndex, "handle pool exhausted");
      index = static_cast<std::uint32_t>(m_slots.size());
      m_slots.push_back(Slot{});
    } else {
      index = m_free.back();
      m_free.pop_back();
    }
    Slot &slot = m_slots[index];
    slot.value.emplace(std::forward<Args>(args)...);
    ++m_liveCount;
    return HandleType{index, slot.generation};
  }

  // Returns the removed object so the caller can order its destruction, e.g. after the GPU is idle.
  std::optional<T> remove(HandleType handle) {
    if (!contains(handle)) {
      return std::nullopt;
    }
    Slot &slot = m_slots[handle.index];
    std::optional<T> value = std::move(slot.value);
    slot.value.reset();
    ++slot.generation;
    m_free.push_back(handle.index);
    --m_liveCount;
    return value;
  }

  [[nodiscard]] bool contains(HandleType handle) const noexcept {
    return handle.index < m_slots.size() && m_slots[handle.index].value.has_value() &&
           m_slots[handle.index].generation == handle.generation;
  }

  [[nodiscard]] T *find(HandleType handle) noexcept {
    return contains(handle) ? &*m_slots[handle.index].value : nullptr;
  }
  [[nodiscard]] const T *find(HandleType handle) const noexcept {
    return contains(handle) ? &*m_slots[handle.index].value : nullptr;
  }

  // Resolving a stale or invalid handle is a programmer error; use find() for handles that may be stale.
  [[nodiscard]] T &get(HandleType handle) noexcept {
    SONNET_ASSERT(contains(handle), "stale or invalid handle {}:{}", handle.index, handle.generation);
    return *m_slots[handle.index].value;
  }
  [[nodiscard]] const T &get(HandleType handle) const noexcept {
    SONNET_ASSERT(contains(handle), "stale or invalid handle {}:{}", handle.index, handle.generation);
    return *m_slots[handle.index].value;
  }

  template <typename Fn> void forEach(Fn &&fn) {
    for (std::uint32_t i = 0; i < m_slots.size(); ++i) {
      if (m_slots[i].value) {
        fn(HandleType{i, m_slots[i].generation}, *m_slots[i].value);
      }
    }
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return m_liveCount;
  }
  [[nodiscard]] bool empty() const noexcept {
    return m_liveCount == 0;
  }

  void clear() {
    m_slots.clear();
    m_free.clear();
    m_liveCount = 0;
  }

private:
  struct Slot {
    std::optional<T> value;
    std::uint32_t generation{1};
  };

  std::vector<Slot> m_slots;
  std::vector<std::uint32_t> m_free;
  std::size_t m_liveCount{0};
};

} // namespace sonnet::core
