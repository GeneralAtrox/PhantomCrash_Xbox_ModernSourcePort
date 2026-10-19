// Port navigation for computer robots: following planned routes over the
// stage's navigation grid, with held jumps up ledges, and taking high
// ground in a fight (nav_graph.hpp; rumbling.json live.ai.navigation).
#include "phantomcrash/battle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace phantomcrash {
namespace {

constexpr float facing_jump = 0.35F;      // radians: a jump link is taken facing this close
constexpr float facing_walk = 1.0F;       // walk on while the waypoint is this close ahead
constexpr float hop_rise = 3.5F;          // a stuck robot's hop holds the jump this far up
constexpr float progress_epsilon = 0.25F; // nearer by this much counts as progress
constexpr float level_slack = 0.5F;       // a waypoint is reached within a step plus this
constexpr float goal_snap_cells = 1.5F;   // a goal this near its node is walked to exactly
constexpr float overshoot = 0.5F;         // in the air, stop pushing on this close
constexpr int smooth_lookahead = 8;       // waypoints a straight walk may skip at once
constexpr int climb_kind = 7;             // ENPT segments that climb (never rejoined)

float flat(Vec3 v) {
  v.y = 0;
  return length(v);
}

float stick(const float error, const float speed, const float seconds) {
  return speed > 0 ? std::clamp(error / (speed * seconds), -1.0F, 1.0F) : 0;
}

// Authored climbs (kind 7 segments) with a point no grid node lies near:
// planned routes cannot follow them.
std::vector<bool> blocked_climbs(const NavGraph &graph, const EnemyPath &path, const float rise) {
  std::vector<bool> blocked(path.segments.size());
  for (std::size_t i = 0; i < path.segments.size(); ++i) {
    const auto &segment = path.segments[i];
    blocked[i] =
        segment.kind == climb_kind && std::ranges::any_of(segment.points, [&](const Vec3 p) {
          const int node = graph.nearest(p);
          return node < 0 ||
                 std::abs(graph.nodes()[static_cast<std::size_t>(node)].position.y - p.y) > rise;
        });
  }
  return blocked;
}

// Drops walk waypoints that a straight walk can skip.
std::vector<NavWaypoint> smooth(const StageCollision &stage, const std::vector<NavWaypoint> &route,
                                Vec3 from, const float step, const float half_width) {
  std::vector<NavWaypoint> kept;
  std::size_t i = 0;
  while (i < route.size()) {
    std::size_t reach = i;
    if (route[i].link == NavLink::walk) {
      const std::size_t last = std::min(route.size() - 1, i + smooth_lookahead);
      for (std::size_t j = last; j > i; --j) {
        const bool walks =
            std::all_of(route.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                        route.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                        [](const NavWaypoint &w) { return w.link == NavLink::walk; });
        if (walks && nav_walkable(stage, from, route[j].position, step, half_width)) {
          reach = j;
          break;
        }
      }
    }
    kept.push_back(route[reach]);
    from = route[reach].position;
    i = reach + 1;
  }
  return kept;
}

} // namespace

void Battle::set_stage(StageCollision stage) {
  stage_ = std::move(stage);
  nav_.reset();
  nav_climbs_.clear();
  nav_future_ = settings_.ai.navigation.enabled ? shared_nav_graph(stage_, settings_.ai.navigation)
                                                : NavGraphFuture{};
}

const NavGraph *Battle::nav_graph() {
  if (!nav_ && nav_future_.valid() &&
      nav_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    nav_ = nav_future_.get();
    nav_climbs_ = blocked_climbs(*nav_, path_, settings_.ai.navigation.point_rise);
  }
  return nav_.get();
}

