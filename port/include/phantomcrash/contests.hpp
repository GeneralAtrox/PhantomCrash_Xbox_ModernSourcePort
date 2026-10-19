#pragma once

// The contests of the town and the calendar they are held on
// (tables/town.json, converted from the XBE; see
// src/phantomcrash_research/town_tables.py).

#include "phantomcrash/json.hpp"
#include "phantomcrash/sprite_layer.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace phantomcrash {

// A contest class (0x24d108): its name, its regulation and play fee.
struct ContestClass {
  std::string name;
  std::string regulation; // the text shown for any of its rules
  std::int64_t fee{};     // NY
  int maker{7};           // the maker a robot must be; 7 for any
  std::vector<int> legs;  // the leg types allowed; empty for any
  int other{-1};          // 0 rentals only, 1 no O.C modules, -1 none
  // The robot's weight range for the entrants (+0xc, +0x10, 0x046411);
  // class_no_weight_limit for none.
  std::array<float, 2> weight{-1.0F, -1.0F};
};

// A class's maker rule for any maker, and its other rules (+0x14, +0x28).
inline constexpr int class_any_maker = 7;
inline constexpr int class_rentals_only = 0;
inline constexpr int class_no_overclock = 1;
inline constexpr float class_no_weight_limit = -1.0F;

// A contest (0x24b388).
struct Contest {
  std::string name;
  int contest_class{};
  bool night{};
  // The entrants' levels (+0xc, +0x10) the roster takes (0x046411).
  std::array<int, 2> levels{0, 99};
  // The members drawn before the extra team may be (+0x14), and the
  // roster's size less the player's place (+0x18, 0x045270).
  int reserve_after{};
  int roster_size{};
  int extra_team{-1};     // +0x1c: a team whose members all join the reserve
  std::vector<int> teams; // +0x20: the teams entered (ContestTables::teams)
  int champion{-1};       // +0x40: an entrant (ContestTables::entrants)
  // The champion's two portraits (RECORDS), when it has one.
  std::vector<Sprite> champion_portraits;
};

// A team (0x24d5d8): its leader and members, entrants or -1.
inline constexpr std::size_t team_members = 8;
struct Team {
  int leader{-1};
  std::array<int, team_members> members{-1, -1, -1, -1, -1, -1, -1, -1};
};

// An entrant (0x1b1008, 0x05fa5e): a pilot (tables/parts.json pilots) on a
// robot (tables/garage.json robots) at a level.
struct Entrant {
  int pilot{-1};
  int robot{-1};
  int level{};
  int ai_profile{4}; // entrant +4; loadout +0x5a
};

// The schedule's three venues (rows), and a fourth the calendar also has.
constexpr int schedule_venues = 3;
constexpr int calendar_venues = 3;
constexpr int calendar_days = 60;
constexpr int no_contest = -1;

struct CalendarDay {
  std::array<int, calendar_venues> venues{no_contest, no_contest, no_contest};
  // The day's lineup (tables/garage.json dealer_lineups) of each dealer, by
  // its index (American Stars 0, Kojima 1, Ventuno 2); -1 when none.
  std::array<int, 3> dealers{-1, -1, -1};
  // Wild Machines' stock that day (tables/garage.json): the CHIP lineup,
  // the module lineup of each category and the used SV lineup; -1 none.
  int chips{-1};
  std::array<int, 7> modules{-1, -1, -1, -1, -1, -1, -1};
  int used_svs{-1};
};

struct ContestTables {
  std::vector<ContestClass> classes;
  std::vector<Contest> contests;
  std::vector<CalendarDay> calendar; // calendar_days of them
  std::vector<Team> teams;
  std::vector<Entrant> entrants;
  // 0x06ce91: once the game is cleared, this contest is held as the other.
  int cleared_swap_from{0x31};
  int cleared_swap_to{0x34};

  [[nodiscard]] const Contest *contest(int index) const noexcept;
  [[nodiscard]] const ContestClass *contest_class(int index) const noexcept;
  [[nodiscard]] const Team *team(int index) const noexcept;
  [[nodiscard]] const Entrant *entrant(int index) const noexcept;
};

// Throws std::runtime_error when a value is missing or wrong.
[[nodiscard]] ContestTables parse_contest_tables(const json::JsonValue &document);

// The contest held on `day` (any day; the calendar repeats every 60) at
// `venue`, or no_contest (0x06ce91).
[[nodiscard]] int contest_on(const ContestTables &tables, int day, int venue, bool game_cleared);

} // namespace phantomcrash
