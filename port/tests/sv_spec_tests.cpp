// SV SPEC: the robot a hangar or an offer makes, the vehicle's lines, the
// weapons' and combat pages and turning between the pages.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sv_spec.hpp"

#include <cmath>
#include <string>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

SvSpecScript shipped_script() {
  return parse_sv_spec(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/sv_spec.json"));
}

PartStatsTable shipped_table() {
  return parse_part_stats(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/part_stats.json"));
}

// A body of maker 1 and legs light enough not to slow it; an arm weapon.
PartTables tables() {
  PartTables parts;
  parts.bodies.assign(1, ArmourRecord{});
  parts.bodies[0].name = "BODY";
  parts.bodies[0].maker = 1;
  parts.bodies[0].sockets.base = 4.0F;
  parts.legs.assign(1, ArmourRecord{});
  parts.legs[0].name = "LEGS";
  parts.legs[0].capacity.base = 9000.0F;
  parts.legs[0].speed.base = 120.0F;
  parts.legs[0].jump.base = 20.0F;
  parts.chips.assign(1, ChipRecord{});
  parts.chips[0].name = "CHIP";
  parts.chips[0].critical_percent.base = 12.0F;
  parts.chips[0].dodge_percent.base = 30.0F;
  parts.chips[0].knockdown_resistance.base = 5.0F;
  parts.chips[0].lock_frames = 30;
  parts.chips[0].track = 500;
  parts.legs[0].knockback_resistance.base = 25.0F;
  parts.arm_weapons.assign(1, WeaponRecord{});
  parts.arm_weapons[0].name = "GUN";
  return parts;
}

// Hangar 1 holds the body, legs, CHIP and the gun in the first arm slot.
Garage owned() {
  Garage garage = new_game_garage(garage_new_game_hangars);
  Hangar &bay = garage.hangars[1];
  bay.name = "ALPHA";
  const auto fit = [&](const PartSlot slot, const PartKind kind) {
    bay.parts[static_cast<std::size_t>(slot)] = garage.add_stock({kind, 0, 0, "", 1});
  };
  fit(PartSlot::body, PartKind::body);
  fit(PartSlot::legs, PartKind::legs);
  fit(PartSlot::chip, PartKind::chip);
  fit(PartSlot::right_arm, PartKind::arm);
  return garage;
}

void settle(SvSpec &logic, const double seconds = 1.5) {
  for (double time = 0.0; time < seconds; time += tick) {
    logic.update(tick, InputFrame{});
  }
}

void tap(SvSpec &logic, const Buttons buttons) {
  logic.update(tick, InputFrame{buttons, buttons, 1});
  settle(logic, 0.5);
}

} // namespace

TEST_CASE("sv_spec.json reads the window, the three pages and the lines") {
  const SvSpecScript script = shipped_script();
  CHECK(script.scene == 36);
  CHECK(script.exit_to == "back");
  CHECK(script.rows.size() == 8);
  CHECK(script.rows[1].stat == SpecStat::walk);
  CHECK(script.weapon_slots.size() == 4);
  CHECK(script.weapon_slots[0] == PartSlot::right_arm);
  CHECK(script.maker_icons.size() == 6);
  CHECK(!script.pages[0].help.empty());
  REQUIRE(script.pages.size() == 3);
  CHECK(script.pages[0].kind == SpecPageKind::vehicle);
  CHECK(script.pages[1].kind == SpecPageKind::weapons);
  CHECK(script.pages[2].kind == SpecPageKind::combat);
  CHECK(!script.pages[2].panel.has_value());
  CHECK(script.combat_rows.size() == 8);
  CHECK(script.combat_rows[0].stat == CombatStat::mortal_shot);
}

TEST_CASE("the combat page: the CHIP's chances, lock and aim, the legs' knockback") {
  const PartTables parts = tables();
  const GarageTables garage;
  const PartStatsTable table = shipped_table();
  SvSpec logic(shipped_script(), hangar_spec(owned(), 1), parts, garage, table, "hangar");
  CHECK(std::abs(logic.value(CombatStat::mortal_shot) - 12.0) < 1e-3);
  CHECK(std::abs(logic.value(CombatStat::repulsion_shield) - 30.0) < 1e-3);
  CHECK(std::abs(logic.value(CombatStat::knockdown_resistance) - 5.0) < 1e-3);
  CHECK(std::abs(logic.value(CombatStat::knockback_resistance) - 25.0) < 1e-3);
  CHECK(std::abs(logic.value(CombatStat::lock_time) - 0.5) < 1e-3);
  CHECK(logic.value(CombatStat::aim_speed) == 500.0);
  CHECK(logic.value(CombatStat::laser_reduction) == 0.0);
}

TEST_CASE("a hangar's spec: its speeds under the load, its weapons, its maker") {
  const PartTables parts = tables();
  const GarageTables garage;
  const PartStatsTable table = shipped_table();
  SvSpec logic(shipped_script(), hangar_spec(owned(), 1), parts, garage, table, "hangar");
  CHECK(logic.robot().name == "ALPHA");
  CHECK(logic.maker() == 1);
  CHECK(logic.totals().chip_name == "CHIP");
  // Weighing nothing, the robot is not slowed: the dash is the legs' speed.
  CHECK(std::abs(logic.value(SpecStat::dash) - 120.0) < 1e-3);
  CHECK(std::abs(logic.value(SpecStat::walk) - 60.0) < 1e-3);
  CHECK(std::abs(logic.value(SpecStat::jump) - 20.0) < 1e-3);
  CHECK(logic.value(SpecStat::sockets) == 4.0);
  CHECK(logic.weapon(PartSlot::right_arm) == std::optional<std::string>{"GUN"});
  CHECK(!logic.weapon(PartSlot::left_shoulder).has_value());
}

TEST_CASE("an offer's spec fits the SV into a stock of its own") {
  const PartTables parts = tables();
  const GarageTables garage;
  RobotRecord record;
  record.name = "OFFER";
  record.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{0, 0};
  record.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{0, 0};
  record.parts[static_cast<std::size_t>(PartSlot::chip)] = RobotPart{0, 0};
  const SpecRobot robot = record_spec(record, parts, garage);
  CHECK(robot.name == "OFFER");
  CHECK(robot.garage.hangars[0].complete());
}

TEST_CASE("left and right turn the page; cancel goes back") {
  const PartTables parts = tables();
  const GarageTables garage;
  const PartStatsTable table = shipped_table();
  SvSpec logic(shipped_script(), hangar_spec(owned(), 1), parts, garage, table, "assemble");
  settle(logic);
  const SvSpecScript &script = logic.script();
  CHECK(logic.page() == 0);
  tap(logic, script.right);
  CHECK(logic.page() == 1);
  CHECK(logic.help_text() == script.pages[1].help);
  tap(logic, script.right);
  CHECK(logic.page() == 2);
  CHECK(logic.page_kind() == SpecPageKind::combat);
  tap(logic, script.right);
  CHECK(logic.page() == 0);
  tap(logic, script.left);
  CHECK(logic.page() == 2);
  tap(logic, script.right);
  CHECK(logic.page() == 0);
  tap(logic, script.cancel_buttons);
  settle(logic, 2.0);
  REQUIRE(logic.done());
  CHECK(logic.next() == "assemble");
}
