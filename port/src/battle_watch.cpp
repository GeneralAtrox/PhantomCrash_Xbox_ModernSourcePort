// The robots' remarks on their own state: their rounds running down to
// half (0x075773) and standing idle (0x08e480).
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"

namespace phantomcrash {

// 0x08248b: the mean share of their magazines left over the weapons that
// hold rounds (not beams or melee weapons); 1 with none.
float Battle::ammo_share(const Combatant &robot) {
  float total = 0.0F;
  int weapons = 0;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    const WeaponTuning *weapon = robot.weapons[slot];
    const Magazine &magazine = robot.magazines[slot];
    if (weapon == nullptr || robot.lost[slot] || weapon->hud_kind == hud_kind::beam ||
        weapon->hud_kind == hud_kind::melee || !magazine.limited()) {
      continue;
    }
    total += static_cast<float>(magazine.ammo) / static_cast<float>(magazine.max);
    ++weapons;
  }
  return weapons > 0 ? total / static_cast<float>(weapons) : 1.0F;
}

// 0x075773: once, as the share falls through half; with its armour below a
// quarter too, the robot says so.
void Battle::watch_ammo(Combatant &robot) {
  const float before = robot.ammo_share;
  robot.ammo_share = ammo_share(robot);
  if (before > chatter_half_armour && robot.ammo_share <= chatter_half_armour) {
    chatter(robot, robot.hp >= robot.max_hp * chatter_low_armour ? chatter_event::ammo_half
                                                                 : chatter_event::ammo_half_low);
  }
}

// 0x08e480: a robot standing with its pad untouched remarks on it every
// idle_chatter_seconds; anything else starts the count again.
void Battle::watch_idle(Combatant &robot, const RobotControl &control, const FireControl &fire,
                        const float seconds) {
  bool touched = control.move[0] != 0.0F || control.move[1] != 0.0F || control.twist != 0.0F ||
                 control.pitch != 0.0F || control.jump || control.dash || control.dodge != 0 ||
                 control.reset_twist || fire.camouflage;
  for (const bool held : fire.fire) {
    touched = touched || held;
  }
  if (touched || robot.mover.mode() != MoveMode::stand || settings_.idle_chatter_seconds <= 0.0F) {
    robot.idle_seconds = 0.0F;
    return;
  }
  robot.idle_seconds += seconds;
  if (robot.idle_seconds >= settings_.idle_chatter_seconds) {
    robot.idle_seconds = 0.0F;
    chatter(robot, chatter_event::idle);
  }
}

} // namespace phantomcrash
