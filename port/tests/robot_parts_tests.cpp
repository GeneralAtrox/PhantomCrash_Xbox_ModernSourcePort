// The original part records (tables/parts.json) and the weapons' ammunition.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) < 1e-3F;
}

// The Dog I chip and two weapons, as the extractor writes them.
const char *const part_tables_json = R"({
  "chips": [{"index": 0, "name": "Dog I", "rank": 0, "family": 1, "inner_shape": 0,
             "inner_value": {"base": 200, "mid": 560, "top": 700, "knee": 60, "low_scale": -1},
             "track": 3, "outer_shape": 2,
             "outer_value": {"base": 200, "mid": 595, "top": 800, "knee": 60, "low_scale": -1},
             "lock_frames": 22}],
  "arm_weapons": [{"index": 0, "name": "Pistol/HM15", "family": 0, "model": 0, "kind": 0,
                   "homing_degrees": {"base": 0, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
                   "ammo": {"base": 267, "mid": -133, "top": -9732, "knee": 98, "low_scale": -1}}],
  "shoulder_weapons": [{"index": 0, "name": "Aspide", "family": 0, "model": 0, "kind": 1,
                        "homing_degrees": {"base": 1.05, "mid": 0, "top": 1, "knee": 0,
                                           "low_scale": -1},
                        "ammo": {"base": 60, "mid": 0, "top": 60, "knee": 0, "low_scale": -1}}]
})";

} // namespace

TEST_CASE("a level curve climbs to its knee, then to its top") {
  const LevelCurve dog{200.0F, 560.0F, 700.0F, 60, -1};
  CHECK(near(dog.value(0), 200.0F));
  CHECK(near(dog.value(30), 480.0F));
  CHECK(near(dog.value(60), 760.0F));
  CHECK(near(dog.value(99), 900.0F));
  // A negative level runs the curve by its size, scaled by low_scale;
  // the value never drops below zero.
  const LevelCurve pistol{267.0F, -133.0F, -9732.0F, 98, -1};
  CHECK(near(pistol.value(-49), 267.0F + 66.5F));
  CHECK(pistol.value(99) == 0.0F);
  CHECK(near(LevelCurve{10.0F, 0.0F, 99.0F, 0, -1}.value(33), 43.0F));
  CHECK(near(LevelCurve{10.0F, 99.0F, 0.0F, 99, -1}.value(33), 43.0F));
}

TEST_CASE("the part tables give a chip's fire control and a weapon's HUD kind and ammo") {
  const PartTables tables = parse_part_tables(json::parse(part_tables_json));
  CHECK(tables.chip(0) != nullptr);
  CHECK(tables.chip(1) == nullptr);
  if (tables.chip(0) == nullptr) {
    return;
  }
  const ChipStats dog = chip_stats(*tables.chip(0), 30);
  CHECK(dog.inner_shape == 0);
  CHECK(dog.outer_shape == 2);
  CHECK(near(dog.inner_value, 480.0F));
  CHECK(dog.track == 3.0F);
  CHECK(dog.lock_frames == 22.0F);

  std::map<std::string, WeaponTuning> weapons = parse_weapon_tunings(json::parse(R"({
    "handgun": {"kind": "gun", "part": {"table": "arm", "index": 0}},
    "missile": {"kind": "missile", "part": {"table": "shoulder", "index": 0, "level": 5}},
    "drill": {"kind": "melee"},
    "lost": {"kind": "gun", "part": {"table": "shoulder", "index": 9}}
  })"));
  CHECK(weapons.at("drill").hud_kind == hud_kind::melee);
  CHECK(!weapons.at("missile").homing); // no turn rate yet
  const std::vector<std::string> missing = apply_weapon_records(weapons, tables);
  CHECK(missing == std::vector<std::string>{"lost"});
  CHECK(weapons.at("handgun").hud_kind == hud_kind::cannon);
  CHECK(weapons.at("handgun").max_ammo == 267);
  CHECK(weapons.at("missile").hud_kind == hud_kind::missile);
  CHECK(weapons.at("missile").max_ammo == 63); // 60 + 5/99 * 60, truncated
  CHECK(weapons.at("missile").homing);
  CHECK(weapons.at("drill").max_ammo == 0);
}

