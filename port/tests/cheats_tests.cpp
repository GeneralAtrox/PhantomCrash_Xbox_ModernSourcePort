// Tools > Cheats' one-off changes (cheats.hpp) and the switches' gates.

#include "test_support.hpp"

#include "phantomcrash/cheats.hpp"
#include "phantomcrash/garage.hpp"
#include "phantomcrash/rumbling_rules.hpp"

#include <algorithm>
#include <string>

using namespace phantomcrash;

namespace {

// A complete robot on `body`, with one arm weapon and a CHIP.
RobotRecord robot_on(const int body) {
  RobotRecord robot;
  robot.name = "SV-" + std::to_string(body);
  robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{body, 0};
  robot.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{0, 0};
  robot.parts[static_cast<std::size_t>(PartSlot::right_arm)] = RobotPart{0, 0};
  robot.parts[static_cast<std::size_t>(PartSlot::chip)] = RobotPart{0, 0};
  return robot;
}

struct Tables {
  PartTables parts;
  GarageTables garage;

  Tables() {
    // Bodies 1 and 2 are one model: 1 the enemy's copy, 2 the shop's.
    parts.bodies.assign(3, ArmourRecord{});
    parts.bodies[1].maker = 1;
    parts.bodies[2].maker = 1;
    parts.bodies[2].price.base = 2000.0F;
    parts.legs.assign(2, ArmourRecord{});
    parts.legs[0].model = 0;
    parts.legs[1].model = 1;
    parts.chips.assign(2, ChipRecord{});
    parts.chips[0].name = "OCG";
    parts.chips[1].name = "OCG"; // another chatter
    WeaponRecord weapon;
    weapon.model = 0;
    parts.arm_weapons.assign(2, weapon);
    parts.arm_weapons[1].price.base = 1500.0F;
    parts.shoulder_weapons.assign(2, weapon);
    parts.shoulder_weapons[1].model = -1; // a blast's, which no robot carries
    garage.modules.push_back({"TURBO", 1.0F, 500});
    garage.modules.push_back({"TURBO", 1.0F, 500});
    garage.paints.resize(4);
    garage.emblems.resize(3);
    for (int body = 0; body < 3; ++body) {
      garage.robots.push_back(robot_on(body));
    }
  }
};

int loose(const Garage &garage, const PartKind kind, const int id) {
  return static_cast<int>(std::ranges::count_if(garage.stock, [&](const auto &entry) {
    return entry.second.kind == kind && entry.second.id == id && entry.second.hangar < 0;
  }));
}

} // namespace

TEST_CASE("fill hangars fits each listed body model once into the empty hangars") {
  const Tables tables;
  QuestSave quest;
  quest.garage = new_game_garage(1);
  CHECK(install_robot(quest.garage, 0, tables.garage.robots[1], tables.parts, tables.garage) >= 0);
  CheatTables cheats;
  cheats.hangar_robots = {2, 0, 1, 7};

  CHECK(fill_hangars(quest, cheats, tables.parts, tables.garage) == 1); // 2 is 1's model
  const auto &hangars = quest.garage.hangars;
  CHECK(hangars[1].name == "SV-0");
  CHECK(hangars[1].owned);
  CHECK(hangars[1].complete());
  CHECK(hangars[2].empty());
  CHECK(fill_hangars(quest, cheats, tables.parts, tables.garage) == 0); // all held already
}

