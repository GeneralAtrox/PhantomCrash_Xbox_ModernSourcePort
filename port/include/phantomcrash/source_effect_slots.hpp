#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace phantomcrash {
struct SourceEffectPriority {
  int primary{}, secondary{};
};
struct SourceEffectAllocation {
  std::size_t slot{};
  std::uint64_t evicted{};
};

// Battle's singleton effect table: 64 entries shared by action and impact
// constructors. Age is counted in source updates, except for resource 58.
class SourceEffectSlots final {
public:
  [[nodiscard]] std::optional<SourceEffectAllocation>
  allocate(std::uint64_t handle, int resource,
           std::optional<SourceEffectPriority> priority = std::nullopt, int owner = -1);
  void release(std::uint64_t handle);
  void step();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool contains(std::uint64_t handle) const;
  [[nodiscard]] std::size_t count(int resource, int owner) const;
  [[nodiscard]] static bool protected_resource(int resource);

private:
  struct Entry {
    std::uint64_t handle{};
    int resource{-1};
    std::uint32_t age{};
    std::optional<SourceEffectPriority> priority;
    int owner{-1};
  };
  [[nodiscard]] std::optional<std::size_t> replacement(int resource) const;
  std::array<Entry, 64> entries_{};
};
} // namespace phantomcrash
