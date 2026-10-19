#include "ai_sim_probe.hpp"

#include "ai_sim_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phantomcrash::ai_sim {
namespace {

float flat_distance(const Vec3 a, const Vec3 b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

} // namespace

bool usable(const Combatant &robot, const std::size_t slot) {
  return robot.weapons[slot] != nullptr && !robot.lost[slot] &&
         (!robot.magazines[slot].empty() || robot.magazines[slot].recharging());
}

bool rival(const Combatant &self, const Combatant &other) {
  return &other != &self && other.alive && !other.dying && other.team != self.team &&
         other.mover.mode() != MoveMode::scripted;
}

Probe::Probe(const int run, SimArena &arena, const ProbeTuning tuning, std::ostream *trace)
    : run_index_(run), arena_(arena), tuning_(tuning), trace_(trace) {
  tracks_.resize(arena_.battle().robots().size());
  run_.run = run;
  run_.area = arena_.area();
  run_.contest = arena_.contest();
  run_.stage = arena_.stage_name();
  run_.player = arena_.has_player();
}

void Probe::begin_life(const int index, const double t) {
  const Combatant &robot = arena_.battle().robots()[static_cast<std::size_t>(index)];
  Track &track = tracks_[static_cast<std::size_t>(index)];
  LifeStats life;
  life.run = run_index_;
  life.area = arena_.area();
  life.robot = index;
  life.entrant = arena_.entrant(index);
  life.ability = robot.ai.ability;
  life.radius = robot.radius;
  life.jump_rise = held_jump_rise(robot.mover.tuning());
  life.profile = robot.ai.profile;
  life.name = robot.name;
  life.tag = robot.tag;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (const WeaponTuning *weapon = robot.weapons[slot]) {
      life.weapons[slot] = weapon->label.empty() ? weapon->name : weapon->label;
    }
  }
  life.start = t;
  track.life = std::move(life);
  track.samples.clear();
  track.next_sample = t;
  track.next_sight = t;
  track.last_fired = -1e9;
  track.acquired_at = -1;
  track.was_visible = false;
  track.last_target = -1;
  track.stuck_since = -1;
  track.episode.reset();
  track.last_at = robot.mover.position();
  track.recent_damage.clear();
  track.blame_seen = robot.damage_from;
  track.heavy_until = track.locked_until = track.turn_since = track.hunt_since = -1;
  track.turn_attacker = -1;
  track.kills_seen = 0;
  track.was_locked = false;
  track.was_camouflaged = robot.camouflaged;
  track.behaviour_at = robot.mover.position();
  ++run_.lives;
}

void Probe::end_life(const int index, const double t, const std::string &how) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  if (!track.life) {
    return;
  }
  LifeStats &life = *track.life;
  life.end = t;
  life.ended = how;
  if (track.acquired_at >= 0) {
    ++life.unanswered;
  }
  close_blocked(index, t);
  close_episode(index, t);
  run_.destroyed += how == "destroyed" ? 1 : 0;
  run_.withdrawn += how == "withdrawn" ? 1 : 0;
  lives_.push_back(std::move(life));
  track.life.reset();
}

float Probe::reach(const Combatant &robot, const Combatant *target) const {
  float best = 0.0F;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (usable(robot, slot)) {
      const WeaponTuning &weapon = *robot.weapons[slot];
      const float extra =
          weapon.kind == WeaponKind::melee && target != nullptr ? target->radius : 0;
      best = std::max(best, weapon.range + extra);
    }
  }
  return best;
}

