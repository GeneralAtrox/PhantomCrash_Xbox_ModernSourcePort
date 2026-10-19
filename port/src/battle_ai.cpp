// Original NPC callback dispatcher and perception (0x057c71 / 0x05b9c1).
#include "phantomcrash/battle.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {
// 0x05ec7d: a ranker's (entrants 0, 1, 10 and 19) is 2; otherwise loadout
// +0x6c: member 0, leader 1, champion 2. A reserve's is 0 (0x045270 seats the
// reserve team through 0x0462cd as members); a player's and a stand-alone
// drone's are taken as 0.
int ai_class(const Combatant &robot, const OriginalAiData &data) {
  if (!robot.piloted &&
      std::ranges::find(data.ranker_entrants, robot.ai.entrant) != data.ranker_entrants.end()) {
    return static_cast<int>(data.value("ranker_class"));
  }
  return !robot.piloted && robot.marker_colour >= 0 && robot.marker_colour <= 2
             ? robot.marker_colour
             : 0;
}

// 0x05d4b7: the first robot, in order, that hurt `drone` since its last
// tick and whose class is below a ranker's; a ranker only by chance. Like
// the original it asks no side: no weapon hurts a teammate here, and sensing
// drops a teammate target on the next tick.
int blamed_attacker(const Combatant &drone, const std::vector<Combatant> &robots,
                    const std::vector<float> &fresh, const OriginalAiData &data,
                    std::minstd_rand &random) {
  for (std::size_t i = 0; i < fresh.size() && i < robots.size(); ++i) {
    const Combatant &other = robots[i];
    if (fresh[i] <= 0 || &other == &drone || !other.alive) {
      continue;
    }
    if (ai_class(other, data) < data.value("ranker_class") ||
        ai_chance(random, data.value("retaliation_ranker_chance"),
                  data.value("probability_scale"))) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
} // namespace

DroneAi parse_drone_ai(const json::JsonValue &value) {
  DroneAi ai;
  ai.fight = value.boolean_or("fight", false);
  ai.trace = value.boolean_or("trace", false);
  ai.nearest_fallback_target = value.boolean_or("nearest_fallback_target", false);
  ai.jump_keeps_target = value.boolean_or("jump_keeps_target", false);
  ai.rivals_by_sight = value.boolean_or("rivals_by_sight", false);
  ai.jump_any_floor = value.boolean_or("jump_any_floor", false);
  ai.assigned_ability_settings = value.boolean_or("assigned_ability_settings", false);
  if (const json::JsonValue *navigation = value.find("navigation")) {
    ai.navigation = parse_nav_tuning(*navigation);
  }
  return ai;
}

void Battle::ai_sense(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto &level = data.levels[static_cast<std::size_t>(s.ability)];
  const auto &setup = data.levels[static_cast<std::size_t>(s.setup_ability)];
  s.detect_left -= seconds;
  if (s.detect_left <= 0) {
    s.cloak_detection =
        ai_chance(random_, static_cast<float>(s.ability + 1), data.value("cloak_detect_scale"));
    s.detect_left = data.value("cloak_detect_seconds");
  }
  const auto eligible = [&](const Combatant &other) {
    return &other != &drone && other.alive && other.team != drone.team &&
           other.mover.mode() != MoveMode::scripted;
  };
  const auto sees = [&](const Combatant &other) {
    if (!eligible(other)) {
      return false;
    }
    const Vec3 offset = other.centre(settings_.robot_height) - drone.aim_origin();
    const float distance = length(offset);
    if (distance > data.value("acquire_range")) {
      return false;
    }
    // 0x04f152 measures the bearing off the heading plus twist and the
    // elevation off the level; 0x05d11e holds them to the sight angles.
    const float yaw = std::abs(wrap_angle(heading_toward(offset) - drone.mover.aim_heading()));
    const float pitch = std::atan2(offset.y, std::hypot(offset.x, offset.z));
    if (yaw > setup.sight_yaw || std::abs(pitch) > setup.sight_pitch) {
      return false;
    }
    if (other.camouflaged && !s.cloak_detection) {
      const bool firing = std::ranges::any_of(other.cooldown, [](float v) { return v > 0; });
      if ((distance > data.value("cloak_proximity") && !firing) ||
          !ai_chance(random_, static_cast<float>(s.ability), data.value("cloak_detect_scale"))) {
        return false;
      }
    }
    return clear_target_sight(drone.aim_origin(), other.centre(settings_.robot_height));
  };
  // 0x05b9c1 picks afresh each tick from the rivals within acquire_range,
  // nearest first (0x04f152): a player's robot it must see (0x05d11e), a
  // computer's it must have locked (0x04f6a0, robot +0x130). With neither,
  // any rival it sees: the nearest, or while it holds a target the last one
  // seen down the list (nearest_fallback_target keeps the nearest).
  std::vector<std::pair<float, int>> near;
  for (std::size_t i = 0; i < robots_.size(); ++i) {
    const float distance = length(robots_[i].mover.position() - drone.mover.position());
    if (eligible(robots_[i]) && distance < data.value("acquire_range")) {
      near.emplace_back(distance, static_cast<int>(i));
    }
  }
  std::ranges::sort(near);
  const int locked = ai_lock_target(drone);
  int candidate = -1;
  for (const auto &[distance, i] : near) {
    const Combatant &other = robots_[static_cast<std::size_t>(i)];
    if (other.piloted || settings_.ai.rivals_by_sight ? sees(other) : i == locked) {
      candidate = i;
      break;
    }
  }
  if (candidate < 0) {
    const bool holding = s.target >= 0 && !settings_.ai.nearest_fallback_target;
    for (const auto &[distance, i] : near) {
      if (candidate >= 0 && !holding) {
        break;
      }
      if (sees(robots_[static_cast<std::size_t>(i)])) {
        candidate = i;
      }
    }
  }
  if (s.target >= 0 && (s.target >= static_cast<int>(robots_.size()) ||
                        !eligible(robots_[static_cast<std::size_t>(s.target)]))) {
    s.target = -1;
  }
  s.visible = candidate >= 0;
  if (s.visible) {
    if (s.target != candidate) {
      ++s.detections;
    }
    s.target = candidate;
  }
  if (s.visible) {
    s.memory = level.memory_seconds;
    s.last_seen = robots_[static_cast<std::size_t>(s.target)].centre(settings_.robot_height);
    s.search = -1;
  } else if (!settings_.ai.jump_keeps_target || s.jump_phase == 0) {
    s.memory -= seconds;
    if (s.memory <= 0) {
      s.target = -1;
    }
  }
  // 0x05d2bd: holding no target, a robot can turn on who hurt it since its
  // last tick (blamed_attacker). It turns on one it can see at ability + 127
  // in 255, on a cloaked one at the ability (at least 10), and always when
  // its own class is above 0. Fetching a supply box other than money, it
  // carries on.
  std::vector<float> fresh = drone.damage_from;
  for (std::size_t i = 0; i < fresh.size() && i < s.damage_seen.size(); ++i) {
    if (fresh[i] >= s.damage_seen[i]) {
      fresh[i] -= s.damage_seen[i];
    }
  }
  s.damage_seen = drone.damage_from;
  if (s.target >= 0 || s.mode == AiMode::route_special) {
    return;
  }
  const int attacker = blamed_attacker(drone, robots_, fresh, data, random_);
  if (attacker < 0) {
    return;
  }
  const Combatant &other = robots_[static_cast<std::size_t>(attacker)];
  const auto ability = static_cast<float>(s.ability);
  const float scale = data.value("probability_scale");
  bool provoked =
      other.camouflaged
          ? ai_chance(random_, std::max(ability, data.value("retaliation_cloaked_floor")), scale)
          : ai_chance(random_, ability + data.value("retaliation_seen_bonus"), scale);
  provoked = provoked || ai_class(drone, data) > 0;
  bool fetching = false;
  if (s.pickup >= 0 && supply_ && s.pickup < static_cast<int>(supply_->pickups().size())) {
    const auto kind =
        static_cast<float>(supply_->pickups()[static_cast<std::size_t>(s.pickup)].kind);
    fetching = kind >= data.value("retaliation_fetch_first_kind") &&
               kind <= data.value("retaliation_fetch_last_kind");
  }
  if (!provoked || fetching) {
    return;
  }
  s.target = attacker;
  s.last_seen = other.centre(settings_.robot_height);
  s.memory = data.value("retaliation_memory_seconds") +
             ability * data.value("retaliation_memory_per_ability");
  s.pickup = -1;
  s.jump_phase = 0;
  ai_enter(s, AiMode::engage, data, random_);
  // +0x132 = 8 s: a ranker's own choice (0x05de0f) waits that long.
  if (std::ranges::find(data.ranker_entrants, s.entrant) != data.ranker_entrants.end()) {
    s.ranker_provoked = true;
    s.ranker_engage = s.ranker_orbit = false;
    s.ranker_left = data.value("retaliation_ranker_hold_seconds");
  }
}

RobotControl Battle::ai_control(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const float step = 1.0F / data.value("tick_hz");
  if (!s.initialized) {
    if (settings_.ai.showcase) {
      s.ability = static_cast<int>(data.value("showcase_ability")); // 0x05f445
    }
    s.ability = std::clamp(s.ability, 0, 99);
    // 0x080778 runs 0x057863, whose setters cache sight, firing, camouflage
    // and movement chances at 0x05f445's ability, before 0x057820 assigns
    // it: ability 0, or the showcase's.
    s.setup_ability =
        settings_.ai.showcase || settings_.ai.assigned_ability_settings ? s.ability : 0;
    if (s.profile < 0 || s.profile >= static_cast<int>(data.profiles.size())) {
      throw std::runtime_error("NPC has an invalid original AI profile");
    }
    s.initialized = true;
    // Robots think in turn: with two battle ticks to each AI step, half of
    // them step on each tick instead of all of them on every other one.
    const auto index = static_cast<int>(&drone - robots_.data());
    const int phases = std::max(1, static_cast<int>(std::lround(step / seconds)));
    s.accumulator = step * static_cast<float>(index % phases) / static_cast<float>(phases);
    s.previous_hp = drone.hp;
    s.last_position = drone.mover.position();
    ai_setup_weapons(drone, ai_class(drone, data) > 1);
    ai_enter(s, AiMode::route, data, random_);
    s.camouflage_wait = data.levels[static_cast<std::size_t>(s.setup_ability)].camouflage_interval *
                        ai_random(random_);
    if (settings_.ai.trace) {
      std::clog << "Original AI: entrant=" << s.entrant << " ability=" << s.ability
                << " profile=" << s.profile << " name=" << drone.name << '\n';
    }
  }
  // Analogue controls persist between AI updates; pressed buttons are pulses.
  s.control.dodge = 0;
  s.camouflage_toggle = false;
  s.accumulator += seconds;
  while (s.accumulator + 0.000001F >= step) {
    s.accumulator -= step;
    ++s.ticks;
    s.guard -= step;
    s.state_left -= step;
    s.action_left -= step;
    ai_sense(drone, step);
    bool armed = false;
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      armed |= drone.weapons[slot] && !drone.lost[slot] &&
               (!drone.magazines[slot].empty() || drone.magazines[slot].recharging());
    }
    if (s.pickup < 0 && s.jump_phase == 0) {
      ai_decide(s, data, drone.hp, drone.max_hp, armed, random_);
    }
    s.previous_hp = drone.hp;
    if (s.pickup < 0 && s.jump_phase == 0) {
      ai_move(drone, step);
    }
    if (s.pickup < 0) {
      ai_jump(drone, step);
    }
    ai_pickup(drone, step);
    ai_reactions(drone, step);
    ai_weapons(drone, step);
    s.diagnostics_left -= step;
    if (settings_.ai.trace && s.diagnostics_left <= 0) {
      s.diagnostics_left = data.value("trace_interval");
      std::clog << "Original AI: entrant=" << s.entrant << " ticks=" << s.ticks
                << " state=" << static_cast<int>(s.mode) << " target=" << s.target
                << " visible=" << s.visible << " changes=" << s.transitions << " hp=" << drone.hp
                << " requests=" << s.shots << " attacks=" << s.attacks << " dodges=" << s.dodges
                << " pickups=" << s.pickup_selections << " recoveries=" << s.recoveries << '\n';
    }
  }
  return s.control;
}
} // namespace phantomcrash
