#pragma once

#include "phantomcrash/robot_parts.hpp"

namespace phantomcrash {

// How far a record weapon reaches and locks on, as the original works it
// out from the record: the reach the computer fires within (0x09d42f, the
// weapon's +0x74 and +0x78) and the lock checks (0x4f6a0 for arm weapons,
// 0x5262d and 0x51045 for shoulder ones, 0x51e37 for melee). World units.
struct WeaponReach {
  float range{};        // the farthest it is fired at
  float min_range{};    // and the nearest (lobbed rounds; 0 otherwise)
  float lock_range{};   // a lock is made within this
  float hold_degrees{}; // and held this far off the aim (0: only in the reticle)
};

inline constexpr float arm_lock_range = 75.0F;            // 0x19b55c
inline constexpr float shoulder_lock_range = 125.0F;      // 0x19f8c8
inline constexpr float melee_lock_range = 25.0F;          // 0x51e37, a player's robot
inline constexpr float computer_melee_lock_range = 20.0F; // 0x51e37, the computer's
inline constexpr float lock_hold_degrees = 90.0F;         // 0x19f870
inline constexpr float melee_reach = 15.0F;               // 0x09d42f
inline constexpr float lobbed_reach_margin = 5.0F;        // 0x19b544
inline constexpr float lobbed_reach_fallback = 10.0F;     // 0x09d42f, a round fired downward

// 0x09dd85: how far a straight round flies in its life, starting at
// `start` and speeding up by `acceleration` to `top` (per frame, frames).
[[nodiscard]] float straight_reach(float start, float top, float acceleration, float life);
// 0x09d42f's estimate of a lobbed round's range: the flat-ground range of
// its launch speed and angle, against the world's gravity a battle tick
// while the speed counts a frame, so it comes out about half the round's
// true range.
[[nodiscard]] float lobbed_reach(float speed, float launch_degrees);
// `shoulder`: the weapon is on a shoulder mount.
[[nodiscard]] WeaponReach weapon_reach(const WeaponStats &stats, bool shoulder);

} // namespace phantomcrash
