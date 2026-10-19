#pragma once

// Tools > Cheats' one-off changes to a game (a port addition). Its switches
// are CheatOptions (settings.hpp); data/debug/cheats.json lists the robots
// and how much of each part the changes give.

#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sleep.hpp"

#include <vector>

namespace phantomcrash {

enum class CheatAction { fill_hangars, all_items, go_to_day };

struct CheatRequest {
  CheatAction action{CheatAction::fill_hangars};
  int day{}; // go_to_day's, counted from 0

  bool operator==(const CheatRequest &) const = default;
};

struct CheatTables {
  // The robot records FILL HANGARS puts in the empty hangars, in order: one
  // for each body model.
  std::vector<int> hangar_robots;
  // The loose copies ALL ITEMS keeps of each weapon (one for each side) and
  // of each other part and module.
  int weapon_copies{2};
  int part_copies{1};
  // The stock it raises the garage to, so everything fits.
  int stock_size{1024};
};

// Missing members keep their defaults.
[[nodiscard]] CheatTables parse_cheat_tables(const json::JsonValue &document);

// Each listed robot whose body model no hangar holds yet goes into the next
// empty hangar, which the player then owns. Returns how many were fitted.
int fill_hangars(QuestSave &quest, const CheatTables &cheats, const PartTables &parts,
                 const GarageTables &garage);

// Tops the loose stock up to the copies of every distinct part and module:
// one record for each body, legs and weapon model (a shop's where there is
// one, else the enemy's or boss's own) and for each CHIP and module name.
// Not a weapon without a model, which is a blast's. Loose copies of a same
// part count. Owns every paint, emblem and of `track_count` music tracks.
void add_all_items(QuestSave &quest, const CheatTables &cheats, const PartTables &parts,
                   const GarageTables &garage, int track_count);

// Sleeps the nights until the calendar shows `day` (counted from 0), as the
// garage's SLEEP does each night; none when it already does.
void go_to_day(QuestSave &quest, int day, const SleepScript &sleep);

} // namespace phantomcrash
