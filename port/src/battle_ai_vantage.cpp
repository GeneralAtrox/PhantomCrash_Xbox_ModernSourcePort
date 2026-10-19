// Port navigation for computer robots: taking high ground in a fight, a
// spot above the target with a clear shot (rumbling.json live.ai.navigation).
#include "phantomcrash/battle.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {

constexpr float level_slack = 0.5F;         // a spot is reached within a step plus this
constexpr float vantage_rise_weight = 1.5F; // each unit higher is worth this much path

float flat(Vec3 v) {
  v.y = 0;
  return length(v);
}

} // namespace

int Battle::ai_vantage(const Combatant &drone) {
  const auto &t = settings_.ai.navigation;
  const auto &s = drone.ai;
  const NavGraph &graph = *nav_;
  const Combatant &enemy = robots_.at(static_cast<std::size_t>(s.target));
  float range = 0;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    const WeaponTuning *weapon = drone.weapons[slot];
    if (weapon && !drone.lost[slot] && weapon->kind != WeaponKind::melee) {
      range = std::max(range, weapon->range);
    }
  }
  const float reach = std::min(t.vantage_radius, range * t.vantage_range_share);
  if (reach <= t.vantage_nearest) {
    return -1;
  }
  const Vec3 feet = enemy.mover.position();
  const Vec3 aim = enemy.aim_origin();
  std::vector<std::pair<float, int>> candidates;
  for (const auto &[node, cost] : graph.costs_from(graph.nearest(drone.mover.position()),
                                                   t.vantage_path_limit, jump_reach(drone))) {
    const Vec3 p = graph.nodes()[static_cast<std::size_t>(node)].position;
    const float rise = p.y - feet.y;
    const float distance = flat(p - feet);
    const bool avoided = std::ranges::any_of(
        s.nav.avoided, [&](const NavAvoid &a) { return a.from < 0 && a.to == node; });
    if (rise >= t.vantage_rise && distance >= t.vantage_nearest && distance <= reach && !avoided) {
      candidates.emplace_back(cost - std::min(rise, t.jump_rise) * vantage_rise_weight, node);
    }
  }
  std::ranges::sort(candidates);
  const auto tests = std::min(candidates.size(), static_cast<std::size_t>(t.vantage_sight_tests));
  for (std::size_t k = 0; k < tests; ++k) {
    const int node = candidates[k].second;
    const Vec3 eye =
        graph.nodes()[static_cast<std::size_t>(node)].position + Vec3{0.0F, drone.radius, 0.0F};
    if (clear_target_sight(eye, aim)) {
      return node;
    }
  }
  return -1;
}

bool Battle::ai_high_ground(Combatant &drone, const float seconds) {
  const auto &t = settings_.ai.navigation;
  auto &s = drone.ai;
  NavAgent &n = s.nav;
  n.holding = false;
  if (!t.enabled || !t.high_ground || s.target < 0 || nav_graph() == nullptr) {
    return false;
  }
  if (n.vantage_target != s.target) {
    n.vantage_target = s.target;
    n.vantage = -1;
    n.vantage_left = 0;
  }
  n.vantage_left -= seconds;
  // The search waits for a tick with a graph search to spare.
  if (n.vantage_left <= 0 && drone.mover.grounded() && take_nav_search()) {
    n.vantage_left = t.vantage_seconds;
    n.vantage = ai_random(random_) < t.vantage_chance ? ai_vantage(drone) : -1;
  }
  if (n.vantage < 0) {
    return false;
  }
  const Vec3 spot = nav_->nodes()[static_cast<std::size_t>(n.vantage)].position;
  const Vec3 at = drone.mover.position();
  if (flat(spot - at) < t.waypoint_reach * 2.0F && std::abs(spot.y - at.y) < t.step + level_slack) {
    n.holding = true;
    return false; // holding it: fight as usual, without closing in
  }
  if (!ai_navigate(drone, spot, seconds, 1.0F)) {
    if (n.dropped) {
      n.avoided.push_back({-1, n.vantage, t.avoid_seconds}); // not that spot again soon
      n.vantage_left = t.vantage_seconds;
    }
    n.vantage = -1;
    return false;
  }
  return true;
}

} // namespace phantomcrash
