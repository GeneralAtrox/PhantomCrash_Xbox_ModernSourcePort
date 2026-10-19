// Port addition to BUY CHIP: a boss beaten sells its CHIP (data/tables/
// added_chips.json "boss_shop") on the days its contest is held.

#include "phantomcrash/chip_shop.hpp"

#include "phantomcrash/contests.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

bool contains(const std::vector<int> &values, const int value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

// The calendar's own contests, not the cleared game's swap (contest_on): the
// First Ranker's contest is won by clearing the game, which swaps it away.
bool held_on(const ContestTables &contests, const int day, const std::vector<int> &listed) {
  for (int venue = 0; venue < calendar_venues; ++venue) {
    if (contains(listed, contest_on(contests, day, venue, false))) {
      return true;
    }
  }
  return false;
}

} // namespace

void add_boss_chips(std::vector<ChipOffer> &offers, const PartTables &parts,
                    const GarageTables &garage, const ContestTables &contests, const int day,
                    const std::vector<int> &won_contests) {
  for (const BossChip &boss : garage.boss_chips) {
    const ChipRecord *chip = parts.chip(boss.id);
    const bool offered = std::any_of(offers.begin(), offers.end(), [&boss](const ChipOffer &offer) {
      return offer.chip == boss.id;
    });
    const auto won = [&won_contests](const int contest) { return contains(won_contests, contest); };
    if (chip == nullptr || offered || !held_on(contests, day, boss.contests) ||
        std::none_of(boss.contests.begin(), boss.contests.end(), won)) {
      continue;
    }
    offers.push_back({boss.id, boss.level, chip->name,
                      part_price(parts, garage, PartKind::chip, boss.id, boss.level)});
  }
}

} // namespace phantomcrash
