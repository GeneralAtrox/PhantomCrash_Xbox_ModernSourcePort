#pragma once

// The RUMBLING tests' tables: a few contests, teams and entrants, and the
// rules and parts they are checked against.

#include "phantomcrash/contests.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/rumbling_roster.hpp"
#include "phantomcrash/rumbling_rules.hpp"

#include <string>

namespace phantomcrash::rumbling_fixture {

inline constexpr double tick = 1.0 / 60.0;

// Classes: 0 open, 1 maker 1 with legs type 5, 2 at most 10 weight with no
// O.C modules. Contest 0 (class 0, venue 0 every day but day 1) enters
// teams 0 and 1 with team 2 as its extra team; contest 1 is class 1 and
// contest 2 class 2; contest 3 has a champion and no teams. Day 1 holds the
// Area Ranker contest 14 at Shinjuku.
// Entrants 0-7 are level 10 on robot 0 (maker 0, legs model 0), 8 is level
// 50, 9 rides robot 1 (maker 1, legs model 1) and 10 robot 2 (heavy, O.C).
inline ContestTables contests() {
  std::string list;
  for (int index = 0; index < 15; ++index) {
    const int contest_class = index < 3 ? index : 0;
    std::string entry = "{\"name\": \"C" + std::to_string(index) +
                        "\", \"class\": " + std::to_string(contest_class) +
                        ", \"levels\": [0, 40], \"reserve_after\": 2, \"roster_size\": 3";
    if (index == 0) {
      entry += ", \"teams\": [0, 1], \"extra_team\": 2, \"champion\": 8";
    } else if (index == 3) {
      entry += ", \"champion\": 8";
    }
    list += std::string(index == 0 ? "" : ",") + entry + "}";
  }
  std::string calendar;
  for (int day = 0; day < calendar_days; ++day) {
    calendar += std::string(day == 0 ? "" : ",") +
                (day == 1 ? "{\"venues\": [14, -1, -1]}" : "{\"venues\": [0, -1, -1]}");
  }
  std::string entrants;
  for (int id = 0; id < 11; ++id) {
    const int robot = id == 9 ? 1 : (id == 10 ? 2 : 0);
    entrants += std::string(id == 0 ? "" : ",") + "{\"pilot\": " + std::to_string(id % 2) +
                ", \"robot\": " + std::to_string(robot) +
                ", \"level\": " + std::to_string(id == 8 ? 50 : 10) + "}";
  }
  return parse_contest_tables(json::parse(
      "{\"classes\": [{\"name\": \"OPEN\"}, {\"name\": \"MAKER\", \"maker\": 1, \"legs\": [5]},"
      " {\"name\": \"LIGHT\", \"weight\": [-1, 10], \"other\": 1}],"
      " \"contests\": [" +
      list + "], \"calendar\": [" + calendar +
      "], \"teams\": [{\"leader\": 7, \"members\": [0, 1, -1, -1, -1, -1, -1, -1]},"
      " {\"leader\": -1, \"members\": [2, 3, 9, -1, -1, -1, -1, -1]},"
      " {\"leader\": -1, \"members\": [4, 5, -1, -1, -1, -1, -1, -1]}],"
      " \"entrants\": [" +
      entrants + "]}"));
}

struct Fixture {
  RumblingRules rules = parse_rumbling_rules(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"));
  PartTables parts;
  GarageTables garage;
  PartStatsTable stats;
  ContestTables tables = contests();

  Fixture() {
    rules.roster.live_size = 0; // the original's roster sizes
    rules.legs_types = {2, 5};
    parts.bodies.assign(2, ArmourRecord{});
    parts.bodies[1].maker = 1;
    parts.legs.assign(2, ArmourRecord{});
    parts.legs[1].model = 1;
    PilotRecord pilot;
    pilot.name = "Yuki";
    pilot.robot_name = "Kudryavka";
    pilot.bounty = {0.0F, 5000.0F, 10769.0F, 65, 1};
    parts.pilots = {pilot, PilotRecord{}};
    for (int robot = 0; robot < 3; ++robot) {
      RobotRecord record;
      record.name = "R" + std::to_string(robot);
      const int part = robot == 1 ? 1 : 0;
      record.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{part, 0};
      record.parts[static_cast<std::size_t>(PartSlot::legs)] = RobotPart{part, 0};
      if (robot == 2) {
        record.modules = {0};
      }
      garage.robots.push_back(record);
    }
    ModuleRecord module;
    module.weight = 25.0F;
    module.active_frames = 600;
    garage.modules = {module};
  }

  [[nodiscard]] RegulationCheck check() const {
    return {rules, parts, garage, stats};
  }
};

inline RosterRandom constant(const float value) {
  return [value] { return value; };
}

} // namespace phantomcrash::rumbling_fixture
