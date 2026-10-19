#pragma once

// The rules RUMBLING enters a contest by (data/world/rumbling.json): the
// regulation check, rentals and the rankers' contests.

#include "phantomcrash/contests.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/part_stats.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"

#include <vector>

namespace phantomcrash {

// A class's maker rule allows any maker outside 0 to 2 (0x0137d6).
inline constexpr int regulation_makers = 3;

// How each area's day roster is drawn (0x045270, 0x045a68; "roster").
struct RosterRules {
  int max_size{6}; // the block's loadouts, the player's place among them
  // The port's live rumble (rumbling_world.hpp): a contest with a roster
  // has this many places instead, the player's among them; 0 keeps the
  // original's. The settings file's "rumble_robots" sets it.
  int live_size{11};
  // Once reserve_after members are drawn, a draw takes the extra team when
  // a number from 0 to 1 reaches reserve_share (0x17accc); otherwise the
  // unbeaten when it is below unbeaten_share (0x17cd14), else the beaten.
  float reserve_share{0.5F};
  float unbeaten_share{0.7F};
  // Classes whose champion joins without the contest won (0x16, 0x17).
  std::vector<int> champion_classes;
  // Contests that have no roster once won (0x30).
  std::vector<int> closed_once_won;
  // Once the game is cleared, cleared_contest is drawn as one of
  // cleared_picks at random (0x34, the table at 0x203654).
  int cleared_contest{no_contest};
  std::vector<int> cleared_picks;
  // The bounty grows by level / bounty_level_scale (0x0456b7, 0x19f880).
  float bounty_level_scale{64.0F};
};

struct RumblingRules {
  std::vector<int> legs_types; // by legs model (0x0823cb)
  std::vector<int> rental_classes;
  std::vector<int> rental_robots;
  // Each area's Area Ranker contest and the class leaders' contests that
  // open it, by area.
  struct AreaRanker {
    int contest{no_contest};
    std::vector<int> leaders;
    int entrant{-1}; // whom WIRE HEADS DATA shows in the contest (0x013f5a)
  };
  std::vector<AreaRanker> area_rankers;
  int first_ranker{no_contest};
  int first_ranker_needs{3};
  int first_ranker_entrant{-1};
  RosterRules roster;
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] RumblingRules parse_rumbling_rules(const json::JsonValue &document);

enum class RankerContest { none, area, first };

// 0x09700f: whether `contest` is `area`'s Area Ranker contest or the First
// Ranker's.
[[nodiscard]] RankerContest ranker_contest(const RumblingRules &rules, int area, int contest);
// 0x096fd0 and 0x096ff3: whether the ranker's contest is open to the player.
[[nodiscard]] bool ranker_open(const RumblingRules &rules, RankerContest kind, int area,
                               const std::vector<int> &won);

// 0x0138da.
[[nodiscard]] bool rental_allowed(const RumblingRules &rules, int contest_class);

// What the ridden Scoobee needs for a class besides the fee (0x0137d6).
struct RegulationCheck {
  const RumblingRules &rules;
  const PartTables &parts;
  const GarageTables &garage;
  const PartStatsTable &stats;
  // Tools > Cheats: any contest held may be entered, its rankers' and
  // rentals' too, by any ridden Scoobee with the fee (a port addition).
  bool open_contests{};
};

// A ridden Scoobee with a body, legs and a CHIP (0x01d4a4).
[[nodiscard]] bool riding_ready(const QuestSave &quest);
// The fee in hand, a Scoobee ridden and the class's maker, legs and other
// rule met by it (only the first two under open_contests).
[[nodiscard]] bool meets_regulation(const RegulationCheck &check, const ContestClass &rules,
                                    const QuestSave &quest);

} // namespace phantomcrash
