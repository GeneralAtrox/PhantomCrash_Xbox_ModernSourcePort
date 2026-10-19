#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>

namespace phantomcrash {

void Battle::update_camouflage(Combatant &robot, const bool toggle, const float seconds) {
  const float active = robot.combat.camouflage_seconds >= 0 ? robot.combat.camouflage_seconds
                                                            : settings_.camouflage_seconds;
  const float recharge = robot.combat.camouflage_recharge_seconds >= 0
                             ? robot.combat.camouflage_recharge_seconds
                             : settings_.camouflage_recharge_seconds;
  if (active <= 0) {
    robot.camouflaged = false;
    robot.camouflage = 0;
    return;
  }
  if (toggle) {
    const float minimum = !robot.piloted && settings_.ai.original
                              ? settings_.ai.original->value("cloak_minimum")
                              : 0.1F;
    robot.camouflaged = !robot.camouflaged && robot.camouflage > minimum;
    robot.camouflage_broken = false;
    if (robot.camouflaged) {
      chatter(robot, chatter_event::camouflage);
    }
  }
  const float before = robot.camouflage;
  const float change = robot.camouflaged ? -seconds / active
                       : recharge > 0    ? seconds / recharge
                                         : 1.0F;
  robot.camouflage = std::clamp(before + change, 0.0F, 1.0F);
  if (robot.camouflaged && before > camouflage_low_share &&
      robot.camouflage <= camouflage_low_share) {
    chatter(robot, chatter_event::camouflage_low);
  }
  robot.camouflaged = robot.camouflaged && robot.camouflage > 0;
}

} // namespace phantomcrash
