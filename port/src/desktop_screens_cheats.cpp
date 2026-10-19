// Tools > Cheats on the running game (docs/CHEATS.md): the switches reach
// the quest and the battle every tick; a one-off change waits for the next
// screen, so no screen's own copy of the quest writes over it.

#include "desktop_game_screens.hpp"
#include "desktop_screens.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <exception>
#include <utility>

namespace phantomcrash {

void ScreenRunner::set_cheats(const CheatOptions &cheats) {
  const bool richer = cheats.infinite_money && !cheats_.infinite_money;
  cheats_ = cheats;
  game_->set_open_contests(cheats.open_contests);
  if (richer && !cooperative()) {
    quest_.money = money_limit;
  }
}

void ScreenRunner::cheat(const CheatRequest &request) {
  if (cooperative()) {
    SDL_Log("Cheats change a solo game only");
    return;
  }
  pending_cheats_.push_back(request);
  // These screens keep no copy of the quest that the runner's is not, so
  // they open again on the changed game straight away.
  if (location_menus_.contains(name_) || name_ == "town_map") {
    const std::string before = place_before_;
    show(name_, false);
    place_before_ = before;
  }
}

std::vector<std::string> ScreenRunner::cheat_days() const {
  const ContestTables &contests = game_->contests();
  const RumblingRules &rules = game_->regulation_check().rules;
  const int days = game_->sleep_script().days_in_cycle;
  std::vector<std::string> labels;
  for (int day = 0; day < days; ++day) {
    char label[16];
    std::snprintf(label, sizeof(label), "Day %02d", day + 1);
    std::string entry = label;
    for (int venue = 0; venue < calendar_venues; ++venue) {
      const int held = contest_on(contests, day, venue, quest_.cleared);
      switch (ranker_contest(rules, venue, held)) {
      case RankerContest::first:
        entry += " - First Ranker";
        break;
      case RankerContest::area:
        entry += " - Area Ranker";
        break;
      case RankerContest::none:
        break;
      }
    }
    labels.push_back(std::move(entry));
  }
  return labels;
}

void ScreenRunner::apply_cheat_switches() {
  if (cooperative()) {
    return; // the session owns its campaigns
  }
  if (cheats_.infinite_money) {
    quest_.money = money_limit;
  }
  screen_->set_cheats(cheats_);
}

void ScreenRunner::apply_pending_cheats() {
  for (const CheatRequest &request : std::exchange(pending_cheats_, {})) {
    switch (request.action) {
    case CheatAction::fill_hangars: {
      const int fitted =
          fill_hangars(quest_, cheat_tables(), game_->part_tables(), game_->garage_tables());
      SDL_Log("Cheat: %d robots fitted into empty hangars", fitted);
      break;
    }
    case CheatAction::all_items:
      add_all_items(quest_, cheat_tables(), game_->part_tables(), game_->garage_tables(),
                    static_cast<int>(credits_.size()));
      SDL_Log("Cheat: stock holds %zu of %d entries", quest_.garage.stock.size(),
              quest_.garage.stock_size);
      break;
    case CheatAction::go_to_day:
      go_to_day(quest_, request.day, game_->sleep_script());
      SDL_Log("Cheat: day %d", quest_.day + 1);
      break;
    }
  }
}

const CheatTables &ScreenRunner::cheat_tables() {
  if (!cheat_tables_) {
    try {
      cheat_tables_ = parse_cheat_tables(roots_.document("debug/cheats.json"));
    } catch (const std::exception &error) {
      SDL_Log("debug/cheats.json: %s", error.what());
      cheat_tables_ = CheatTables{};
    }
  }
  return *cheat_tables_;
}

} // namespace phantomcrash
