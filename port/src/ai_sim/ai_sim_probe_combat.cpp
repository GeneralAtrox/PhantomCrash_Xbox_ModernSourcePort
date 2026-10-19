// The probe's combat accounting (ai_sim_probe.hpp): shots, hits, kills
// and damage, read from the battle's events and the victims' tallies.

#include "ai_sim_probe.hpp"

#include "ai_sim_world.hpp"

#include <algorithm>

namespace phantomcrash::ai_sim {

void Probe::read_events(const double t, std::vector<bool> &destroyed) {
  const Battle &battle = arena_.battle();
  for (const BattleEvent &event : battle.events()) {
    if (event.robot < 0 || event.robot >= static_cast<int>(tracks_.size())) {
      continue;
    }
    Track &track = tracks_[static_cast<std::size_t>(event.robot)];
    LifeStats *life = track.life ? &*track.life : nullptr;
    switch (event.kind) {
    case BattleEventKind::fired: {
      if (life == nullptr) {
        break;
      }
      ++life->shots;
      ++run_.shots;
      if (event.slot >= 0 && event.slot < static_cast<int>(weapon_slots)) {
        ++life->behaviour.slot_shots[static_cast<std::size_t>(event.slot)];
      }
      if (life->first_shot < 0) {
        life->first_shot = t - life->start;
      }
      if (run_.first_shot < 0) {
        run_.first_shot = t;
      }
      if (track.acquired_at >= 0) {
        life->reactions.push_back(t - track.acquired_at);
        track.acquired_at = -1;
      }
      track.last_fired = t;
      const Combatant &robot = battle.robots()[static_cast<std::size_t>(event.robot)];
      run_.player_shots_at += robot.ai.target == 0 && arena_.has_player() ? 1 : 0;
      break;
    }
    case BattleEventKind::hit:
      if (life != nullptr) {
        ++life->hits;
        ++run_.hits;
        if (life->first_hit < 0) {
          life->first_hit = t - life->start;
        }
      }
      break;
    case BattleEventKind::bingo: // the killer and everyone who helped
      if (life != nullptr) {
        ++life->assists;
      }
      break;
    case BattleEventKind::supply:
      if (life != nullptr) {
        ++life->pickups;
      }
      break;
    case BattleEventKind::destroyed: {
      destroyed[static_cast<std::size_t>(event.robot)] = true;
      const int killer = battle.robots()[static_cast<std::size_t>(event.robot)].shot_by;
      if (killer < 0 || killer >= static_cast<int>(tracks_.size())) {
        break;
      }
      ++run_.kills;
      if (run_.first_kill < 0) {
        run_.first_kill = t;
      }
      if (Track &by = tracks_[static_cast<std::size_t>(killer)]; by.life) {
        ++by.life->kills;
      }
      break;
    }
    default:
      break;
    }
  }
}

// Damage dealt, by the victims' running tallies by attacker; a killing
// blow clears the tally, so it is the victim's last armour, to its killer.
void Probe::read_damage(const std::vector<bool> &destroyed) {
  const auto &robots = arena_.battle().robots();
  for (std::size_t victim = 0; victim < robots.size(); ++victim) {
    const Combatant &robot = robots[victim];
    Track &track = tracks_[victim];
    const auto credit = [&](const std::size_t attacker, const double amount) {
      if (amount <= 0.0 || attacker >= tracks_.size() || attacker == victim) {
        return;
      }
      run_.damage += amount;
      if (LifeStats *life = tracks_[attacker].life ? &*tracks_[attacker].life : nullptr) {
        life->damage_dealt += amount;
        life->player_damage += victim == 0 ? amount : 0.0;
      }
      run_.player_damage_taken += victim == 0 ? amount : 0.0;
    };
    if (destroyed[victim] && track.alive) {
      if (robot.shot_by >= 0) {
        credit(static_cast<std::size_t>(robot.shot_by), track.hp);
      }
    } else {
      for (std::size_t attacker = 0; attacker < robot.damage_from.size(); ++attacker) {
        const float before = attacker < track.damage_from.size() ? track.damage_from[attacker] : 0;
        credit(attacker, robot.damage_from[attacker] - before);
      }
    }
    if (track.life && track.alive) {
      const float after = robot.alive ? robot.hp : 0.0F;
      track.life->damage_taken += std::max(0.0F, track.hp - after);
    }
    track.damage_from = robot.damage_from;
  }
}

} // namespace phantomcrash::ai_sim