bool Battle::ai_plan(Combatant &drone, const Vec3 goal) {
  const auto &t = settings_.ai.navigation;
  const NavGraph &graph = *nav_;
  NavAgent &n = drone.ai.nav;
  const Vec3 at = drone.mover.position();
  const int start = graph.nearest(at);
  const int goal_node = graph.nearest(goal);
  if (start >= 0 && goal_node >= 0 && start != goal_node) {
    if (!take_nav_search()) {
      return true; // this tick's searches are spent: keep the route, plan next tick
    }
  }
  n.route.clear();
  n.next = 0;
  n.stall = 0;
  n.best = std::numeric_limits<float>::max();
  n.replan_left = t.replan_seconds;
  n.since_plan = 0;
  n.goal = goal;
  n.goal_node = goal_node;
  n.unreachable = n.goal_node < 0;
  if (start < 0 || n.goal_node < 0) {
    return false;
  }
  if (start != n.goal_node) {
    n.route = graph.path(start, n.goal_node, n.avoided, t.avoid_cost, jump_reach(drone));
    if (n.route.empty()) {
      n.unreachable = true;
      return false;
    }
    // Aside from its start node (pushed off it, or on ground with no node of
    // its own), it walks there first: a jump is taken from its take-off.
    const Vec3 first = graph.nodes()[static_cast<std::size_t>(start)].position;
    if (n.route.front().link != NavLink::walk && flat(first - at) > t.takeoff_reach) {
      n.route.insert(n.route.begin(), {first, NavLink::walk, start});
    }
  }
  // Finish at the goal itself, at its node's height: ENPT points can lie
  // a little under the street.
  const Vec3 end = graph.nodes()[static_cast<std::size_t>(n.goal_node)].position;
  if (flat(goal - end) < t.cell * goal_snap_cells) {
    n.route.push_back({{goal.x, end.y, goal.z}, NavLink::walk, n.goal_node});
  }
  if (!n.rough) {
    n.route = smooth(stage_, n.route, at, t.step, drone.radius * 0.8F);
  }
  return true;
}

