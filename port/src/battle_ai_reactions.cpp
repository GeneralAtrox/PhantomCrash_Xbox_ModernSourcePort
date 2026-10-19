// Projectile threat response, 0x05d530. Uses the port's live projectiles.
#include "phantomcrash/battle.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
void Battle::ai_reactions(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  s.threat = false;
  const float angle =
      radians(std::min(static_cast<float>(s.ability) + data.value("threat_angle_base"),
                       data.value("threat_angle_max")));
  for (const auto &shot : armory_.projectiles()) {
    if (shot.team == drone.team || !shot.weapon) {
      continue;
    }
    const float speed = length(shot.velocity);
    if (speed <= 0) {
      continue;
    }
    const Vec3 toward = drone.centre(settings_.robot_height) - shot.position;
    const float distance = length(toward);
    if (distance >
        speed * seconds * data.value("threat_ticks_ahead") + data.value("threat_margin")) {
      continue;
    }
    if (distance > 0 && !s.ranker_defensive) {
      const Vec3 incoming = toward * (1 / distance);
      if (dot(incoming, shot.velocity) < 0 ||
          dot(heading_forward(drone.mover.aim_heading()), incoming * -1) < std::cos(angle)) {
        continue;
      }
    }
    if (!clear_target_sight(shot.position, drone.centre(settings_.robot_height))) {
      continue;
    }
    const float chance = s.ranker_defensive
                             ? data.value("defensive_dodge_chance")
                             : static_cast<float>(s.ability) * data.value("dodge_ability_scale");
    if (ai_chance(random_, chance, data.value("probability_scale"))) {
      s.threat = true;
      s.control.dodge = (random_() & 1U) ? 1 : -1;
      ++s.dodges;
      break;
    }
  }
}
} // namespace phantomcrash
