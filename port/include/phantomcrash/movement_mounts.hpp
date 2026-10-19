#pragma once

#include "phantomcrash/skeleton.hpp"

namespace phantomcrash {
inline constexpr std::size_t movement_slots = 14;
struct MovementMount {
  int kind{-1};
  Vec3 position;
  Vec3 direction;
};
struct MovementGroups {
  int jump{-1};
  int descent{-1};
  int dodge{-1};
  int dash_stop{-1};
  int takeoff{-1};
  float takeoff_seconds{};
  std::vector<std::array<int, 3>> contact;
  std::array<int, 2> dash{-1, -1};
};
struct MovementMounts {
  std::vector<std::array<MovementMount, movement_slots>> legs;
  MovementGroups groups;
};
struct MovementPoint {
  Vec3 position;
  Vec3 direction;
};
[[nodiscard]] MovementMounts parse_movement_mounts(const json::JsonValue &value);
} // namespace phantomcrash