TEST_CASE("all items keeps each distinct part loose, raises the stock and owns the rest") {
  const Tables tables;
  QuestSave quest;
  CheatTables cheats;
  cheats.stock_size = 600;
  add_all_items(quest, cheats, tables.parts, tables.garage, 5);

  const Garage &garage = quest.garage;
  CHECK(garage.stock_size == 600);
  CHECK(loose(garage, PartKind::body, 0) == 1);
  CHECK(loose(garage, PartKind::body, 1) == 0); // the enemy's copy of 2
  CHECK(loose(garage, PartKind::body, 2) == 1);
  CHECK(loose(garage, PartKind::legs, 1) == 1);
  CHECK(loose(garage, PartKind::arm, 0) == 0);
  CHECK(loose(garage, PartKind::arm, 1) == 2); // the one sold
  CHECK(loose(garage, PartKind::shoulder, 0) == 2);
  CHECK(loose(garage, PartKind::shoulder, 1) == 0);
  CHECK(loose(garage, PartKind::chip, 1) == 0);
  CHECK(loose(garage, PartKind::module, 0) == 1);
  CHECK(garage.stock.size() == 2 + 2 + 2 + 2 + 1 + 1);
  const auto chip = std::ranges::find_if(
      garage.stock, [](const auto &entry) { return entry.second.kind == PartKind::chip; });
  CHECK(chip->second.name == "OCG");
  CHECK((garage.paints == std::vector<int>{0, 1, 2, 3}));
  CHECK((garage.emblems == std::vector<int>{0, 1, 2, emblem_none}));
  CHECK(quest.music.owned.size() == 5);

  const std::size_t entries = garage.stock.size();
  add_all_items(quest, cheats, tables.parts, tables.garage, 5);
  CHECK(quest.garage.stock.size() == entries); // topped up, not doubled

  QuestSave bought;
  bought.garage.add_stock({PartKind::body, 1, 0, {}, -1}); // a same part counts
  add_all_items(bought, cheats, tables.parts, tables.garage, 5);
  CHECK(loose(bought.garage, PartKind::body, 2) == 0);
}

TEST_CASE("a raised stock is saved with the garage; the original's is not written") {
  Garage garage = new_game_garage(1);
  CHECK(garage_to_json(garage).find("stock_size") == nullptr);
  garage.stock_size = 400;
  for (int entry = 0; entry < 300; ++entry) {
    CHECK(garage.add_stock({PartKind::legs, 0, 0, {}, -1}) == entry);
  }
  const Garage read = garage_from_json(garage_to_json(garage));
  CHECK(read.stock_size == 400);
  CHECK(read.stock.size() == 300);
  CHECK(read.free_stock() == 100);
}

TEST_CASE("go to day sleeps the nights until the calendar shows the day") {
  SleepScript sleep;
  QuestSave quest;
  quest.day = 10;
  quest.spin_paid = 50;
  go_to_day(quest, 14, sleep);
  CHECK(quest.day == 14);
  CHECK(quest.total_days == 4);
  CHECK(quest.spin_paid == 0);
  go_to_day(quest, 2, sleep); // round the calendar's end
  CHECK(quest.day == 2);
  CHECK(quest.total_days == 4 + 48);
  go_to_day(quest, 2, sleep);
  CHECK(quest.total_days == 52);
}

TEST_CASE("open contests pass the regulation with any ridden Scoobee and the fee") {
  RumblingRules rules;
  PartTables parts;
  parts.bodies.assign(1, ArmourRecord{}); // maker 0
  parts.legs.assign(1, ArmourRecord{});
  parts.chips.assign(1, ChipRecord{});
  const GarageTables garage;
  const PartStatsTable stats;
  QuestSave quest;
  quest.money = 100;
  quest.garage = new_game_garage(1);
  Hangar &bay = quest.garage.hangars[0];
  bay.parts[0] = quest.garage.add_stock({PartKind::body, 0, 0, {}, 0});
  bay.parts[1] = quest.garage.add_stock({PartKind::legs, 0, 0, {}, 0});
  bay.parts[6] = quest.garage.add_stock({PartKind::chip, 0, 0, {}, 0});
  quest.garage.riding = 0;
  ContestClass photon;
  photon.maker = 1;
  photon.fee = 50;

  CHECK(!meets_regulation({rules, parts, garage, stats}, photon, quest));
  CHECK(meets_regulation({rules, parts, garage, stats, true}, photon, quest));
  quest.money = 10;
  CHECK(!meets_regulation({rules, parts, garage, stats, true}, photon, quest));
}
