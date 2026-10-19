// Supply attraction: original 0x05949b, 0x0678f8 and 0x059a1d.
#include "phantomcrash/battle.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
void Battle::ai_pickup(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  if (!supply_ || s.mode == AiMode::route_special) {
    return;
  }
  const auto &data = *settings_.ai.original;
  const auto &pickups = supply_->pickups();
  const auto available = [](const SupplyPickup &p) {
    return p.state == SupplyState::falling || p.state == SupplyState::landed;
  };
  if (s.pickup >= 0) {
    s.pickup_left -= seconds;
    if (s.pickup >= static_cast<int>(pickups.size()) || !available(pickups[s.pickup]) ||
        s.pickup_left <= 0) {
      s.pickup = -1;
      ai_enter(s, AiMode::route, data, random_);
      return;
    }
  } else {
    if (s.target >= 0) {
      return;
    }
    float nearest = data.value("pickup_range");
    int candidate = -1;
    for (std::size_t i = 0; i < pickups.size(); ++i) {
      if (!available(pickups[i])) {
        continue;
      }
      const float distance = length(pickups[i].position - drone.mover.position());
      if (distance < nearest) {
        nearest = distance;
        candidate = static_cast<int>(i);
      }
    }
    // Original selects the nearest first, then rejects money for ability >=31
    // (except in a showcase, quest +0x618c 1). The original tests the kind's
    // number (money is 0..3); the port its effect, so added kinds count.
    const auto &kinds = supply_->settings().kinds;
    const auto money = [&](const int kind) {
      return kind >= 0 && kind < static_cast<int>(kinds.size()) &&
             kinds[static_cast<std::size_t>(kind)].effect == SupplyEffect::money;
    };
    if (candidate < 0 ||
        (!settings_.ai.showcase && s.ability >= data.value("pickup_money_ability") &&
         money(pickups[candidate].kind))) {
      return;
    }
    s.pickup = candidate;
    s.pickup_left = data.value("pickup_timeout");
    ++s.pickup_selections;
  }
  const auto &pickup = pickups[s.pickup];
  const Vec3 offset = pickup.position - drone.mover.position();
  const float distance = std::hypot(offset.x, offset.z);
  const float error = wrap_angle(heading_toward(offset) - drone.mover.heading());
  const float turn = radians(drone.mover.tuning().turn_degrees_per_second) * seconds;
  s.control = {};
  s.control.reset_twist = true;
  s.control.move[0] = turn > 0 ? std::clamp(error / turn, -1.0F, 1.0F) : 0;
  if (std::abs(error) < data.value("pickup_full_speed_angle")) {
    s.control.move[1] = 1;
  } else if (std::abs(error) < data.value("route_turn_radians") &&
             distance >= data.value("pickup_slow_distance")) {
    s.control.move[1] = data.value("route_walk");
  }
  const bool overhead = distance < drone.radius + data.value("pickup_overhead_radius") &&
                        offset.y > data.value("pickup_overhead_height");
  if (overhead) {
    s.control.move[1] = 0;
  }
  s.control.jump = overhead || (drone.stage_contact & stage_contact::wall) != 0;
  s.control.dash = s.control.jump;
  // The native supply collision handler performs collection for every robot.
}
} // namespace phantomcrash
