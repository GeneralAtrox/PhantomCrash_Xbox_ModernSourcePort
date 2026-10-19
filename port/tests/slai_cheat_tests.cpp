#include "test_support.hpp"
#include "phantomcrash/cheats.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include <set>

using namespace phantomcrash;

TEST_CASE("Fill Hangars includes all SLAI body types after the Phantom Crash starters") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  if (!roots.find_asset("slai/native_parts.json")) {
    return;
  }
  const auto data = roots.document("debug/cheats.json");
  const auto cheats = parse_cheat_tables(data);
  const auto base = parse_cheat_tables(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("debug/cheats.json"));
  const auto parts = parse_part_tables(json::read_file(roots.asset("tables/parts.json")));
  const auto garage = parse_garage_tables(json::read_file(roots.asset("tables/garage.json")));
  const auto &slai = json::required(data, "slai_hangar_robots").array();
  REQUIRE(slai.size() == 10);
  REQUIRE(cheats.hangar_robots.size() == base.hangar_robots.size() + slai.size());
  for (std::size_t i = 0; i < base.hangar_robots.size(); ++i) {
    CHECK(cheats.hangar_robots[i] == base.hangar_robots[i]);
  }
  CheatTables only;
  std::set<int> body_models;
  for (std::size_t i = 0; i < slai.size(); ++i) {
    const int index = static_cast<int>(slai[i].number());
    CHECK(cheats.hangar_robots[base.hangar_robots.size() + i] == index);
    const auto *robot = garage.robot(index);
    REQUIRE(robot != nullptr);
    REQUIRE(robot->complete());
    const int body = robot->part(PartSlot::body)->id;
    REQUIRE(body >= 0 && body < static_cast<int>(parts.bodies.size()));
    CHECK(body_models.insert(parts.bodies[static_cast<std::size_t>(body)].maker).second);
    only.hangar_robots.push_back(index);
  }
  QuestSave all_slai;
  all_slai.garage = new_game_garage(1);
  CHECK(fill_hangars(all_slai, only, parts, garage) == 10);
  for (std::size_t i = 0; i < slai.size(); ++i) {
    const auto &hangar = all_slai.garage.hangars[i];
    CHECK(hangar.owned);
    CHECK(hangar.complete());
    CHECK(hangar.name.starts_with("SLAI "));
  }
  CHECK(all_slai.garage.hangars[10].empty());
  CHECK(all_slai.garage.hangars[11].empty());
  const auto saved = all_slai.garage;
  CHECK(fill_hangars(all_slai, only, parts, garage) == 0);
  CHECK(all_slai.garage == saved);

  QuestSave mixed;
  mixed.garage = new_game_garage(1);
  REQUIRE(install_robot(mixed.garage, 0, *garage.robot(base.hangar_robots[0]), parts, garage) >= 0);
  mixed.garage.hangars[0].name = "My current robot";
  const auto current = mixed.garage.hangars[0];
  CHECK(fill_hangars(mixed, cheats, parts, garage) == garage_hangar_count - 1);
  CHECK(mixed.garage.hangars[0].name == current.name);
  CHECK(mixed.garage.hangars[0].parts == current.parts);
  for (std::size_t i = 1; i < mixed.garage.hangars.size(); ++i) {
    CHECK(mixed.garage.hangars[i].name == garage.robot(cheats.hangar_robots[i])->name);
  }
}
