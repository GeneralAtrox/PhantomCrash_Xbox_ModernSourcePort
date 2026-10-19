#pragma once

namespace phantomcrash {
// Host preferences, snapshotted when a shared arena is created.
struct CoopOptions {
  int enemy_level_offset{};
  int kill_ny_percent{100};
  bool share_shootdowns{true};
  bool share_cash{true};
  bool share_boss_clears{true};
  bool operator==(const CoopOptions &) const = default;
};
} // namespace phantomcrash