bool Battle::ai_navigate(Combatant &drone, const Vec3 goal, const float seconds,
                         const float speed) {
  const auto &t = settings_.ai.navigation;
  if (!t.enabled || nav_graph() == nullptr) {
    return false;
  }
  auto &s = drone.ai;
  NavAgent &n = s.nav;
  n.dropped = false;
  n.unreachable = false;
  n.asked = true;
  if (n.fallback_left > 0) {
    n.fallback_left -= seconds;
    return false; // escalated: steering the original way for now
  }
  for (NavAvoid &a : n.avoided) {
    a.seconds -= seconds;
  }
  std::erase_if(n.avoided, [](const NavAvoid &a) { return a.seconds <= 0; });
  const Vec3 at = drone.mover.position();
  if (ai_escalate(drone, seconds)) {
    return false;
  }
  const bool grounded = drone.mover.grounded();
  n.replan_left -= seconds;
  n.since_plan += seconds;
  n.air_seconds = grounded ? 0.0F : n.air_seconds + seconds;
  if (grounded && n.jumping) {
    n.jumping = false;
    if (at.y < n.jump_land - t.step - level_slack) {
      // Back down below the ledge: this robot does not get up there (an
      // edge it cannot get over). It plans another way for a while.
      n.avoided.push_back({-1, n.jump_to, t.failed_jump_seconds});
      n.route.clear();
      ++n.strikes;
      ++s.recoveries;
    }
    n.jump_to = -1;
  }
  // Wedged in the air on geometry, it plans from where it hangs.
  const bool settled = grounded || n.air_seconds > t.give_up_seconds;
  const bool arrived = !n.route.empty() && n.next >= n.route.size();
  const bool moved = length(goal - n.goal) > t.goal_moved && n.since_plan >= t.replan_min_seconds;
  if (settled && (!n.lifting || !grounded) &&
      (moved || n.route.empty() || (n.replan_left <= 0 && !arrived))) {
    if (!ai_plan(drone, goal)) {
      return false;
    }
  }
  while (n.next < n.route.size()) {
    const NavWaypoint &w = n.route[n.next];
    const bool takeoff = n.next + 1 < n.route.size() && n.route[n.next + 1].link == NavLink::jump;
    const float reach = takeoff ? std::min(t.waypoint_reach, t.takeoff_reach) : t.waypoint_reach;
    if (!grounded || flat(w.position - at) >= reach ||
        std::abs(w.position.y - at.y) > t.step + level_slack) {
      break;
    }
    ++n.next;
    n.stall = 0;
    n.best = std::numeric_limits<float>::max();
  }
  n.active = true;
  // Past the route's end, the rest of the way is walked straight.
  const bool finished = n.next >= n.route.size();
  if (finished && flat(goal - at) <= t.waypoint_reach) {
    s.control.move[1] = 0;
    return true;
  }
  const NavWaypoint w =
      finished ? NavWaypoint{{goal.x, at.y, goal.z}, NavLink::walk, -1} : n.route[n.next];
  const Vec3 to = w.position - at;
  const bool jump = w.link == NavLink::jump;
  const float error = wrap_angle(heading_toward(to) - drone.mover.heading());
  s.control.move[0] = stick(error, radians(drone.mover.tuning().turn_degrees_per_second), seconds);
  const auto press = [&](const float lift_to) {
    // Only a press the robot takes: while it would be ignored (landing, the
    // jump still held) the jump stays released.
    s.control.jump = drone.mover.jump_ready();
    n.lifting = s.control.jump;
    n.lift_to = lift_to;
    return s.control.jump;
  };
  if (grounded) {
    n.lifting = false;
    if (jump) {
      // Line up standing: face the ledge, and walk on only with the press
      // that takes off. Shoving into the wall below the ledge lifts the
      // robot off the ground for a tick, and each landing ignores the jump.
      const bool facing = std::abs(error) < facing_jump;
      const bool takes_off = facing && press(w.position.y + t.jump_hold_over);
      s.control.move[1] = takes_off ? 1.0F : 0.0F;
      if (takes_off) {
        n.jump_to = w.node;
        n.jump_land = w.position.y;
      }
    } else {
      s.control.move[1] = std::abs(error) < facing_walk ? speed : 0.0F;
    }
  } else {
    s.control.move[1] = flat(to) > overshoot ? 1.0F : 0.0F;
    n.jumping = n.jumping || n.jump_to >= 0;
    if (n.lifting && at.y < n.lift_to) {
      s.control.jump = true;
    } else {
      n.lifting = false; // let go once; pressing again would dive
    }
  }
  const float distance = flat(to) + (jump ? std::max(0.0F, w.position.y - at.y) : 0.0F);
  if (distance < n.best - progress_epsilon) {
    n.best = distance;
    n.stall = 0;
  } else {
    n.stall += seconds;
  }
  if (grounded && !jump && n.stall > t.stuck_seconds) {
    if (crowded(drone, to)) {
      s.control.strafe = static_cast<float>(s.dodge_side); // step out of its way
    } else {
      press(at.y + hop_rise);
    }
  }
  if (n.stall > t.give_up_seconds) {
    // First plan again without shortcuts; stuck again, avoid the waypoint.
    if (n.rough && w.node >= 0) {
      n.avoided.push_back({-1, w.node, t.avoid_seconds});
    }
    n.rough = !n.rough;
    n.replan_left = 0;
    n.route.clear();
    n.stall = 0;
    n.best = std::numeric_limits<float>::max();
    n.lifting = false;
    ++n.strikes;
    ++s.recoveries;
  }
  return true;
}

bool Battle::ai_escalate(Combatant &drone, const float seconds) {
  const auto &t = settings_.ai.navigation;
  auto &s = drone.ai;
  NavAgent &n = s.nav;
  const Vec3 at = drone.mover.position();
  if (length(at - n.anchor) > t.escalate_distance) {
    n.anchor = at; // headway
    n.anchor_seconds = 0;
    n.strikes = 0;
    n.escalation = 0;
    return false;
  }
  n.anchor_seconds += seconds;
  if (n.anchor_seconds < t.escalate_seconds && n.strikes < t.escalate_recoveries) {
    return false;
  }
  n.anchor_seconds = 0;
  n.strikes = 0;
  n.route.clear();
  n.stall = 0;
  n.best = std::numeric_limits<float>::max();
  n.lifting = false;
  ++s.recoveries;
  if (n.escalation == 0) {
    n.escalation = 1;
    n.dropped = true; // the caller gives up its goal
  } else {
    n.escalation = 0;
    n.fallback_left = t.fallback_seconds;
  }
  return true;
}

} // namespace phantomcrash
