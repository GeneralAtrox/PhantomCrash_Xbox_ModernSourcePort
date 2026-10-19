// Reads the navigation tuning (rumbling.json live.ai.navigation; nav_graph.hpp).
#include "phantomcrash/nav_graph.hpp"

#include <algorithm>

namespace phantomcrash {

NavTuning parse_nav_tuning(const json::JsonValue &value) {
  NavTuning t;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  t.enabled = value.boolean_or("enabled", t.enabled);
  t.cell = std::max(0.5F, number("cell", t.cell));
  t.step = number("step", t.step);
  t.jump_rise = number("jump_rise", t.jump_rise);
  t.jump_cells = std::clamp(static_cast<int>(number("jump_cells", 3.0F)), 1, 8);
  t.headroom = number("headroom", t.headroom);
  t.wall_clearance = number("wall_clearance", t.wall_clearance);
  t.jump_cost = number("jump_cost", t.jump_cost);
  t.drop_cost = number("drop_cost", t.drop_cost);
  t.waypoint_reach = number("waypoint_reach", t.waypoint_reach);
  t.replan_seconds = number("replan_seconds", t.replan_seconds);
  t.stuck_seconds = number("stuck_seconds", t.stuck_seconds);
  t.give_up_seconds = number("give_up_seconds", t.give_up_seconds);
  t.avoid_seconds = number("avoid_seconds", t.avoid_seconds);
  t.avoid_cost = number("avoid_cost", t.avoid_cost);
  t.goal_moved = number("goal_moved", t.goal_moved);
  t.replan_min_seconds = number("replan_min_seconds", t.replan_min_seconds);
  t.plans_per_tick =
      std::max(0, static_cast<int>(number("plans_per_tick", static_cast<float>(t.plans_per_tick))));
  t.high_ground = value.boolean_or("high_ground", t.high_ground);
  t.vantage_rise = number("vantage_rise", t.vantage_rise);
  t.vantage_radius = number("vantage_radius", t.vantage_radius);
  t.vantage_nearest = number("vantage_nearest", t.vantage_nearest);
  t.vantage_range_share = number("vantage_range_share", t.vantage_range_share);
  t.vantage_path_limit = number("vantage_path_limit", t.vantage_path_limit);
  t.vantage_seconds = number("vantage_seconds", t.vantage_seconds);
  t.vantage_chance = number("vantage_chance", t.vantage_chance);
  t.vantage_sight_tests = static_cast<int>(number("vantage_sight_tests", 48.0F));
  t.rejoin_sight_tests = std::max(
      1, static_cast<int>(number("rejoin_sight_tests", static_cast<float>(t.rejoin_sight_tests))));
  t.body_radius = number("body_radius", t.body_radius);
  t.takeoff_reach = number("takeoff_reach", t.takeoff_reach);
  t.point_rise = number("point_rise", t.point_rise);
  t.escalate_seconds = number("escalate_seconds", t.escalate_seconds);
  t.escalate_recoveries = std::max(
      1,
      static_cast<int>(number("escalate_recoveries", static_cast<float>(t.escalate_recoveries))));
  t.escalate_distance = number("escalate_distance", t.escalate_distance);
  t.fallback_seconds = number("fallback_seconds", t.fallback_seconds);
  t.jump_margin = std::max(0.0F, number("jump_margin", t.jump_margin));
  t.jump_hold_over = number("jump_hold_over", t.jump_hold_over);
  t.route_skips =
      std::max(1, static_cast<int>(number("route_skips", static_cast<float>(t.route_skips))));
  t.failed_jump_seconds = number("failed_jump_seconds", t.failed_jump_seconds);
  t.crowd_gap = number("crowd_gap", t.crowd_gap);
  return t;
}

} // namespace phantomcrash