void Probe::watch_ai(const int index, const double t, const float seconds) {
  const Battle &battle = arena_.battle();
  const Combatant &robot = battle.robots()[static_cast<std::size_t>(index)];
  Track &track = tracks_[static_cast<std::size_t>(index)];
  LifeStats &life = *track.life;
  const RobotAiState &ai = robot.ai;
  life.alive += seconds;
  run_.alive_robot_seconds += seconds;
  life.mode[std::clamp(static_cast<int>(ai.mode), 0, ai_modes - 1)] += seconds;
  life.transitions = ai.transitions;
  life.detections = ai.detections;
  life.dodges = ai.dodges;
  life.recoveries = ai.recoveries;

  const Combatant *target = ai.target >= 0 && ai.target < static_cast<int>(battle.robots().size())
                                ? &battle.robots()[static_cast<std::size_t>(ai.target)]
                                : nullptr;
  if (target != nullptr) {
    life.target += seconds;
    life.player_target += ai.target == 0 ? seconds : 0.0;
  }
  if (ai.visible) {
    life.visible += seconds;
    if (life.first_seen < 0) {
      life.first_seen = t - life.start;
    }
  }
  // A fresh sighting (or a new target in sight) starts the reaction clock.
  if (ai.visible && (!track.was_visible || ai.target != track.last_target) &&
      track.acquired_at < 0) {
    track.acquired_at = t;
    ++life.acquisitions;
  }
  if (target == nullptr && track.acquired_at >= 0) {
    track.acquired_at = -1;
    ++life.unanswered;
  }
  track.was_visible = ai.visible;
  track.last_target = ai.target;

  if (target != nullptr && target->alive) {
    const float distance =
        length(target->centre(battle.settings().robot_height) - robot.aim_origin());
    bool in_reach = false;
    bool requested = false;
    bool locked = false;
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if (usable(robot, slot)) {
        const WeaponTuning &weapon = *robot.weapons[slot];
        const float extra = weapon.kind == WeaponKind::melee ? target->radius : 0.0F;
        in_reach |= distance <= weapon.range + extra && distance >= weapon.min_range;
      }
      requested |= ai.request[slot];
      locked |= ai.request[slot] && robot.locks[slot] == ai.target;
    }
    life.in_reach += in_reach ? seconds : 0.0;
    life.request += requested ? seconds : 0.0;
    life.locked += locked ? seconds : 0.0;
  }
  life.trigger += std::ranges::any_of(ai.fire, [](bool held) { return held; }) ? seconds : 0.0;
}

// Could it shoot someone if it turned to them: a rival within its weapons'
// reach with no stage wall between them. Facing and the AI's sight angles
// do not count; the gap between this and firing is the AI's choice.
void Probe::watch_sight(const int index, const double t) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  if (t + 1e-9 < track.next_sight) {
    return;
  }
  track.next_sight = t + tuning_.sight_seconds;
  const Battle &battle = arena_.battle();
  const Combatant &robot = battle.robots()[static_cast<std::size_t>(index)];
  LifeStats &life = *track.life;
  const float range = reach(robot, nullptr);
  float nearest = std::numeric_limits<float>::max();
  bool opportunity = false;
  for (const Combatant &other : battle.robots()) {
    if (!rival(robot, other)) {
      continue;
    }
    const Vec3 to = other.centre(battle.settings().robot_height);
    const float distance = length(to - robot.aim_origin());
    nearest = std::min(nearest, distance);
    if (!opportunity && distance <= range && !battle.stage().segment_hit(robot.aim_origin(), to)) {
      opportunity = true;
    }
  }
  const double step = tuning_.sight_seconds;
  if (nearest < std::numeric_limits<float>::max()) {
    life.nearest_sum += nearest;
    ++life.nearest_count;
  }
  life.no_enemy_near += nearest > tuning_.acquire_range ? step : 0.0;
  if (opportunity) {
    life.opportunity += step;
    life.opportunity_no_target += robot.ai.target < 0 ? step : 0.0;
    life.opportunity_holding += t - track.last_fired > tuning_.idle_after_fire ? step : 0.0;
  }
}