TEST_CASE("a magazine spends rounds and counts a refill up") {
  Magazine magazine;
  magazine.reset(3);
  CHECK(magazine.limited());
  for (int i = 0; i < 3; ++i) {
    magazine.take();
  }
  CHECK(magazine.empty());
  CHECK(magazine.shown == 0.0F);
  magazine.tick(1.0F);
  CHECK(near(magazine.dry_seconds, 1.0F));
  magazine.refill(10);
  CHECK(magazine.ammo == 3);
  CHECK(magazine.refilling());
  magazine.tick(Magazine::refill_count_seconds * 0.5F);
  CHECK(near(magazine.shown, 1.5F));
  magazine.tick(Magazine::refill_count_seconds);
  CHECK(!magazine.refilling());
  CHECK(magazine.shown == 3.0F);

  Magazine unlimited;
  unlimited.reset(0);
  unlimited.take();
  CHECK(!unlimited.empty());
  CHECK(!unlimited.limited());
}

namespace {

// The Slingshot's stats, a chip, a body, legs and two pilots, as the
// extractor writes them.
const char *const stats_json = R"({
  "chips": [{"index": 0, "name": "Dog I", "inner_shape": 0,
             "inner_value": {"base": 200, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
             "track": 3, "outer_shape": 2,
             "outer_value": {"base": 200, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
             "lock_frames": 22,
             "critical_percent": {"base": 1, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
             "dodge_percent": {"base": 150, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
             "knockdown_resistance": {"base": 3, "mid": 0, "top": 0, "knee": 0,
                                      "low_scale": -1}}],
  "shoulder_weapons": [{"index": 0, "name": "Slingshot", "family": 0, "model": 0, "kind": 3,
    "flight": 1, "launch_degrees": 30, "fire_frames": 45, "life_frames": 75,
    "start_speed": 0.75, "top_speed": 0.75, "acceleration": 0, "spread_degrees": 4,
    "homing_min_degrees": 0,
    "homing_degrees": {"base": 0, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "rounds_min": 1, "rounds": {"base": 2, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "damage_min": 300, "damage": {"base": 400, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "blast_radius": {"base": 20, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "blast_start_radius": 1,
    "blast_frames": {"base": 15, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "blast_damage": {"base": 120, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "ammo_min": 8, "ammo": {"base": 75, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "stun": {"base": 0, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "knockback": {"base": 0.5, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "knockdown": {"base": 12.5, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "price": {"base": 2500, "mid": 0, "top": 0, "knee": 0, "low_scale": -1}}],
  "bodies": [{"index": 0, "name": "Body", "armour":
    {"base": 15000, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "armour_floor": {"player": 6000, "computer": 0}}],
  "legs": [{"index": 0, "name": "Legs", "armour":
    {"base": 0, "mid": 0, "top": 0, "knee": 0, "low_scale": -1},
    "armour_floor": {"player": 100, "computer": 600},
    "knockback_resistance": {"base": 70, "mid": 0, "top": 0, "knee": 0, "low_scale": -1}}],
  "pilots": [{"index": 0, "name": null, "tag": null, "robot_name": null},
             {"index": 1, "name": "Yuki Kamishiro", "tag": "Yuki", "robot_name": "Kudryavka"}]
})";

} // namespace

TEST_CASE("the records give a weapon's timing, flight and blast, and a robot's armour and tag") {
  const PartTables tables = parse_part_tables(json::parse(stats_json));
  CHECK(tables.arm_weapons.empty()); // absent tables read as empty
  const WeaponRecord *record = tables.weapon(PartRef{WeaponTable::shoulder, 0, 0});
  CHECK(record != nullptr);
  if (record == nullptr) {
    return;
  }
  const WeaponStats sling = weapon_stats(*record, 0);
  CHECK(sling.flight == weapon_flight_lobbed);
  CHECK(sling.rounds == 2);
  CHECK(sling.damage == 400);
  CHECK(sling.ammo == 75);
  CHECK(sling.price == 2500);

  WeaponTuning weapon;
  weapon.kind = WeaponKind::grenade;
  apply_weapon_stats(weapon, sling);
  CHECK(near(weapon.cooldown, 0.8F)); // 45 frames: a round every 1 + 23 ticks (0x09b4f7)
  CHECK(near(weapon.life_seconds, 1.25F));
  CHECK(near(weapon.speed, 45.0F));
  CHECK(near(weapon.launch_degrees, 30.0F));
  CHECK(near(weapon.damage, 400.0F));
  CHECK(weapon.rounds == 2);
  CHECK(near(weapon.spread_degrees, 4.0F));
  CHECK(near(weapon.blast_radius, 20.0F));
  CHECK(near(weapon.blast_start_radius, 1.0F));
  CHECK(near(weapon.blast_seconds, 0.25F));
  CHECK(near(weapon.blast_damage, 120.0F));
  CHECK(!weapon.homing);
  CHECK(near(weapon.knockback, 15.0F)); // 0.5 a tick
  CHECK(near(weapon.knockdown_percent, 12.5F));
  CHECK(weapon.stagger_seconds == 0.0F); // no stun: no flinch

  // The player's floors raise weak parts; a computer robot's differ.
  CHECK(near(robot_armour(tables, 0, 0, 0, 0, true), 15100.0F));
  CHECK(near(robot_armour(tables, 0, 0, 0, 0, false), 15600.0F));
  CHECK(near(robot_armour(tables, 0, 0, 5, 0, true), 15000.0F)); // no such legs

  CHECK(tables.pilot(1) != nullptr && tables.pilot(1)->tag == "Yuki");
  CHECK(tables.pilot(0) != nullptr && tables.pilot(0)->tag.empty());
  CHECK(tables.pilot(2) == nullptr);
}

TEST_CASE("a robot's chip and legs give its dodge, critical and resistances") {
  const PartTables tables = parse_part_tables(json::parse(stats_json));
  const CombatStats stats = combat_stats(tables, 0, 0, 0, 0);
  CHECK(near(stats.critical_percent, 1.0F));
  CHECK(near(stats.dodge_chance, 1.0F)); // 150 clamped to 100 percent
  CHECK(near(stats.knockdown_resistance, 3.0F));
  CHECK(near(stats.knockback_resistance, 70.0F));
  const CombatStats none = combat_stats(tables, -1, 0, 4, 0);
  CHECK(none.dodge_chance == 0.0F && none.knockback_resistance == 0.0F);
}

TEST_CASE("fitted modules add their critical and knockdown resistance to the chip's") {
  const PartTables tables = parse_part_tables(json::parse(stats_json));
  const GarageTables garage = parse_garage_tables(
      json::parse(R"({"robots": [], "dealer_lineups": [], "paints": [], "emblems": [], "modules": [
    {"name": "Cat Statue", "price": 10, "robot_4538": 24, "robot_4540": 0},
    {"name": "30mm Armor Unit", "price": 10, "robot_4538": 0, "robot_4540": 12}]})"));
  CombatStats stats = combat_stats(tables, 0, 0, 0, 0);
  add_module_bonuses(stats, garage, {0, 1, 0, 7}); // no module 7
  CHECK(near(stats.critical_percent, 49.0F));      // 1 + 24 + 24, uncapped
  CHECK(near(stats.knockdown_resistance, 15.0F));  // 3 + 12
  CHECK(near(stats.dodge_chance, 1.0F));
}

TEST_CASE("fitted modules' laser reductions add up to the beam resistance, at most all of it") {
  const PartTables tables = parse_part_tables(json::parse(stats_json));
  const GarageTables garage = parse_garage_tables(
      json::parse(R"({"robots": [], "dealer_lineups": [], "paints": [], "emblems": [], "modules": [
    {"name": "10mm Armor Unit", "price": 10, "laser_reduction": 30},
    {"name": "Ivory Fortune God", "price": 10, "laser_reduction": 45},
    {"name": "Cat Statue", "price": 10, "robot_4538": 24}]})"));
  CombatStats stats = combat_stats(tables, 0, 0, 0, 0);
  CHECK(stats.beam_resistance == 0.0F); // the CHIP gives none
  add_module_bonuses(stats, garage, {0, 2});
  CHECK(near(stats.beam_resistance, 0.30F));
  CombatStats stacked = combat_stats(tables, 0, 0, 0, 0);
  add_module_bonuses(stacked, garage, {0, 1, 1});
  CHECK(near(stacked.beam_resistance, 1.0F)); // 30 + 45 + 45 kept to 100
}

TEST_CASE("a beam's charge recharges from empty to full over its recharge time") {
  Magazine beam;
  beam.reset(10, 2.0F);
  CHECK(beam.recharging());
  for (int shot = 0; shot < 10; ++shot) {
    beam.take();
  }
  CHECK(beam.empty());
  beam.tick(0.1F);
  CHECK(beam.ammo == 0);
  CHECK(near(beam.shown, 0.5F));
  beam.tick(1.0F); // 5.5 rounds recharged
  CHECK(beam.ammo == 5);
  beam.tick(5.0F);
  CHECK(beam.ammo == 10);
  CHECK(near(beam.shown, 10.0F));
  CHECK(!beam.refilling());
}
