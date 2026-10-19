#include "desktop_coop_matches.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {

void CoopMatch::collect_rewards() {
  const auto participant = [&](int actor) {
    return actor >= 0 &&
           std::ranges::any_of(players, [actor](const Participant &p) { return p.actor == actor; });
  };
  const auto cash = [&](int owner, std::int64_t amount) {
    if (!participant(owner)) {
      return;
    }
    for (auto &player : players) {
      if (player.actor >= 0 && (player.actor == owner || options.share_cash)) {
        player.bounty = std::min(money_limit, player.bounty + amount);
      }
    }
  };
  // Called once per simulation tick, before the roster replaces downed robots.
  for (const auto &event : battle->events()) {
    if (event.kind == BattleEventKind::carrier_down ||
        event.kind == BattleEventKind::wreck_destroyed || event.kind == BattleEventKind::supply ||
        event.kind == BattleEventKind::object_reward || event.kind == BattleEventKind::hit) {
      if (std::isfinite(event.amount) && event.amount > 0) {
        cash(event.robot, static_cast<std::int64_t>(std::min<double>(event.amount, money_limit)));
      }
      continue;
    }
    if (event.kind != BattleEventKind::destroyed || event.robot <= 0 ||
        event.robot >= static_cast<int>(battle->robots().size())) {
      continue;
    }
    const auto &victim = battle->robots()[event.robot];
    if (victim.piloted) {
      continue;
    }
    const auto contributed = [&](int actor) {
      return actor >= 0 && actor < static_cast<int>(victim.damage_from.size()) &&
             victim.damage_from[actor] > 0;
    };
    if (options.share_shootdowns &&
        std::ranges::any_of(players, [&](const Participant &p) { return contributed(p.actor); })) {
      for (const auto &player : players) {
        // Battle already credits every contributor, even when an NPC did most damage.
        if (player.actor >= 0 && !contributed(player.actor)) {
          ++battle->robot(player.actor).kills;
        }
      }
    }
    const auto &roster = simulation_save.rosters[area].members;
    const auto place = static_cast<std::size_t>(event.robot - 1);
    if (place >= roster.size() || !participant(victim.shot_by)) {
      continue;
    }
    const auto &member = roster[place];
    const auto bounty = std::clamp(world->coop_bounty(simulation_save, member.entrant),
                                   std::int64_t{0}, money_limit);
    cash(victim.shot_by, bounty * std::clamp(options.kill_ny_percent, 0, 1000) / 100);
    const bool boss =
        member.source == RosterSource::leader || member.source == RosterSource::champion;
    for (auto &player : players) {
      if (player.actor < 0 ||
          (player.actor != victim.shot_by && !(boss && options.share_boss_clears))) {
        continue;
      }
      player.beaten.push_back(beaten_key(member.team, member.place));
      if (member.source == RosterSource::champion) {
        player.won = true;
      }
    }
  }
}

} // namespace phantomcrash
