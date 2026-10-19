// Authored ENPT routing and original movement/search callback families.
#include "phantomcrash/battle.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace phantomcrash {
namespace {
float flat(Vec3 v) {
  v.y = 0;
  return length(v);
}
// Port navigation: an approach plans a route to a target this far above or below.
constexpr float approach_rise = 3.0F;
float stick(float error, float speed, float seconds) {
  return speed > 0 ? std::clamp(error / (speed * seconds), -1.0F, 1.0F) : 0;
}
} // namespace

void Battle::ai_route(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto at = drone.mover.position();
  if (s.route_rejoin) {
    // Join an existing polyline. Keep the entrance's active segment until it
    // has carried the robot into the arena; never jump to its first point.
    if (!drone.path.active() || path_.segments[drone.path.segment()].kind != 0) {
      // The reach flood is a graph search: without one to spare this tick,
      // rejoin on a later one.
      if (settings_.ai.navigation.enabled && nav_graph() != nullptr && !take_nav_search()) {
        return;
      }
      // Port navigation: only points a planned route can reach. The nearest
      // reachable point in sight wins, so test them nearest first.
      const std::vector<bool> reach = nav_reach(drone);
      struct Candidate {
        float distance{};
        Vec3 point;
        PathFollower follower;
      };
      std::vector<Candidate> candidates;
      const std::vector<bool> *climbs = planned_climbs();
      for (std::size_t i = 0; i < path_.segments.size(); ++i) {
        const auto &segment = path_.segments[i];
        if (segment.kind == 0 || segment.kind == 7) {
          continue;
        }
        PathFollower candidate;
        candidate.join(path_, static_cast<int>(i), at);
        const auto point = candidate.update(path_, at, settings_.drone_reach, random_, climbs);
        // Port navigation: a point it already stands at (a dead end) is no
        // place to go.
        if (climbs != nullptr && flat(point - at) < settings_.drone_reach) {
          continue;
        }
        candidates.push_back({length(point - at), point, candidate});
      }
      std::ranges::stable_sort(candidates, {}, &Candidate::distance);
      // Each sight line is a stage raycast: testing every point when none is
      // in sight cost several milliseconds, so a planned route tests only the
      // nearest few and otherwise takes the nearest it can reach.
      const bool planned = !reach.empty();
      int tests_left =
          planned ? settings_.ai.navigation.rejoin_sight_tests : std::numeric_limits<int>::max();
      const Candidate *nearest = nullptr;
      const Candidate *chosen = nullptr;
      for (const Candidate &c : candidates) {
        if (!nav_reaches(reach, c.point)) {
          continue;
        }
        nearest = nearest != nullptr ? nearest : &c;
        if (tests_left-- <= 0) {
          break;
        }
        if (clear_target_sight(drone.aim_origin(), c.point + Vec3{0, drone.radius, 0})) {
          chosen = &c;
          break;
        }
      }
      chosen = chosen != nullptr ? chosen : (planned ? nearest : nullptr);
      if (chosen != nullptr) {
        drone.path = chosen->follower;
      } else if (planned) {
        // Nowhere on the route it can reach (a pit): steer the original way.
        s.nav.fallback_left = settings_.ai.navigation.fallback_seconds;
      }
    }
    s.route_rejoin = false;
    s.route_stalled = 0;
  }
  if (!drone.path.active()) {
    return;
  }
  const Vec3 target =
      drone.path.update(path_, at, settings_.drone_reach, random_, planned_climbs());
  if (length(target - s.route_target) > 0.001F ||
      flat(target - at) < flat(s.route_target - s.last_position)) {
    s.route_stalled = 0;
  } else {
    s.route_stalled += seconds;
  }
  s.route_target = target;
  s.last_position = at;
  const int kind = path_.segments[drone.path.segment()].kind;
  const float speed = kind == 0 || kind == 3 ? data.value("route_walk")
                      : kind == 5            ? data.value("route_dash_walk")
                                             : 1.0F;
  const bool pause =
      kind == 2 && std::fmod(static_cast<float>(s.ticks) / data.value("tick_hz"),
                             data.value("route_pause_cycle")) < data.value("route_pause_seconds");
  if (ai_navigate(drone, target, seconds, speed)) {
    s.nav.skips = 0;
    // The planned route replaces the straight line and the wall recovery.
    s.control.dash = kind == 5 && drone.mover.grounded() && !s.nav.lifting;
    if (pause && drone.mover.grounded() && !s.nav.lifting) {
      s.control.move[1] = 0;
    }
    if (s.route_stalled >= data.value("route_retry_seconds")) {
      s.route_rejoin = true;
      ++s.recoveries;
    }
    return;
  }
  if (s.nav.dropped) {
    // Escalated: skip the point it could not get to.
    drone.path.update(path_, at, std::numeric_limits<float>::max(), random_);
    return;
  }
  if (s.nav.unreachable) {
    // No route there (another level it cannot get down to, or a ledge its
    // jump does not reach): skip the point rather than walk straight at
    // it, and after a few, rejoin the route elsewhere.
    drone.path.update(path_, at, std::numeric_limits<float>::max(), random_, planned_climbs());
    if (++s.nav.skips >= settings_.ai.navigation.route_skips) {
      s.nav.skips = 0;
      s.route_rejoin = true;
    }
    return;
  }
  if (settings_.ai.navigation.enabled && nav_graph() != nullptr && s.nav.fallback_left <= 0) {
    s.route_rejoin = true; // no route there: take another polyline
  }
  const float error = wrap_angle(heading_toward(target - at) - drone.mover.heading());
  s.control.move[0] = stick(error, radians(drone.mover.tuning().turn_degrees_per_second), seconds);
  s.control.move[1] = std::abs(error) < data.value("route_turn_radians") ? speed : 0;
  s.control.dash = kind == 5;
  if (pause) {
    s.control.move[1] = 0;
  }
  const bool touching = (drone.stage_contact & (stage_contact::wall | stage_contact::fence)) != 0;
  s.blocked = touching ? s.blocked + seconds : 0;
  if (s.blocked > 0) {
    if (s.blocked < data.value("blocked_reverse_seconds")) {
      s.control.move[1] = -1;
    } else if (s.blocked < data.value("blocked_pause_seconds")) {
      s.control.move[1] = 0;
    } else {
      s.control.jump = true;
    }
    if (s.blocked > data.value("blocked_jump_seconds")) {
      s.control.dodge = s.dodge_side;
      s.blocked = 0;
      s.route_rejoin = true;
    }
  }
  if (s.route_stalled >= data.value("route_retry_seconds")) {
    s.route_rejoin = true;
    s.control.jump = true;
    ++s.recoveries;
  }
}

