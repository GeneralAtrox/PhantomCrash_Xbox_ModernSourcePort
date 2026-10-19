// Parsing of tables/town.json's contests and calendar.

#include "phantomcrash/contests.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::required;

int integer(const json::JsonValue &value) {
  return static_cast<int>(value.number());
}

std::vector<int> integers(const json::JsonValue &value) {
  std::vector<int> out;
  for (const auto &item : value.array()) {
    out.push_back(integer(item));
  }
  return out;
}

template <typename T> const T *at(const std::vector<T> &items, const int index) noexcept {
  return index >= 0 && index < static_cast<int>(items.size())
             ? &items[static_cast<std::size_t>(index)]
             : nullptr;
}

} // namespace

const Team *ContestTables::team(const int index) const noexcept {
  return at(teams, index);
}

const Entrant *ContestTables::entrant(const int index) const noexcept {
  return at(entrants, index);
}

const Contest *ContestTables::contest(const int index) const noexcept {
  return index >= 0 && index < static_cast<int>(contests.size())
             ? &contests[static_cast<std::size_t>(index)]
             : nullptr;
}

const ContestClass *ContestTables::contest_class(const int index) const noexcept {
  return index >= 0 && index < static_cast<int>(classes.size())
             ? &classes[static_cast<std::size_t>(index)]
             : nullptr;
}

ContestTables parse_contest_tables(const json::JsonValue &document) {
  ContestTables tables;
  for (const auto &value : required(document, "classes").array()) {
    ContestClass entry;
    entry.name = required(value, "name").string();
    entry.regulation = value.string_or("regulation", "");
    entry.fee = static_cast<std::int64_t>(value.number_or("fee", 0));
    entry.maker = static_cast<int>(value.number_or("maker", 7));
    if (const auto *legs = value.find("legs")) {
      entry.legs = integers(*legs);
    }
    entry.other = static_cast<int>(value.number_or("other", -1));
    if (const auto *weight = value.find("weight")) {
      const auto &range = weight->array();
      for (std::size_t end = 0; end < entry.weight.size() && end < range.size(); ++end) {
        entry.weight[end] = static_cast<float>(range[end].number());
      }
    }
    tables.classes.push_back(std::move(entry));
  }
  for (const auto &value : required(document, "contests").array()) {
    Contest entry;
    entry.name = required(value, "name").string();
    entry.contest_class = integer(required(value, "class"));
    entry.night = value.boolean_or("night", false);
    if (const auto *levels = value.find("levels")) {
      const auto range = integers(*levels);
      for (std::size_t end = 0; end < entry.levels.size() && end < range.size(); ++end) {
        entry.levels[end] = range[end];
      }
    }
    entry.reserve_after = static_cast<int>(value.number_or("reserve_after", 0));
    entry.roster_size = static_cast<int>(value.number_or("roster_size", 0));
    entry.extra_team = static_cast<int>(value.number_or("extra_team", -1));
    if (const auto *teams = value.find("teams")) {
      entry.teams = integers(*teams);
    }
    entry.champion = static_cast<int>(value.number_or("champion", -1));
    if (const auto *portraits = value.find("champion_portraits")) {
      for (const auto &portrait : portraits->array()) {
        if (portrait.is_object() && portrait.find("image") != nullptr &&
            portrait.find("image")->is_string()) {
          entry.champion_portraits.push_back(parse_sprite(portrait));
        }
      }
    }
    tables.contests.push_back(std::move(entry));
  }
  for (const auto &value : required(document, "calendar").array()) {
    CalendarDay day;
    const auto venues = integers(required(value, "venues"));
    for (std::size_t venue = 0; venue < day.venues.size() && venue < venues.size(); ++venue) {
      day.venues[venue] = venues[venue];
    }
    const auto copy_into = [&](const char *key, auto &into) {
      if (const auto *found = value.find(key)) {
        const auto lineups = integers(*found);
        for (std::size_t k = 0; k < into.size() && k < lineups.size(); ++k) {
          into[k] = lineups[k];
        }
      }
    };
    copy_into("dealers", day.dealers);
    copy_into("modules", day.modules);
    day.chips = static_cast<int>(value.number_or("chips", -1));
    day.used_svs = static_cast<int>(value.number_or("used_svs", -1));
    tables.calendar.push_back(day);
  }
  if (const auto *teams = document.find("teams")) {
    for (const auto &value : teams->array()) {
      Team team;
      team.leader = static_cast<int>(value.number_or("leader", -1));
      if (const auto *members = value.find("members")) {
        const auto ids = integers(*members);
        for (std::size_t k = 0; k < team.members.size() && k < ids.size(); ++k) {
          team.members[k] = ids[k];
        }
      }
      tables.teams.push_back(team);
    }
  }
  if (const auto *entrants = document.find("entrants")) {
    for (const auto &value : entrants->array()) {
      Entrant entrant;
      entrant.pilot = static_cast<int>(value.number_or("pilot", -1));
      entrant.robot = static_cast<int>(value.number_or("robot", -1));
      entrant.level = static_cast<int>(value.number_or("level", 0));
      entrant.ai_profile =
          static_cast<int>(value.number_or("ai_profile", value.number_or("word_4", 4)));
      tables.entrants.push_back(entrant);
    }
  }
  if (tables.calendar.size() != calendar_days) {
    throw std::runtime_error("the calendar must have 60 days");
  }
  return tables;
}

int contest_on(const ContestTables &tables, const int day, const int venue,
               const bool game_cleared) {
  if (venue < 0 || venue >= calendar_venues || tables.calendar.empty()) {
    return no_contest;
  }
  const int days = static_cast<int>(tables.calendar.size());
  const int index = ((day % days) + days) % days;
  const int contest =
      tables.calendar[static_cast<std::size_t>(index)].venues[static_cast<std::size_t>(venue)];
  if (game_cleared && contest == tables.cleared_swap_from) {
    return tables.cleared_swap_to;
  }
  return contest;
}

} // namespace phantomcrash
