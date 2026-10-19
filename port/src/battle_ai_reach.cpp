// Port navigation: what a computer robot can reach on the grid, and who
// stands in its way (nav_graph.hpp; rumbling.json live.ai.navigation).
#include "phantomcrash/battle.hpp"

#include <cmath>

namespace phantomcrash {
namespace {

constexpr float crowd_ahead = 0.3F; // a robot this far ahead (cosine) is in the way

} // namespace

float Battle::jump_reach(const Combatant &drone) const {
  const auto &t = settings_.ai.navigation;
  // Legs and load set how high the robot jumps; the grid's jump links go up
  // to jump_rise for any robot.
  return std::min(t.jump_rise, held_jump_rise(drone.mover.tuning()) - t.jump_margin);
}

bool Battle::crowded(const Combatant &drone, const Vec3 to) const {
  const float gap = settings_.ai.navigation.crowd_gap;
  const Vec3 at = drone.mover.position();
  const Vec3 ahead = normalize(Vec3{to.x, 0.0F, to.z});
  for (const Combatant &other : robots_) {
    if (&other == &drone || !other.alive || other.dying) {
      continue;
    }
    Vec3 offset = other.mover.position() - at;
    offset.y = 0;
    const float distance = length(offset);
    if (distance - drone.radius - other.radius < gap &&
        dot(offset, ahead) > crowd_ahead * distance) {
      return true;
    }
  }
  return false;
}

const std::vector<bool> *Battle::planned_climbs() {
  return settings_.ai.navigation.enabled && nav_graph() != nullptr ? &nav_climbs_ : nullptr;
}

std::vector<bool> Battle::nav_reach(const Combatant &drone) {
  std::vector<bool> reach;
  if (!settings_.ai.navigation.enabled || nav_graph() == nullptr) {
    return reach;
  }
  return nav_->reachable_from(nav_->nearest(drone.mover.position()), jump_reach(drone));
}

bool Battle::take_nav_search() {
  if (nav_plans_left_ <= 0) {
    return false;
  }
  --nav_plans_left_;
  return true;
}

bool Battle::nav_reaches(const std::vector<bool> &reach, const Vec3 point) const {
  if (reach.empty()) {
    return true;
  }
  const int node = nav_->nearest(point);
  return node >= 0 && reach[static_cast<std::size_t>(node)] &&
         std::abs(nav_->nodes()[static_cast<std::size_t>(node)].position.y - point.y) <=
             settings_.ai.navigation.point_rise;
}

} // namespace phantomcrash
