// TEST DRIVE: the ridden hangar's robot as the training ground's player,
// each fitted weapon from its record.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/test_drive.hpp"

#include <string>

using namespace phantomcrash;

namespace {

TestDriveScript shipped_script() {
  return parse_test_drive(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/test_drive.json"));
}

TrainingScript shipped_training() {
  return parse_training(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/training.json"));
}

// A gun and a melee arm weapon, a lobbed backpack weapon.
PartTables tables() {
  PartTables parts;
  parts.bodies.assign(3, ArmourRecord{});
  parts.legs.assign(2, ArmourRecord{});
  parts.chips.assign(5, ChipRecord{});
  parts.arm_weapons.assign(2, WeaponRecord{});
  parts.arm_weapons[0].name = "RIFLE";
  parts.arm_weapons[1].name = "BLADE";
  parts.arm_weapons[1].kind = hud_kind::melee;
  parts.shoulder_weapons.assign(1, WeaponRecord{});
  parts.shoulder_weapons[0].name = "MORTAR";
  parts.shoulder_weapons[0].flight = weapon_flight_lobbed;
  return parts;
}

// Hangar 1: body 2 at level 3, legs 1, CHIP 4 named Tin, the rifle in the first arm
// slot, the blade in the second and the mortar on the right backpack.
Garage owned() {
  Garage garage = new_game_garage(garage_new_game_hangars);
  Hangar &bay = garage.hangars[1];
  bay.paint = 7;
  const auto fit = [&](const PartSlot slot, const PartKind kind, const int id, const int level,
                       const std::string &name = "") {
    bay.parts[static_cast<std::size_t>(slot)] = garage.add_stock({kind, id, level, name, 1});
  };
  fit(PartSlot::body, PartKind::body, 2, 3);
  fit(PartSlot::legs, PartKind::legs, 1, 0);
  fit(PartSlot::chip, PartKind::chip, 4, 2, "Tin");
  fit(PartSlot::right_arm, PartKind::arm, 0, 0);
  fit(PartSlot::left_arm, PartKind::arm, 1, 5);
  fit(PartSlot::left_shoulder, PartKind::shoulder, 0, 0);
  garage.riding = 1;
  return garage;
}

} // namespace

TEST_CASE("test_drive.json names a template for every weapon kind") {
  const TestDriveScript script = shipped_script();
  CHECK(script.scene == 47);
  CHECK(script.return_to == "hangar");
  CHECK(script.lesson.mode == 3);
  CHECK(script.templates.size() == 4);
  const TrainingScript training = shipped_training();
  for (const auto &[kind, name] : script.templates) {
    CHECK(training.battle.settings.weapons.contains(name));
  }
}

TEST_CASE("a record's flight and HUD kind make its weapon kind") {
  WeaponRecord record;
  CHECK(record_kind(record) == WeaponKind::gun);
  record.kind = hud_kind::melee;
  CHECK(record_kind(record) == WeaponKind::melee);
  record.flight = weapon_flight_missile;
  CHECK(record_kind(record) == WeaponKind::missile);
  record.flight = weapon_flight_lobbed;
  CHECK(record_kind(record) == WeaponKind::grenade);
}

TEST_CASE("the ridden robot becomes the training ground's player") {
  const PartTables parts = tables();
  const Garage garage = owned();
  const TrainingScript script =
      test_drive_training(shipped_training(), shipped_script(), garage, garage.riding, parts);
  CHECK(script.return_to == "hangar");
  const RobotParts &robot = script.robots.at("player");
  CHECK(robot.body == 2);
  CHECK(robot.legs == 1);
  CHECK(robot.colour == 7);
  CHECK(robot.arms[0] == 0);
  CHECK(robot.arms[1] == 1);
  CHECK(robot.shoulders[0] == -1);
  CHECK(robot.shoulders[1] == 0);
  const TrainingRobot &player = script.battle.player;
  CHECK(player.chip == 4);
  CHECK(player.chip_level == 2);
  CHECK(player.chip_name == "Tin"); // the chatter's name for the robot
  CHECK(player.body_level == 3);
  CHECK(player.weapons[static_cast<std::size_t>(WeaponSlot::right_shoulder)].empty());
  const auto &weapons = script.battle.settings.weapons;
  const WeaponTuning &right = weapons.at(player.weapons[0]);
  CHECK(right.label == "RIFLE");
  CHECK(right.kind == WeaponKind::gun);
  const WeaponTuning &left = weapons.at(player.weapons[1]);
  CHECK(left.kind == WeaponKind::melee);
  CHECK(left.part.has_value() && left.part->level == 5);
  const WeaponTuning &mortar =
      weapons.at(player.weapons[static_cast<std::size_t>(WeaponSlot::left_shoulder)]);
  CHECK(mortar.kind == WeaponKind::grenade);
  CHECK(mortar.part.has_value() && mortar.part->table == WeaponTable::shoulder);
  // The loadout carries the levels on to the load and armour.
  const RobotLoadout loadout = training_loadout(script, "player", player);
  CHECK(loadout.body_level == 3);
  CHECK(loadout.weapons.size() == 3);
}
