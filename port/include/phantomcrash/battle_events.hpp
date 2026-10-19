#pragma once

// What a battle tick reports to the HUD, the sounds and the screens
// (Battle::events, battle.hpp).

#include "phantomcrash/stage_collision.hpp"

#include <optional>

namespace phantomcrash {

struct WeaponTuning;

// What the HUD's messages report (0x04da6c queues them per robot): a hit
// the robot landed (0x09d2ce), the robot shot down, or a robot it helped
// shoot down (both 0x07675e); and the chatter events raised for the robot
// (0x049470, battle_chatter.hpp). A drone that respawned reports it.
// A robot that takes a supply pickup reports it, and one that shoots the
// supply carrier down (`amount`: the credits of a money pickup, the
// carrier's bounty). The sounds hear a weapon fired (a round leaving, a
// strike swung: the robot's, from `slot` at `at`) and a shot or strike
// landing (the attacker's, at `at`: on a robot, a wall or the floor). A
// robot destroyed, by anyone or anything, is `destroyed` at its middle. An
// `effect` sets off effect group `slot` at `at` (the supply wreck's crash).
// A piloted robot that took damage this tick is `damaged` by the sum, its
// `amount` (the original's controller rumble, 0x07675e). A direct hit that
// starts a stage object's destruction pays its `amount` as `object_reward`.
// `fence_contact` (a port addition) is a robot pressing against surface 8,
// at `surface`. A `hit` pays its attacker `amount` (hit_reward_ny in
// stored units, 0x09d2ce).
enum class BattleEventKind {
  shot_down,
  hit,
  bingo,
  chatter,
  respawned,
  supply,
  carrier_down,
  wreck_destroyed,
  object_reward,
  fired,
  impact,
  destroyed,
  effect,
  damaged,
  fence_contact,
  special_carrier // the red supply carrier set out (port addition)
};

struct BattleEvent {
  int robot{};
  BattleEventKind kind{};
  int chatter{-1}; // the chatter event (chatter_event)
  float amount{};
  int slot{-1}; // fired/impact: weapon mount (WeaponSlot); supply: pickup type
  const WeaponTuning *weapon{};
  Vec3 at;
  bool on_robot{}; // impact: it struck a robot
  std::optional<StageHit> surface;
  int object{-1}; // effect: scenery instance whose destruction changed collision
  int target{-1}; // impact: struck robot; preserved for attached source effects
  Vec3 direction; // fired: barrel direction before a multi-barrel weapon advances
};

} // namespace phantomcrash
