#pragma once

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/garage.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/training_ground.hpp"

#include <map>
#include <optional>
#include <string>

namespace phantomcrash {

// TEST DRIVE (scene 47, 0x097192): the ridden robot's loadout (+ 0x60c8)
// in a battle of mode 6 on contest record 47; it goes back to the hangar
// after (0x0a8e1b, scene 14). What data/world/test_drive.json holds.
struct TestDriveScript {
  int scene{47};
  int contest{47};
  std::string return_to{"hangar"};
  // The port runs it as the training ground's FreePlay (lesson mode 3).
  TrainingLesson lesson{"TEST DRIVE", 3, 1};
  // The training weapon (training.json battle.weapons) whose tuning a
  // record of each kind starts from; the record then gives its stats.
  std::map<WeaponKind, std::string> templates;
  std::vector<std::pair<std::string, std::string>> stage_choices; // stage, label
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] TestDriveScript parse_test_drive(const json::JsonValue &document);

// How a weapon record fights: its flight (+0x1d) makes missiles and
// grenades, its HUD kind (+0x1c) melee weapons; the rest are guns.
[[nodiscard]] WeaponKind record_kind(const WeaponRecord &record);

// The battle's weapon for part `ref` (a weapon record at its level): the
// template of its kind from `base`, named `name`, labelled and given the
// record's stats. Nothing when the record is missing.
[[nodiscard]] std::optional<WeaponTuning>
record_weapon(const TestDriveScript &drive, const std::map<std::string, WeaponTuning> &base,
              const PartTables &parts, const PartRef &ref, std::string name);

// The training script with hangar `hangar`'s robot as the player's: its
// body, legs and CHIP at their levels, its paint and emblem, and a weapon
// for each fitted arm and backpack slot.
[[nodiscard]] TrainingScript test_drive_training(const TrainingScript &base,
                                                 const TestDriveScript &drive, const Garage &garage,
                                                 int hangar, const PartTables &parts);

} // namespace phantomcrash