void Battle::ai_move(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto &level = data.levels[static_cast<std::size_t>(s.ability)];
  // 0x05cce9 cached the movement chances at setup (none below ability 1).
  const auto &setup = data.levels[static_cast<std::size_t>(s.setup_ability)];
  const auto &motion = drone.mover.tuning();
  s.control = {};
  s.nav.active = false;
  // Port navigation: headway is measured over unbroken navigation.
  if (!s.nav.asked) {
    s.nav.anchor_seconds = 0;
  }
  s.nav.asked = false;
  const bool ranker_entrant =
      std::ranges::find(data.ranker_entrants, s.entrant) != data.ranker_entrants.end();
  if (ranker_entrant) {
    if (s.entrant == static_cast<int>(data.value("ranker_no_random_cloak"))) {
      s.forced_weapons |= static_cast<unsigned>(data.ranker_weapon_masks[2]);
    }
    // 0x05de0f counts +0x132 down and, when it runs out, chooses the route
    // (0x05e494) or the attack (0x05e660) afresh; both start it at 8 s.
    s.ranker_left -= seconds;
    if (s.ranker_left <= 0) {
      const int band =
          std::clamp(static_cast<int>(drone.hp * 5 / std::max(drone.max_hp, 1.0F)), 0, 4);
      s.forced_weapons |= static_cast<unsigned>(data.ranker_weapon_masks[band]);
      s.ranker_defensive |= band < 2 || static_cast<float>(s.ticks) / data.value("tick_hz") >
                                            data.value("ranker_defensive_seconds");
      s.ranker_orbit = s.ranker_provoked = false;
      s.ranker_engage =
          s.target >= 0 && (band < 4 || ai_chance(random_, data.value("ranker_full_health_chance"),
                                                  data.value("probability_scale")));
      s.ranker_left =
          data.value(s.ranker_engage ? "ranker_engage_seconds" : "ranker_route_seconds");
    }
  }
  // Provoked, a ranker runs the common states until it chooses again.
  const bool ranker = ranker_entrant && !s.ranker_provoked;
  if (ranker && !s.ranker_engage && s.visible &&
      ai_chance(random_, data.value("ranker_route_attack_chance"),
                data.value("probability_scale"))) {
    s.ranker_engage = true;
  }
  if (s.mode == AiMode::engage && s.entered) {
    const bool melee = std::ranges::any_of(
        drone.weapons, [](const auto *w) { return w && w->kind == WeaponKind::melee; });
    if (melee &&
        ai_chance(random_, data.value("melee_submode_chance"), data.value("probability_scale"))) {
      s.submode = 4;
    } else if (s.submode == 4) {
      s.submode = 3;
    }
  }
  s.entered = false;
  const AiMode mode = ranker ? (s.ranker_engage ? AiMode::engage : AiMode::route) : s.mode;
  // Port navigation: a fight's high ground takes the place of the route.
  const bool climbing = mode == AiMode::engage && s.target >= 0 && s.nav.vantage >= 0 &&
                        s.nav.vantage_target == s.target;
  if ((mode == AiMode::route || mode == AiMode::route_special ||
       (mode == AiMode::engage && !ranker && s.submode == 3)) &&
      !climbing) {
    ai_route(drone, seconds);
  }
  if (mode == AiMode::engage && s.target < 0) {
    if (s.search < 0) {
      // 0x05be72 selects an INDEX by cumulative short weights.
      const int total = std::accumulate(data.search_weights.begin(), data.search_weights.end(), 0);
      int draw = static_cast<int>(ai_random(random_) * static_cast<float>(total));
      s.search = 0;
      for (std::size_t i = 0; i < data.search_weights.size(); ++i) {
        draw -= data.search_weights[i];
        s.search = static_cast<int>(i);
        if (draw <= 0) {
          break;
        }
      }
      s.action_left = data.value("search_seconds");
      s.dodge_side = (random_() & 1U) ? 1 : -1;
    }
    if (s.search == 0 || s.search == 4) {
      ai_route(drone, seconds);
    }
    if (s.search == 1) {
      s.control.move[1] = static_cast<float>(s.dodge_side);
      s.control.jump = true;
    }
    if (s.search == 2 && s.action_left <= 0) {
      s.control.dodge = s.dodge_side;
    }
    s.control.pitch = drone.mover.pitch() >= motion.pitch_limit ? -1.0F : 1.0F;
    if (s.action_left <= 0) {
      s.search = -1;
    }
  } else if (s.target >= 0) {
    const Vec3 offset = s.last_seen - drone.aim_origin();
    const float yaw = wrap_angle(heading_toward(offset) - drone.mover.heading());
    const float pitch = std::atan2(offset.y, std::hypot(offset.x, offset.z));
    s.control.pitch = stick(pitch - drone.mover.pitch(), motion.pitch_per_second, seconds);
    const bool routing = mode == AiMode::route || mode == AiMode::route_special ||
                         (mode == AiMode::engage && !ranker && s.submode == 3);
    if (routing) {
      s.control.twist = stick(wrap_angle(yaw - drone.mover.twist()),
                              radians(motion.twist_degrees_per_second), seconds);
    } else {
      s.control.reset_twist = true;
      s.control.move[0] = stick(yaw, radians(motion.turn_degrees_per_second), seconds);
    }
    if (mode == AiMode::withdraw) {
      s.control.move[1] = s.ability > 0 ? -1.0F : 1.0F;
      if (s.ability == 0) {
        s.control.move[0] =
            stick(wrap_angle(yaw + pi), radians(motion.turn_degrees_per_second), seconds);
      }
    }
    if (mode == AiMode::engage) {
      // Port navigation: high ground, once taken, is held rather than left
      // to close in.
      if (!ranker && ai_high_ground(drone, seconds)) {
        s.control.dash = false;
      } else if ((s.submode == 4 || (ranker && s.ranker_engage)) && !s.nav.holding) {
        s.control.move[1] =
            flat(offset) > data.value(ranker ? "ranker_close_distance" : "approach_stop_distance")
                ? 1.0F
                : 0;
        s.control.dash = ranker && s.control.move[1] > 0;
        // Port navigation: out of sight or on another level, walk a route.
        if (s.control.move[1] > 0 && (!s.visible || std::abs(offset.y) > approach_rise) &&
            ai_navigate(drone, s.last_seen, seconds, 1.0F)) {
          s.control.dash = false;
        }
      }
      if (s.nav.active) {
        // Steering follows the route: aim with the torso, as routing does.
        s.control.reset_twist = false;
        s.control.twist = stick(wrap_angle(yaw - drone.mover.twist()),
                                radians(motion.twist_degrees_per_second), seconds);
      } else {
        s.control.jump |= (drone.stage_contact & stage_contact::wall) != 0;
      }
    }
  }
  if (ranker && s.ranker_engage) {
    const float distance = flat(s.last_seen - drone.aim_origin());
    if (!s.visible) {
      s.control.move[1] = 0;
      s.blocked += seconds;
      if (s.blocked >= data.value("ranker_blocked_seconds")) {
        s.control.dodge = s.dodge_side;
        s.ranker_engage = s.ranker_orbit = false;
        s.route_rejoin = true;
        s.blocked = 0;
      }
    } else {
      s.blocked = 0;
      if (distance <= data.value("ranker_close_distance") &&
          ai_chance(random_,
                    data.value(s.entrant == 1 ? "ranker_first_rival_orbit_chance"
                                              : "ranker_orbit_chance"),
                    data.value("probability_scale"))) {
        s.ranker_orbit = true;
      }
      if (s.ranker_orbit) {
        s.control.move[1] = 0;
        s.control.dash = false;
        s.control.dodge = s.dodge_side;
        if (distance > data.value("ranker_orbit_range")) {
          s.ranker_engage = s.ranker_orbit = false;
          s.route_rejoin = true;
        }
      }
    }
  }
  if (s.action_left <= 0 && mode != AiMode::route_special) {
    s.action_left = data.value("movement_interval");
    if (drone.mover.grounded()) {
      s.control.jump |= ai_chance(random_, static_cast<float>(setup.movement_chances[1]),
                                  data.value("probability_scale"));
    } else {
      s.control.dash |= ai_chance(random_, static_cast<float>(setup.movement_chances[0]),
                                  data.value("probability_scale"));
    }
    if (ai_chance(random_, static_cast<float>(setup.movement_chances[2]),
                  data.value("probability_scale"))) {
      s.control.dodge = (random_() & 1U) ? 1 : -1;
      ++s.dodges;
    }
  }
  s.control.move[0] *= level.control_scale;
  s.control.move[1] *= level.control_scale;
}
} // namespace phantomcrash
