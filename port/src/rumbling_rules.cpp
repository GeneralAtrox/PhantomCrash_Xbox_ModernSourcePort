#include "phantomcrash/rumbling_rules.hpp"

#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/robot_totals.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

using json::required;

std::vector<int> integers(const json::JsonValue &value) {
  std::vector<int> out;
  for (const json::JsonValue &entry : value.array()) {
    out.push_back(static_cast<int>(entry.number()));
  }
  return out;
}

bool contains(const std::vector<int> &values, const int value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

const StockItem *fitted_item(const Garage &garage, const Hangar &hangar, const PartSlot slot) {
  const auto found = garage.stock.find(hangar.part(slot));
  return found != garage.stock.end() ? &found->second : nullptr;
}

template <typename Record>
const Record *record_at(const std::vector<Record> &records, const StockItem *item) {
  if (item == nullptr || item->id < 0 || item->id >= static_cast<int>(records.size())) {
    return nullptr;
  }
  return &records[static_cast<std::size_t>(item->id)];
}

RosterRules parse_roster(const json::JsonValue &value) {
  RosterRules roster;
  roster.max_size = static_cast<int>(value.number_or("max_size", roster.max_size));
  roster.live_size = static_cast<int>(value.number_or("live_size", roster.live_size));
  roster.reserve_share = static_cast<float>(value.number_or("reserve_share", roster.reserve_share));
  roster.unbeaten_share =
      static_cast<float>(value.number_or("unbeaten_share", roster.unbeaten_share));
  const auto list = [&value](const char *key) {
    const json::JsonValue *found = value.find(key);
    return found != nullptr ? integers(*found) : std::vector<int>{};
  };
  roster.champion_classes = list("champion_classes");
  roster.closed_once_won = list("closed_once_won");
  roster.cleared_contest = static_cast<int>(value.number_or("cleared_contest", no_contest));
  roster.cleared_picks = list("cleared_picks");
  roster.bounty_level_scale =
      static_cast<float>(value.number_or("bounty_level_scale", roster.bounty_level_scale));
  if (roster.max_size < 1 || roster.live_size < 0 || roster.bounty_level_scale <= 0.0F) {
    throw std::runtime_error(
        "roster: max_size and bounty_level_scale must be positive, live_size 0 or more");
  }
  return roster;
}

} // namespace

RumblingRules parse_rumbling_rules(const json::JsonValue &document) {
  try {
    RumblingRules rules;
    rules.legs_types = integers(required(document, "legs_types"));
    rules.rental_classes = integers(required(document, "rental_classes"));
    rules.rental_robots = integers(required(document, "rental_robots"));
    for (const json::JsonValue &entry : required(document, "area_rankers").array()) {
      rules.area_rankers.push_back({static_cast<int>(required(entry, "contest").number()),
                                    integers(required(entry, "leaders")),
                                    static_cast<int>(entry.number_or("entrant", -1))});
    }
    rules.first_ranker = static_cast<int>(document.number_or("first_ranker", no_contest));
    rules.first_ranker_needs =
        static_cast<int>(document.number_or("first_ranker_needs", rules.first_ranker_needs));
    rules.first_ranker_entrant = static_cast<int>(document.number_or("first_ranker_entrant", -1));
    if (const json::JsonValue *roster = document.find("roster")) {
      rules.roster = parse_roster(*roster);
    }
    return rules;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("rumbling.json: ") + error.what());
  }
}

RankerContest ranker_contest(const RumblingRules &rules, const int area, const int contest) {
  if (contest == no_contest) {
    return RankerContest::none;
  }
  if (contest == rules.first_ranker) {
    return RankerContest::first;
  }
  if (area >= 0 && area < static_cast<int>(rules.area_rankers.size()) &&
      rules.area_rankers[static_cast<std::size_t>(area)].contest == contest) {
    return RankerContest::area;
  }
  return RankerContest::none;
}

bool ranker_open(const RumblingRules &rules, const RankerContest kind, const int area,
                 const std::vector<int> &won) {
  if (kind == RankerContest::area) {
    const auto &leaders = rules.area_rankers[static_cast<std::size_t>(area)].leaders;
    return std::all_of(leaders.begin(), leaders.end(),
                       [&won](const int leader) { return contains(won, leader); });
  }
  if (kind == RankerContest::first) {
    const auto count = std::count_if(
        rules.area_rankers.begin(), rules.area_rankers.end(),
        [&won](const RumblingRules::AreaRanker &ranker) { return contains(won, ranker.contest); });
    return count >= rules.first_ranker_needs;
  }
  return true;
}

bool rental_allowed(const RumblingRules &rules, const int contest_class) {
  return contains(rules.rental_classes, contest_class);
}

bool riding_ready(const QuestSave &quest) {
  const int riding = quest.garage.riding;
  if (riding < 0 || riding >= garage_hangar_count) {
    return false;
  }
  const Hangar &hangar = quest.garage.hangars[static_cast<std::size_t>(riding)];
  return hangar.owned && hangar.complete();
}

bool meets_regulation(const RegulationCheck &check, const ContestClass &rules,
                      const QuestSave &quest) {
  if (quest.money < rules.fee || !riding_ready(quest)) {
    return false;
  }
  if (check.open_contests) {
    return true;
  }
  const Garage &garage = quest.garage;
  const Hangar &hangar = garage.hangars[static_cast<std::size_t>(garage.riding)];
  if (rules.maker >= 0 && rules.maker < regulation_makers) {
    const ArmourRecord *body =
        record_at(check.parts.bodies, fitted_item(garage, hangar, PartSlot::body));
    if (body == nullptr || body->maker != rules.maker) {
      return false;
    }
  }
  if (!rules.legs.empty()) {
    const ArmourRecord *legs =
        record_at(check.parts.legs, fitted_item(garage, hangar, PartSlot::legs));
    if (legs == nullptr || legs->model < 0 ||
        legs->model >= static_cast<int>(check.rules.legs_types.size()) ||
        !contains(rules.legs, check.rules.legs_types[static_cast<std::size_t>(legs->model)])) {
      return false;
    }
  }
  if (rules.other == class_rentals_only) {
    return false;
  }
  if (rules.other == class_no_overclock) {
    const RobotTotals totals = robot_totals(check.stats, check.parts, check.garage, garage, hangar);
    return totals.active_seconds <= 0.0;
  }
  return true;
}

} // namespace phantomcrash
