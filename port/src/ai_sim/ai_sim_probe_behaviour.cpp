// The probe's behaviour accounting (ai_sim_probe.hpp): how each robot uses
// its mounts and camouflage, when it dodges, whether it turns on whoever
// shoots it, how soon it finds someone new after a kill, and how many
// robots hold the same target at once.

#include "ai_sim_probe.hpp"

#include "ai_sim_world.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash::ai_sim {
namespace {

// Someone holds `index` locked on one of their mounts.
bool locked_on(const std::vector<Combatant> &robots, const int index) {
  const Combatant &self = robots[static_cast<std::size_t>(index)];
  return std::ranges::any_of(robots, [&](const Combatant &other) {
    return rival(self, other) && std::ranges::find(other.locks, index) != other.locks.end();
  });
}

} // namespace

void Probe::watch_behaviour(const int index, const double t, const float seconds) {
  const Battle &battle = arena_.battle();
  const auto &robots = battle.robots();
  const Combatant &robot = robots[static_cast<std::size_t>(index)];
  Track &track = tracks_[static_cast<std::size_t>(index)];
  BehaviourStats &b = track.life->behaviour;
  const RobotAiState &ai = robot.ai;
  const Combatant *target = ai.target >= 0 && ai.target < static_cast<int>(robots.size())
                                ? &robots[static_cast<std::size_t>(ai.target)]
                                : nullptr;

  // The mounts: ready to fire, and with the target in reach.
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (!usable(robot, slot)) {
      continue;
    }
    b.slot_ready[slot] += seconds;
    if (target != nullptr && target->alive) {
      const WeaponTuning &weapon = *robot.weapons[slot];
      const float distance =
          length(target->centre(battle.settings().robot_height) - robot.aim_origin());
      const float extra = weapon.kind == WeaponKind::melee ? target->radius : 0.0F;
      b.slot_in_reach[slot] +=
          distance <= weapon.range + extra && distance >= weapon.min_range ? seconds : 0.0;
    }
  }

  if (robot.camouflaged) {
    b.camouflaged += seconds;
    if (!track.was_camouflaged) {
      ++b.camouflage_uses;
      b.camouflage_threat_uses += ai.threat ? 1 : 0;
    }
  }
  track.was_camouflaged = robot.camouflaged;

  // A heavy hit: a share of the armour gone within the window.
  const float lost = std::max(0.0F, track.hp - robot.hp);
  if (lost > 0.0F) {
    track.recent_damage.emplace_back(t, lost);
  }
  while (!track.recent_damage.empty() &&
         track.recent_damage.front().first < t - tuning_.heavy_window) {
    track.recent_damage.pop_front();
  }
  float recent = 0.0F;
  for (const auto &[when, amount] : track.recent_damage) {
    recent += amount;
  }
  if (t > track.heavy_until && recent >= tuning_.heavy_share * robot.max_hp) {
    ++b.heavy_hits;
    track.heavy_until = t + tuning_.dodge_window;
    track.recent_damage.clear();
  }
  const bool locked = locked_on(robots, index);
  if (locked && !track.was_locked) {
    ++b.locked_on;
    track.locked_until = t + tuning_.dodge_window;
  }
  track.was_locked = locked;
  if (robot.mover.dodged() != 0) {
    ++(ai.threat ? b.dodges_threat : b.dodges_other);
    if (t <= track.heavy_until) {
      ++b.heavy_hit_dodges;
      track.heavy_until = t; // answered
    }
    if (t <= track.locked_until) {
      ++b.locked_on_dodges;
      track.locked_until = -1;
    }
  }

  // Shot by someone other than its target: does it turn on them?
  if (track.turn_since >= 0) {
    const bool gone = track.turn_attacker >= static_cast<int>(robots.size()) ||
                      !robots[static_cast<std::size_t>(track.turn_attacker)].alive;
    if (ai.target == track.turn_attacker) {
      ++(track.turn_busy ? b.turned_busy : b.turned_free);
      b.turn_seconds += t - track.turn_since;
      track.turn_since = -1;
    } else if (gone || t - track.turn_since > tuning_.turn_window) {
      track.turn_since = -1;
    }
  }
  for (std::size_t attacker = 0; attacker < robot.damage_from.size(); ++attacker) {
    const float before = attacker < track.blame_seen.size() ? track.blame_seen[attacker] : 0.0F;
    const auto who = static_cast<int>(attacker);
    if (robot.damage_from[attacker] <= before || who == index || who == ai.target ||
        track.turn_since >= 0 || !rival(robot, robots[attacker])) {
      continue;
    }
    track.turn_since = t;
    track.turn_attacker = who;
    track.turn_busy = target != nullptr;
    ++(track.turn_busy ? b.shot_busy : b.shot_free);
  }
  track.blame_seen = robot.damage_from;

  // After a kill: how soon it holds a live rival again.
  if (track.life->kills > track.kills_seen) {
    track.kills_seen = track.life->kills;
    track.hunt_since = t;
    ++b.kills_after;
  } else if (track.hunt_since >= 0) {
    if (target != nullptr && target->alive) {
      ++b.hunted;
      b.hunt_seconds += t - track.hunt_since;
      track.hunt_since = -1;
    } else if (t - track.hunt_since > tuning_.hunt_window) {
      track.hunt_since = -1;
    }
  }

  const Vec3 at = robot.mover.position();
  if (target == nullptr) {
    b.no_target += seconds;
    const float moved = std::hypot(at.x - track.behaviour_at.x, at.z - track.behaviour_at.z);
    b.no_target_moving += seconds > 0.0F && moved / seconds > tuning_.moving_speed ? seconds : 0.0;
  }
  track.behaviour_at = at;
}

void Probe::watch_crowding(const float seconds) {
  const auto &robots = arena_.battle().robots();
  for (std::size_t victim = 0; victim < robots.size(); ++victim) {
    if (!robots[victim].alive || (victim == 0 && !arena_.has_player())) {
      continue;
    }
    const auto attackers = static_cast<int>(std::ranges::count_if(robots, [&](const auto &other) {
      return !other.piloted && other.alive && other.ai.target == static_cast<int>(victim);
    }));
    const bool player = victim == 0;
    (player ? run_.player_alive : run_.rival_alive) += seconds;
    (player ? run_.player_attackers : run_.rival_attackers) += attackers * seconds;
    (player ? run_.player_attacked : run_.rival_attacked) += attackers > 0 ? seconds : 0.0;
    (player ? run_.player_piled : run_.rival_piled) +=
        attackers >= tuning_.piled_on ? seconds : 0.0;
  }
}

} // namespace phantomcrash::ai_sim
