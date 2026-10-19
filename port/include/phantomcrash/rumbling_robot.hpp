#pragma once

// A RUMBLING opponent as the live battle fights with it (rumbling_world.hpp):
// the robot its entrant rides (tables/town.json entrants, tables/garage.json
// robots), each part at its record's level raised by a cleared game's laps
// (0x0456b7), with its weapons, armour, fight stats and movement.

#include "phantomcrash/battle.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/robot.hpp"
#include "phantomcrash/robot_load.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/rumbling_rules.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/test_drive.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>

namespace phantomcrash {

// What an opponent's robot is made from.
struct RumblingTables {
  const PartTables &parts;
  const GarageTables &garage;
  const ContestTables &contests;
  const RumblingRules &rules;
  const TestDriveScript &drive;                         // the weapon templates by kind ...
  const std::map<std::string, WeaponTuning> &templates; // ... among these
  BattleHudStyle hud{};                                 // the chips' box sizes (ui/battle_hud.json)
};

// The maker and legs type RUMBLING's map draws a robot by (0x200838).
struct RobotKind {
  int maker{-1};
  int legs_type{-1};
};
[[nodiscard]] RobotKind robot_kind(const PartTables &parts, const RumblingRules &rules,
                                   const RobotRecord &record);

struct RumblingRobot {
  int entrant{-1};
  std::string name; // the pilot's partner, as the robot is called
  std::string tag;  // the pilot's marker tag
  RobotParts look;
  RobotLoadout loadout; // at the raised levels
  std::array<std::optional<WeaponTuning>, weapon_slots> weapons;
  float armour{};
  CombatStats combat;
  float lock_seconds{}; // its chip's lock time
  SightBoxes sight;     // and boxes
  MovementTuning movement;
  // The area map's icon for it (0x04438d, table 0x200838): its legs' maker
  // and type; -1 when unknown.
  int maker{-1};
  int legs_type{-1};
};

// A part's level with a cleared game's laps added, at most part_level_max.
[[nodiscard]] int raised_level(int level, const QuestSave &quest);

// Entrant `entrant`'s robot; its weapons are named `prefix` and the slot.
// `movement` is the battle's base movement the robot's load slows.
[[nodiscard]] RumblingRobot rumbling_robot(const RumblingTables &tables, const QuestSave &quest,
                                           int entrant, const std::string &prefix,
                                           const MovementTuning &movement, int level_offset = 0);

} // namespace phantomcrash