void Probe::watch_motion(const int index, const double t) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  const Combatant &robot = arena_.battle().robots()[static_cast<std::size_t>(index)];
  LifeStats &life = *track.life;
  const Vec3 at = robot.mover.position();
  life.distance_travelled += flat_distance(at, track.last_at);
  track.last_at = at;
  if (t + 1e-9 < track.next_sample) {
    return;
  }
  track.next_sample = t + tuning_.sample_seconds;
  const RobotControl &control = robot.ai.control;
  const bool pushes = std::abs(control.move[1]) > tuning_.wants_move ||
                      std::abs(control.strafe) > tuning_.wants_move;
  const bool wants = pushes || robot.ai.nav.active;
  track.samples.push_back({t, at, wants, pushes});
  while (track.samples.size() > 1 && t - track.samples[1].t >= tuning_.window_seconds - 1e-6) {
    track.samples.pop_front();
  }
  const Sample &oldest = track.samples.front();
  if (t - oldest.t < tuning_.window_seconds - 1e-6) {
    return; // not a full window yet
  }
  const auto wanting =
      std::ranges::count_if(track.samples, [](const Sample &s) { return s.wants; });
  const bool tries = wanting * 2 > static_cast<std::ptrdiff_t>(track.samples.size());
  const bool still = flat_distance(at, oldest.at) < tuning_.stuck_distance;
  const double step = tuning_.sample_seconds;
  const bool stuck = still && tries;
  const auto pushing =
      std::ranges::count_if(track.samples, [](const Sample &s) { return s.pushes; });
  const bool blocked = still && pushing * 2 > static_cast<std::ptrdiff_t>(track.samples.size());
  if (blocked) {
    life.blocked += step;
    if (track.blocked_since < 0) {
      ++life.blocked_episodes;
      track.blocked_since = t - tuning_.window_seconds;
    }
  } else {
    close_blocked(index, t);
  }
  if (!still) {
    life.moving += step;
  } else if (stuck) {
    life.stuck += step;
  } else if (robot.ai.target >= 0) {
    life.still_fighting += step;
  } else {
    life.still_idle += step;
  }
  if (stuck && !track.episode) {
    ++life.stuck_episodes;
    track.episode = open_episode(index, t - tuning_.window_seconds);
  } else if (!stuck && track.episode) {
    close_episode(index, t);
  }
}

StuckPoint Probe::open_episode(const int index, const double t) const {
  const auto &robots = arena_.battle().robots();
  const Combatant &robot = robots[static_cast<std::size_t>(index)];
  const RobotAiState &ai = robot.ai;
  StuckPoint point{run_index_,
                   arena_.area(),
                   index,
                   tracks_[static_cast<std::size_t>(index)].life->entrant,
                   t,
                   0.0,
                   robot.mover.position(),
                   static_cast<int>(ai.mode),
                   ai.nav.active};
  const bool engage = ai.mode == AiMode::engage;
  const bool routing = ai.mode == AiMode::route || ai.mode == AiMode::route_special;
  point.target = ai.target >= 0;
  if (engage && ai.target >= 0 && ai.nav.vantage >= 0 && ai.nav.vantage_target == ai.target) {
    point.purpose = StuckPurpose::vantage;
  } else if (engage && ai.target >= 0 && ai.submode == 4) {
    point.purpose = StuckPurpose::approach;
  } else if (routing || (engage && ai.target >= 0 && ai.submode == 3)) {
    point.purpose = StuckPurpose::route;
  } else if (engage && ai.target < 0 && (ai.search == 0 || ai.search == 4)) {
    point.purpose = StuckPurpose::search;
  }
  point.goal = ai.nav.goal;
  if (ai.nav.next < ai.nav.route.size()) {
    point.waypoint = ai.nav.route[ai.nav.next].position;
    point.link = static_cast<int>(ai.nav.route[ai.nav.next].link);
  }
  for (const Combatant &other : robots) {
    if (&other != &robot && other.alive && !other.dying) {
      const float d = flat_distance(other.mover.position(), point.at);
      point.crowd = point.crowd < 0 ? d : std::min(point.crowd, d);
    }
  }
  point.recoveries = ai.recoveries;
  return point;
}

