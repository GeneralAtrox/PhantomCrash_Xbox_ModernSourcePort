#include "phantomcrash/source_effect_slots.hpp"
#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
bool SourceEffectSlots::protected_resource(const int resource) {
  // 0x509240: 26/27, 31/32/33 and 58/59 cannot be selected for recycling.
  return resource == 26 || resource == 27 || (resource >= 31 && resource <= 33) || resource == 58 ||
         resource == 59;
}

std::optional<std::size_t> SourceEffectSlots::replacement(const int resource) const {
  std::size_t candidate = 0;
  bool selected = false;
  std::optional<std::size_t> without_priority;
  // 0x509290 starts its replacement scan at entry 1, with entry 0 as the
  // priority comparison baseline. Free-slot search includes entry 0.
  for (std::size_t i = 1; i < entries_.size(); ++i) {
    const auto &entry = entries_[i];
    if (!entry.handle || protected_resource(entry.resource)) {
      continue;
    }
    if (!entry.priority) {
      if (!without_priority || entries_[*without_priority].age < entry.age) {
        without_priority = i;
      }
      continue;
    }
    if (!entry.priority->primary) {
      continue;
    }
    const auto baseline = entries_[candidate].priority.value_or(SourceEffectPriority{});
    const auto incoming = *entry.priority;
    if (incoming.primary > baseline.primary ||
        (incoming.primary == baseline.primary &&
         (incoming.secondary > baseline.secondary ||
          (incoming.secondary == baseline.secondary && entries_[candidate].age < entry.age)))) {
      candidate = i;
      selected = true;
    }
  }
  if (selected) {
    if (!protected_resource(entries_[candidate].resource)) {
      return candidate;
    }
    return protected_resource(resource) ? without_priority : std::nullopt;
  }
  return protected_resource(resource) ? without_priority : std::nullopt;
}

std::optional<SourceEffectAllocation>
SourceEffectSlots::allocate(const std::uint64_t handle, const int resource,
                            const std::optional<SourceEffectPriority> priority, const int owner) {
  if (!handle || contains(handle)) {
    throw std::runtime_error("Invalid or duplicate source effect handle");
  }
  const auto free = std::ranges::find_if(entries_, [](const auto &entry) { return !entry.handle; });
  const auto slot =
      free != entries_.end()
          ? std::optional<std::size_t>(static_cast<std::size_t>(free - entries_.begin()))
          : replacement(resource);
  if (!slot) {
    return std::nullopt;
  }
  const SourceEffectAllocation result{*slot, entries_[*slot].handle};
  entries_[*slot] = {handle, resource, 0, priority, owner};
  return result;
}
void SourceEffectSlots::release(const std::uint64_t handle) {
  for (auto &entry : entries_) {
    if (entry.handle == handle) {
      entry = {};
    }
  }
}
void SourceEffectSlots::step() {
  for (auto &entry : entries_) {
    if (entry.handle && entry.resource != 58) {
      ++entry.age; // 0x5080b8..dc
    }
  }
}
std::size_t SourceEffectSlots::size() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(entries_, [](const auto &entry) { return entry.handle != 0; }));
}
bool SourceEffectSlots::contains(const std::uint64_t handle) const {
  return handle && std::ranges::any_of(
                       entries_, [handle](const auto &entry) { return entry.handle == handle; });
}
std::size_t SourceEffectSlots::count(const int resource, const int owner) const {
  // 0x5094c0 counts active matching resources; negative entry owners are wildcards.
  return static_cast<std::size_t>(std::ranges::count_if(entries_, [&](const auto &entry) {
    return entry.handle && entry.resource == resource && (entry.owner < 0 || entry.owner == owner);
  }));
}
} // namespace phantomcrash
