#include "phantomcrash/rumbling_world.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {

// Every weapon it still has is out of rounds, and none recharges.
bool out_of_ammo(const Combatant &robot) {
  bool armed = false;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (robot.weapons[slot] == nullptr) {
      continue;
    }
    armed = true;
    const Magazine &magazine = robot.magazines[slot];
    if (!robot.lost[slot] && (!magazine.empty() || magazine.recharging())) {
      return false;
    }
  }
  return armed;
}

} // namespace

std::vector<RosterMember> contest_opponents(const RegulationCheck &check,
                                            const ContestTables &contests, const int contest) {
  std::vector<RosterMember> opponents;
  const Contest *held = contests.contest(contest);
  if (!held) {
    return opponents;
  }
  const auto add = [&](const int entrant, const RosterSource source, const int team,
                       const int place) {
    if (entrant_fits(check, contests, contest, entrant) &&
        std::ranges::none_of(opponents,
                             [&](const auto &other) { return other.entrant == entrant; })) {
      opponents.push_back({entrant, source, team, place});
    }
  };
  for (const int index : held->teams) {
    if (const Team *team = contests.team(index)) {
      for (std::size_t m = 0; m < team->members.size(); ++m) {
        add(team->members[m], RosterSource::member, index, static_cast<int>(m) + 1);
      }
      add(team->leader, RosterSource::leader, index, 0);
    }
  }
  if (const Team *team = contests.team(held->extra_team)) {
    for (std::size_t m = 0; m < team->members.size(); ++m) {
      add(team->members[m], RosterSource::extra, held->extra_team, static_cast<int>(m) + 1);
    }
  }
  return opponents;
}

std::optional<RosterMember> draw_replacement(const RegulationCheck &check,
                                             const ContestTables &contests, const QuestSave &quest,
                                             const AreaRoster &roster, const int leaving,
                                             const RosterRandom &random) {
  const Contest *held = contests.contest(roster.drawn_for);
  const auto contains = [](const auto &list, const int id) {
    return std::ranges::find(list, id) != list.end();
  };
  const bool won = contains(quest.won_contests, roster.drawn_for);
  if (!held || (won && contains(check.rules.roster.closed_once_won, roster.drawn_for))) {
    return std::nullopt;
  }
  const auto opponents = contest_opponents(check, contests, roster.drawn_for);
  auto remaining = opponents;
  std::erase_if(remaining,
                [&](const auto &member) { return contains(roster.eliminated, member.entrant); });
  const auto occupied = [&](const int entrant) {
    for (std::size_t i = 0; i < roster.members.size(); ++i) {
      if (static_cast<int>(i) != leaving && roster.members[i].entrant == entrant) {
        return true;
      }
    }
    return false;
  };
  if (remaining.empty()) {
    if (held->champion >= 0 && !occupied(held->champion) &&
        (won || contains(check.rules.roster.champion_classes, held->contest_class))) {
      return RosterMember{held->champion, RosterSource::champion, roster.drawn_for, 0};
    }
  }
  std::array<std::vector<RosterMember>, 3> pools;
  std::vector<RosterMember> respawns;
  for (const auto &member : opponents) {
    if (occupied(member.entrant)) {
      continue;
    }
    if (member.source == RosterSource::leader &&
        std::ranges::any_of(remaining, [&](const auto &other) {
          return other.team == member.team && other.source == RosterSource::member;
        })) {
      continue;
    }
    if (contains(roster.eliminated, member.entrant)) {
      respawns.push_back(member);
      continue;
    }
    const int pool = member.source == RosterSource::extra                            ? 2
                     : contains(quest.beaten, beaten_key(member.team, member.place)) ? 1
                                                                                     : 0;
    pools[pool].push_back(member);
  }
  int pool = 0;
  if ((!pools[2].empty() && static_cast<int>(roster.members.size()) >= held->reserve_after &&
       random() >= check.rules.roster.reserve_share) ||
      (pools[0].empty() && pools[1].empty())) {
    pool = 2;
  } else if (pools[0].empty() ||
             (!pools[1].empty() && random() >= check.rules.roster.unbeaten_share)) {
    pool = 1;
  }
  if (pools[pool].empty()) {
    // A first defeat advances the checklist, but does not remove the pilot
    // from the arena. Prefer unseen eligible opponents before repeat spawns.
    if (respawns.empty()) {
      return std::nullopt;
    }
    pools[pool] = std::move(respawns);
  }
  const auto count = pools[pool].size();
  const auto pick = std::min(
      static_cast<std::size_t>(std::clamp(random(), 0.0F, 1.0F) * static_cast<float>(count)),
      count - 1);
  return pools[pool][pick];
}