void Probe::close_blocked(const int index, const double t) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  if (track.blocked_since < 0 || !track.life) {
    return;
  }
  const double seconds = t - track.blocked_since;
  track.life->longest_blocked = std::max(track.life->longest_blocked, seconds);
  track.life->long_blocked_episodes += seconds >= tuning_.long_stuck_seconds ? 1 : 0;
  track.blocked_since = -1;
}

void Probe::close_episode(const int index, const double t) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  if (!track.episode || !track.life) {
    return;
  }
  const Combatant &robot = arena_.battle().robots()[static_cast<std::size_t>(index)];
  LifeStats &life = *track.life;
  StuckPoint &point = *track.episode;
  point.seconds = t - point.t;
  point.recoveries = robot.ai.recoveries - point.recoveries;
  life.longest_stuck = std::max(life.longest_stuck, point.seconds);
  life.long_stuck_episodes += point.seconds >= tuning_.long_stuck_seconds ? 1 : 0;
  stuck_points_.push_back(point);
  track.episode.reset();
}

void Probe::write_trace(const int index, const double t) {
  Track &track = tracks_[static_cast<std::size_t>(index)];
  if (trace_ == nullptr || t + 1e-9 < track.next_trace) {
    return;
  }
  track.next_trace = t + 1.0;
  const Combatant &robot = arena_.battle().robots()[static_cast<std::size_t>(index)];
  const Vec3 at = robot.mover.position();
  const auto &ai = robot.ai;
  *trace_ << run_index_ << ',' << arena_.area() << ',' << t << ',' << index << ','
          << (track.life ? track.life->entrant : -1) << ',' << at.x << ',' << at.y << ',' << at.z
          << ',' << robot.hp << ',' << static_cast<int>(ai.mode) << ',' << ai.target << ','
          << ai.visible << ',' << ai.nav.active << ',' << ai.nav.stall << ','
          << std::ranges::count(ai.request, true) << ',' << std::ranges::count(ai.fire, true) << ','
          << ai.control.move[0] << ',' << ai.control.move[1] << '\n';
}

void Probe::observe(const double t, const float seconds) {
  const auto &robots = arena_.battle().robots();
  std::vector<bool> destroyed(robots.size(), false);
  read_events(t, destroyed);
  read_damage(destroyed);
  watch_crowding(seconds);
  for (std::size_t index = 0; index < robots.size(); ++index) {
    const Combatant &robot = robots[index];
    Track &track = tracks_[index];
    const bool computer = !robot.piloted;
    if (computer && robot.alive && !track.alive) {
      begin_life(static_cast<int>(index), t);
    } else if (computer && !robot.alive && track.alive) {
      end_life(static_cast<int>(index), t, destroyed[index] ? "destroyed" : "withdrawn");
    }
    if (index == 0 && arena_.has_player() && robot.alive) {
      const bool targeted = std::ranges::any_of(robots, [](const Combatant &other) {
        return !other.piloted && other.alive && other.ai.target == 0;
      });
      run_.player_targeted += targeted ? seconds : 0.0;
      if (targeted && run_.player_first_targeted < 0) {
        run_.player_first_targeted = t;
      }
    }
    if (computer && robot.alive && track.life) {
      watch_ai(static_cast<int>(index), t, seconds);
      watch_sight(static_cast<int>(index), t);
      watch_motion(static_cast<int>(index), t);
      watch_behaviour(static_cast<int>(index), t, seconds);
      write_trace(static_cast<int>(index), t);
    }
    track.alive = robot.alive;
    track.hp = robot.hp;
  }
}

void Probe::finish(const double t, const double wall_seconds) {
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    end_life(static_cast<int>(index), t, "survived");
  }
  run_.seconds = t;
  run_.wall_seconds = wall_seconds;
}

} // namespace phantomcrash::ai_sim
