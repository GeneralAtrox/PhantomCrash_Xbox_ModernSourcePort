#pragma once

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/entry_shield.hpp"
#include "phantomcrash/robot_mover.hpp"
#include "phantomcrash/robot_wrecks.hpp"
#include "phantomcrash/robot_ai.hpp"
#include <map>
#include <vector>

namespace phantomcrash {
// Original NPC decisions; training drones remain on their scripted patrol.
struct DroneAi {
  bool fight{}, trace{};
  int area{-1}; // the RUMBLING area the battle is in (quest +0x6104 / 2), or -1
  // The attract demo and RESULT's backdrop (quest +0x618c 1): every opponent
  // acts at showcase_ability (0x05f445) and chases any supply box (0x05949b).
  bool showcase{};
  // Port options, off by default (rumbling.json live.ai):
  bool nearest_fallback_target{};   // 0x05b9c1's second pass keeps the nearest, not the last seen
  bool jump_keeps_target{};         // the target is remembered through the jump at it
  bool rivals_by_sight{};           // a computer rival is taken on sight, as a player's robot is
  bool jump_any_floor{};            // the jump ignores area 1's floors (0x06e331) elsewhere
  bool assigned_ability_settings{}; // cached settings follow the assigned ability, not 0
  NavTuning navigation;             // planned routes, held jumps and high ground
  std::shared_ptr<const OriginalAiData> original;
  float walk{0.8F}; // retained for existing callers; original routes use their own table
};

[[nodiscard]] DroneAi parse_drone_ai(const json::JsonValue &value);

struct BattleSettings {
  ArenaBounds arena; // the walls while the stage has no collision
  MovementTuning movement;
  std::map<std::string, WeaponTuning> weapons;
  float robot_radius{2.0F}; // robots without a leg radius
  float robot_height{4.4F};
  // A port addition: a hit turns the robot white this long; 0 is off, as
  // the original's damage and robot drawing set no flash.
  float hit_flash_seconds{};
  // The view a robot without a drawn reticle locks through, from its own
  // eyes (port value: the chase camera's; the original uses its camera).
  float sight_fov_degrees{55.0F};
  float arena_wall_height{20.0F};
  float fence_contact_seconds{0.25F}; // port contact pulses; <= 0 disables
  float drone_respawn_seconds{4.0F};
  float death_fallback_seconds{2.0F};            // presentation duration without a loaded pose
  float drone_walk{0.6F};                        // the drones' stick when patrolling
  float drone_reach{3.0F};                       // a path point counts as reached this close
  float camouflage_seconds{12.0F};               // a full gauge lasts this long ...
  float camouflage_recharge_seconds{20.0F};      // ... and refills in this long
  float camouflage_fade_seconds{1.0F};           // it sweeps over the robot in this long
                                                 // (60 frames, states 1/2 and 5/6 of 0x074e80)
  float camouflage_break_seconds{20.0F / 60.0F}; // a hit drops it this fast (state 7, 0x075444)
  float ammo_refill_seconds{};       // an empty weapon refills after this long (0: never)
  float idle_chatter_seconds{30.0F}; // standing with the pad untouched this long (0x08e480)
  EntryShield entry_shield;
  DroneAi ai;
  RobotWreckSettings robot_wrecks;
  // NY a direct hit that starts a stage object's destruction pays its
  // shooter, by object kind (0x203734: ten stored units per entry, 0x0486af).
  std::vector<int> object_reward_ny{10000, 5000, 3000, 7000, 4000, 3000, 0, 10000, 0};
  // NY each hit that damages another robot pays its attacker (0x09d2ce: one
  // stored unit, but not in the tutorial or TEST DRIVE); 0 pays nothing.
  int hit_reward_ny{};
};

[[nodiscard]] BattleSettings parse_battle_settings(const json::JsonValue &value);

} // namespace phantomcrash
