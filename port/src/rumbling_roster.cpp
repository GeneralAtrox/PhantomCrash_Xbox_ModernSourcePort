#include "phantomcrash/rumbling_roster.hpp"

#include "phantomcrash/robot_load.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace phantomcrash {
namespace {

bool contains(const std::vector<int> &values, const int value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T> const T *at(const std::vector<T> &items, const int index) {
  return index >= 0 && index < static_cast<int>(items.size())
             ? &items[static_cast<std::size_t>(index)]
             : nullptr;
}

// A robot record as the load works it out (0x075f93 reads the same parts).
RobotLoadout record_loadout(const RobotRecord &robot) {
  RobotLoadout loadout;
  if (const auto &chip = robot.part(PartSlot::chip)) {
    loadout.chip = chip->id;
    loadout.chip_level = chip->level;
  }
  if (const auto &body = robot.part(PartSlot::body)) {
    loadout.body = body->id;
    loadout.body_level = body->level;
  }
  if (const auto &legs = robot.part(PartSlot::legs)) {
    loadout.legs = legs->id;
    loadout.legs_level = legs->level;
  }
  for (const PartSlot slot : {PartSlot::right_arm, PartSlot::left_arm, PartSlot::right_shoulder,
                              PartSlot::left_shoulder}) {
    if (const auto &weapon = robot.part(slot)) {
      const bool arm = slot == PartSlot::right_arm || slot == PartSlot::left_arm;
      loadout.weapons.push_back(
          {arm ? WeaponTable::arm : WeaponTable::shoulder, weapon->id, weapon->level});
    }
  }
  return loadout;
}

bool within(const float value, const std::array<float, 2> &range) {
  if (range[0] != class_no_weight_limit && range[0] > value) {
    return false;
  }
  return range[1] == class_no_weight_limit || value <= range[1];
}

// The draws' three pools (0x045a68): the unbeaten, the beaten and the
// extra team's reserve, each with how many it has given.
struct Pool {
  std::vector<RosterMember> left;
  int drawn{};
};
enum PoolIndex : std::size_t { unbeaten, beaten, reserve };
using Pools = std::array<Pool, 3>;

std::optional<RosterMember> draw(Pools &pools, const RosterRules &rules, const int reserve_after,
                                 const RosterRandom &random) {
  std::optional<std::size_t> from;
  const bool none = pools[unbeaten].left.empty() && pools[beaten].left.empty();
  if (none || (reserve_after <= pools[unbeaten].drawn + pools[beaten].drawn &&
               random() >= rules.reserve_share)) {
    if (!pools[reserve].left.empty()) {
      from = reserve;
    }
  } else if (pools[unbeaten].left.empty()) {
    from = beaten;
  } else if (pools[beaten].left.empty()) {
    from = unbeaten;
  } else {
    from = random() >= rules.unbeaten_share ? beaten : unbeaten;
  }
  if (!from) {
    // The native population setting requests occupied places. An empty
    // reserve draw must not discard a place while regular entrants remain.
    if (rules.live_size > 0 && !none) {
      from = pools[unbeaten].left.empty() ? beaten : unbeaten;
    } else {
      return std::nullopt;
    }
  }
  Pool &pool = pools[*from];
  const float reach = static_cast<float>(pool.left.size() - 1) * random();
  const auto index =
      std::min(static_cast<std::size_t>(std::max(reach, 0.0F)), pool.left.size() - 1);
  const RosterMember member = pool.left[index];
  pool.left.erase(pool.left.begin() + static_cast<std::ptrdiff_t>(index));
  ++pool.drawn;
  return member;
}

} // namespace

bool entrant_fits(const RegulationCheck &check, const ContestTables &contests, const int contest,
                  const int entrant) {
  const Entrant *who = contests.entrant(entrant);
  const Contest *held = contests.contest(contest);
  if (who == nullptr || held == nullptr || who->level < held->levels[0] ||
      who->level > held->levels[1]) {
    return false;
  }
  const ContestClass *rules = contests.contest_class(held->contest_class);
  const RobotRecord *robot = at(check.garage.robots, who->robot);
  if (rules == nullptr || robot == nullptr) {
    return false;
  }
  const RobotLoadout loadout = record_loadout(*robot);
  if (rules->maker != class_any_maker) {
    const ArmourRecord *body = at(check.parts.bodies, loadout.body);
    if (body == nullptr || body->maker != rules->maker) {
      return false;
    }
  }
  if (!rules->legs.empty()) {
    const ArmourRecord *legs = at(check.parts.legs, loadout.legs);
    const int *type = legs != nullptr ? at(check.rules.legs_types, legs->model) : nullptr;
    if (type == nullptr || !contains(rules->legs, *type)) {
      return false;
    }
  }
  float weight = load_stats(check.parts, loadout).weight;
  int active_frames = 0;
  for (const int id : robot->modules) {
    if (const ModuleRecord *module = at(check.garage.modules, id)) {
      weight += module->weight;
      active_frames += module->active_frames;
    }
  }
  if (!within(weight, rules->weight)) {
    return false;
  }
  return rules->other != class_no_overclock || active_frames <= 0;
}

namespace {

// The pools the draws for `drawn_for` take from (0x045270); false when the
// contest has no roster.
bool fill_pools(const RegulationCheck &check, const ContestTables &contests, const QuestSave &quest,
                const int drawn_for, Pools &pools) {
  const RosterRules &rules = check.rules.roster;
  const Contest *held = contests.contest(drawn_for);
  const bool won = contains(quest.won_contests, drawn_for);
  if (held == nullptr || (won && contains(rules.closed_once_won, drawn_for))) {
    return false;
  }
  // An empty place counts as beaten (0x0bac5a marks it 0xff).
  const auto is_beaten = [&quest](const int team, const int place, const int id) {
    return id < 0 || contains(quest.beaten, beaten_key(team, place));
  };
  const auto members_beaten = [&](const int index, const Team &team) {
    for (std::size_t m = 0; m < team.members.size(); ++m) {
      if (!is_beaten(index, static_cast<int>(m) + 1, team.members[m])) {
        return false;
      }
    }
    return true;
  };
  bool all_beaten = true;
  for (const int index : held->teams) {
    const Team *team = contests.team(index);
    if (team == nullptr) {
      continue;
    }
    for (std::size_t m = 0; m < team->members.size(); ++m) {
      const int id = team->members[m];
      const int place = static_cast<int>(m) + 1;
      if (entrant_fits(check, contests, drawn_for, id)) {
        pools[is_beaten(index, place, id) ? beaten : unbeaten].left.push_back(
            {id, RosterSource::member, index, place});
      }
    }
    const bool done = members_beaten(index, *team);
    if (done && entrant_fits(check, contests, drawn_for, team->leader)) {
      pools[is_beaten(index, 0, team->leader) ? beaten : unbeaten].left.push_back(
          {team->leader, RosterSource::leader, index, 0});
    }
    all_beaten = all_beaten && done && is_beaten(index, 0, team->leader);
  }
  // 0x0459ed: the champion comes out once every team is beaten.
  const bool champion_class = contains(rules.champion_classes, held->contest_class);
  if (held->champion >= 0 && (won || champion_class) && all_beaten) {
    pools[won ? beaten : unbeaten].left.push_back(
        {held->champion, RosterSource::champion, drawn_for, 0});
  }
  if (const Team *extra = contests.team(held->extra_team)) {
    for (std::size_t m = 0; m < extra->members.size(); ++m) {
      if (entrant_fits(check, contests, drawn_for, extra->members[m])) {
        pools[reserve].left.push_back(
            {extra->members[m], RosterSource::extra, held->extra_team, static_cast<int>(m) + 1});
      }
    }
  }
  return true;
}

} // namespace

int roster_places(const Contest &contest, const RosterRules &rules) {
  const int places = std::min(contest.roster_size + 1, rules.max_size);
  return places > 1 && rules.live_size > 0 ? rules.live_size : places;
}

AreaRoster draw_roster(const RegulationCheck &check, const ContestTables &contests,
                       const QuestSave &quest, const int contest, const RosterRandom &random) {
  AreaRoster roster;
  roster.day = quest.day;
  roster.contest = contest;
  const RosterRules &rules = check.rules.roster;
  int drawn_for = contest;
  if (quest.cleared && contest == rules.cleared_contest && !rules.cleared_picks.empty()) {
    const auto count = rules.cleared_picks.size();
    const auto pick = static_cast<std::size_t>(random() * static_cast<float>(count));
    drawn_for = rules.cleared_picks[pick < count ? pick : 0];
  }
  roster.drawn_for = drawn_for;
  Pools pools;
  if (!fill_pools(check, contests, quest, drawn_for, pools)) {
    return roster;
  }
  const Contest &held = *contests.contest(drawn_for);
  const int places = roster_places(held, rules);
  for (int place = 1; place < places; ++place) {
    const auto member = rules.live_size > 0
                            ? draw_replacement(check, contests, quest, roster, -1, random)
                            : draw(pools, rules, held.reserve_after, random);
    if (member) {
      roster.members.push_back(*member);
    }
  }
  return roster;
}

bool refresh_rosters(const RegulationCheck &check, const ContestTables &contests, QuestSave &quest,
                     const RosterRandom &random, const bool fill_vacancies) {
  bool changed = false;
  for (int area = 0; area < roster_areas; ++area) {
    const int contest = contest_on(contests, quest.day, area, quest.cleared);
    AreaRoster &roster = quest.rosters[static_cast<std::size_t>(area)];
    if (roster.day != quest.day || roster.contest != contest) {
      roster = draw_roster(check, contests, quest, contest, random);
      changed = true;
    }
    if (fill_vacancies && check.rules.roster.live_size > 0) {
      const Contest *held = contests.contest(roster.drawn_for);
      const int places = held != nullptr ? roster_places(*held, check.rules.roster) : 0;
      while (static_cast<int>(roster.members.size()) + 1 < places) {
        const auto member = draw_replacement(check, contests, quest, roster, -1, random);
        if (!member) {
          break; // No distinct eligible entrant remains.
        }
        roster.members.push_back(*member);
        changed = true;
      }
    }
  }
  return changed;
}

RosterCard roster_card(const RegulationCheck &check, const ContestTables &contests,
                       const QuestSave &quest, const int entrant, const bool raised) {
  RosterCard card;
  card.entrant = entrant;
  const Entrant *who = contests.entrant(entrant);
  if (who == nullptr) {
    return card;
  }
  card.level = who->level;
  if (const PilotRecord *pilot = check.parts.pilot(who->pilot)) {
    const float bounty = pilot->bounty.value(card.level);
    card.bounty = static_cast<std::int64_t>(bounty);
    if (card.level > 0) {
      const float scale = static_cast<float>(card.level) / check.rules.roster.bounty_level_scale;
      card.bounty = static_cast<std::int64_t>(static_cast<float>(card.bounty) * (scale + 1.0F));
    }
  }
  if (raised && quest.cleared) {
    card.level = std::min(card.level + quest.laps, part_level_max);
  }
  return card;
}

std::vector<RosterCard> wire_heads_cards(const RegulationCheck &check,
                                         const ContestTables &contests, const QuestSave &quest,
                                         const int area, const int contest) {
  std::vector<RosterCard> cards;
  const RumblingRules &rules = check.rules;
  int ranker = -1;
  switch (ranker_contest(rules, area, contest)) {
  case RankerContest::area:
    ranker = rules.area_rankers[static_cast<std::size_t>(area)].entrant;
    break;
  case RankerContest::first:
    ranker = rules.first_ranker_entrant;
    break;
  case RankerContest::none:
    break;
  }
  if (ranker >= 0) {
    cards.push_back(roster_card(check, contests, quest, ranker, false));
    return cards;
  }
  if (area < 0 || area >= roster_areas) {
    return cards;
  }
  const AreaRoster &roster = quest.rosters[static_cast<std::size_t>(area)];
  if (roster.day != quest.day || roster.contest != contest) {
    return cards;
  }
  for (const RosterMember &member : roster.members) {
    cards.push_back(roster_card(check, contests, quest, member.entrant, true));
  }
  return cards;
}

} // namespace phantomcrash