std::vector<int> replacement_entrants(const RegulationCheck &check, const ContestTables &contests,
                                      const QuestSave &, const AreaRoster &roster) {
  std::vector<int> entrants;
  for (const auto &member : contest_opponents(check, contests, roster.drawn_for)) {
    entrants.push_back(member.entrant);
  }
  // Leaders and the champion must be loaded before they become eligible mid-match.
  if (const Contest *held = contests.contest(roster.drawn_for); held && held->champion >= 0) {
    entrants.push_back(held->champion);
  }
  return entrants;
}

std::vector<RosterStatus> RumblingWorld::roster_status(const int area) const {
  std::vector<RosterStatus> status;
  if (area < 0 || area >= roster_areas) {
    return status;
  }
  const AreaRoster &roster = areas_[static_cast<std::size_t>(area)].roster;
  for (const auto &member : contest_opponents(check_, tables_.contests, roster.drawn_for)) {
    const Entrant *entrant = tables_.contests.entrant(member.entrant);
    const PilotRecord *pilot = entrant ? check_.parts.pilot(entrant->pilot) : nullptr;
    const bool eliminated =
        std::ranges::find(roster.eliminated, member.entrant) != roster.eliminated.end();
    status.push_back({pilot ? pilot->name : std::to_string(member.entrant), eliminated,
                      member.source, pilot ? pilot->tag : std::string{}});
  }
  if (const Contest *held = tables_.contests.contest(roster.drawn_for);
      held && held->champion >= 0) {
    const auto *entrant = tables_.contests.entrant(held->champion);
    const auto *pilot = entrant ? check_.parts.pilot(entrant->pilot) : nullptr;
    const bool eliminated =
        std::ranges::find(roster.eliminated, held->champion) != roster.eliminated.end();
    status.push_back({pilot ? pilot->name : std::to_string(held->champion), eliminated,
                      RosterSource::champion, pilot ? pilot->tag : std::string{}});
  }
  return status;
}

bool RumblingWorld::step(const int index, const QuestSave &quest, const float seconds,
                         const bool run) {
  Area &area = areas_[static_cast<std::size_t>(index)];
  Battle &battle = *area.battle;
  bool changed = run;
  if (run) {
    battle.tick({}, {}, seconds);
  }
  for (std::size_t place = 0; place < area.roster.members.size(); ++place) {
    RosterMember &member = area.roster.members[place];
    const int robot_index = static_cast<int>(place) + 1;
    Combatant &robot = battle.robot(robot_index);
    if (battle.cooperative() && robot.piloted) {
      continue;
    }
    if (robot.alive && out_of_ammo(robot)) {
      battle.take_out(robot_index); // nothing left to fight with: it leaves
      changed = true;
    }
    if (!robot.alive && !member.live.down) {
      changed = true;
      for (auto &other : area.roster.members) {
        if (other.live.arrival == waiting_for_opponent) {
          other.live.arrival = 0;
        }
      }
      member.live.down = true;
      member.live.arrival = live_.arrival_seconds;
      const bool player_defeat =
          lent_ == index && robot.shot_by >= 0 &&
          robot.shot_by < static_cast<int>(battle.robots().size()) &&
          (robot.shot_by == 0 || (battle.cooperative() && battle.robots()[robot.shot_by].piloted));
      if (player_defeat) {
        if (std::ranges::find(area.roster.eliminated, member.entrant) ==
            area.roster.eliminated.end()) {
          area.roster.eliminated.push_back(member.entrant);
        }
      }
      if (lent_ == index && robot.shot_by == 0) {
        area.player_kills.push_back(member);
        area.player_bounty +=
            roster_card(check_, tables_.contests, quest, member.entrant, true).bounty;
      }
      robot.shot_by = -1;
    }
    if (member.live.down) {
      if (member.live.arrival == waiting_for_opponent) {
        continue;
      }
      member.live.arrival = std::max(0.0F, member.live.arrival - seconds);
      if (member.live.arrival > 0.0F || robot.dying) {
        continue;
      }
      if (const auto next =
              draw_replacement(check_, tables_.contests, quest, area.roster,
                               static_cast<int>(place), [this] { return random_share(); })) {
        member = *next;
      } else {
        member.live.arrival = waiting_for_opponent;
        continue;
      }
      member.live = {};
      fit(index, place, quest);
      battle.bring_in(robot_index);
      battle.raise_shield(robot_index);
      ++area.changes;
      changed = true;
    }
    record(robot, member.live);
  }
  return changed;
}

} // namespace phantomcrash
