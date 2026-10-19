// The garage: hangars, the parts stock, and fitting a dealer's SV.

#include "test_support.hpp"

#include "phantomcrash/garage.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/robot_parts.hpp"

using namespace phantomcrash;

namespace {

RobotRecord robot_with_modules(const std::vector<int> &modules) {
  RobotRecord robot;
  robot.name = "HL-220H";
  robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{3, 1};
  robot.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{4, 2};
  robot.parts[static_cast<std::size_t>(PartSlot::chip)] = RobotPart{0, 99};
  robot.modules = modules;
  robot.paint = 2;
  robot.emblem = 7;
  return robot;
}

} // namespace

TEST_CASE("a new game's garage owns its first hangars, all empty") {
  const Garage garage = new_game_garage(garage_new_game_hangars);
  CHECK(garage.hangars[0].owned);
  CHECK(garage.hangars[2].owned);
  CHECK(!garage.hangars[3].owned);
  CHECK(garage.hangars[0].empty());
  CHECK(garage.free_stock() == garage_stock_size);
  CHECK(garage.riding == -1);
}

TEST_CASE("the stock takes the lowest free entry") {
  Garage garage = new_game_garage(1);
  CHECK(garage.add_stock({PartKind::body, 1, 0, {}, -1}) == 0);
  CHECK(garage.add_stock({PartKind::legs, 2, 0, {}, -1}) == 1);
  garage.stock.erase(0);
  CHECK(garage.add_stock({PartKind::chip, 3, 5, "ACE", -1}) == 0);
  CHECK(garage.free_stock() == garage_stock_size - 2);
}

TEST_CASE("installing a robot fills its hangar and the stock, naming the CHIP") {
  PartTables parts;
  parts.chips.push_back(ChipRecord{});
  parts.chips[0].name = "OCG-2000";
  GarageTables tables;
  tables.modules.push_back({"TURBO", 1.0F, 500});
  Garage garage = new_game_garage(garage_new_game_hangars);
  const RobotRecord robot = robot_with_modules({0});
  CHECK(robot.complete());
  CHECK(robot.stock_needed() == 4);

  const int chip = install_robot(garage, 1, robot, parts, tables);
  CHECK(chip >= 0);
  const Hangar &bay = garage.hangars[1];
  CHECK(!bay.empty());
  CHECK(bay.complete());
  CHECK(bay.name == "HL-220H");
  CHECK(bay.paint == 2);
  CHECK(bay.emblem == 7);
  CHECK(bay.modules.size() == 1);
  CHECK(garage.stock.at(chip).name == "OCG-2000");
  CHECK(garage.stock.at(chip).level == 99);
  CHECK(garage.stock.at(bay.part(PartSlot::legs)).id == 4);
  CHECK(garage.stock.at(bay.part(PartSlot::legs)).hangar == 1);
  CHECK(garage.free_stock() == garage_stock_size - 4);
}

TEST_CASE("the garage survives the save file") {
  PartTables parts;
  parts.chips.push_back(ChipRecord{});
  GarageTables tables;
  tables.modules.push_back({"TURBO", 1.0F, 500});
  Garage garage = new_game_garage(garage_new_game_hangars);
  const int chip = install_robot(garage, 0, robot_with_modules({0}), parts, tables);
  garage.stock.at(chip).name = "BUDDY";
  garage.riding = 0;
  garage.paints.push_back(5);

  const Garage back = garage_from_json(garage_to_json(garage));
  CHECK(back.stock.size() == garage.stock.size());
  CHECK(back.stock.at(chip).name == "BUDDY");
  CHECK(back.stock.at(chip).kind == PartKind::chip);
  CHECK(back.hangars[0].name == "HL-220H");
  CHECK(back.hangars[0].parts == garage.hangars[0].parts);
  CHECK(back.hangars[0].modules == garage.hangars[0].modules);
  CHECK(back.hangars[2].owned);
  CHECK(!back.hangars[3].owned);
  CHECK(back.riding == 0);
  CHECK(back.paints == garage.paints);
  CHECK(back.emblems == garage.emblems);
}

TEST_CASE("ten hangar saves gain empty slots and the twelfth robot survives saving") {
  Garage original = new_game_garage(10);
  original.hangars[9].name = "Existing tenth";
  auto legacy = garage_to_json(original);
  for (auto &[key, value] : std::get<json::JsonValue::Object>(legacy.value)) {
    if (key == "hangars") {
      std::get<json::JsonValue::Array>(value.value).resize(10);
    }
  }
  Garage migrated = garage_from_json(legacy);
  CHECK(migrated.hangars[9] == original.hangars[9]);
  CHECK(!migrated.hangars[10].owned);
  CHECK(!migrated.hangars[11].owned);
  CHECK(migrated.hangars[10].empty());
  CHECK(migrated.hangars[11].empty());
  PartTables parts;
  parts.chips.push_back(ChipRecord{});
  GarageTables tables;
  migrated.hangars[11].owned = true;
  REQUIRE(install_robot(migrated, 11, robot_with_modules({}), parts, tables) >= 0);
  migrated.riding = 11;
  const auto restored = garage_from_json(garage_to_json(migrated));
  CHECK(restored == migrated);
  CHECK(restored.hangars[11].complete());
  CHECK(restored.riding == 11);
}

TEST_CASE("an SV's price adds up its modules, paint and emblem") {
  PartTables parts;
  GarageTables tables;
  tables.modules.push_back({"TURBO", 1.0F, 500});
  tables.paints.assign(3, {"PAINT", 0});
  tables.paints[2].price = 40;
  tables.emblems.assign(8, {"EMBLEM", 0});
  tables.emblems[7].price = 3;
  CHECK(robot_price(parts, tables, robot_with_modules({0, 0})) == 1043);
}
