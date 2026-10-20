// Battle: what the hits do (0x0988e1), the knockback they leave, and
// robots shot down.
#include "phantomcrash/battle.hpp"

#include "phantomcrash/battle_beam_trace.hpp"
#include "phantomcrash/battle_chatter.hpp"
#include "phantomcrash/money.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace phantomcrash {
namespace {

// A dodge leaves the robot untouchable for 30 frames (0x0988e1).
constexpr float dodge_invulnerable_seconds = 30.0F / part_frames_per_second;
// A hit travelling within 45 degrees of the way its victim faces struck it
// from behind, and does three times the damage.
constexpr float behind_cosine = 0.7071F;
constexpr float behind_factor = 3.0F;
constexpr float critical_factor = 2.0F;
constexpr float percent = 100.0F;
// Knockback carries a robot at most 5 units a tick and fades to 0.8 of
// itself every tick (robot +0x27c4, 0x07c34e).
constexpr float push_cap = 5.0F * battle_ticks_per_second;
constexpr float push_fade_per_tick = 0.8F;
constexpr float push_step_share = 0.5F; // a push moves at most this share of the radius a step
constexpr float push_rest = 0.01F;      // slower than this, it stops
// A placed robot with no ground within its height above looks this many
// heights up before it is left to fall (a port reading).
constexpr float deep_placement_heights = 3.0F;

} // namespace

void Battle::set_marker(const int index, const bool leader, const int player_number) {
  Combatant &robot = robots_.at(static_cast<std::size_t>(index));
  robot.leader = leader;
  robot.player_number = player_number;
}

void Battle::set_boss(const int index, const bool boss) {
  robots_.at(static_cast<std::size_t>(index)).boss = boss;
}

void Battle::set_combat(const int index, const CombatStats &combat) {
  robots_.at(static_cast<std::size_t>(index)).combat = combat;
}

// Tells the robot's chatter what happened to it (0x049470).
void Battle::chatter(const Combatant &robot, const int event) {
  const auto index = static_cast<int>(&robot - robots_.data());
  events_.push_back({index, BattleEventKind::chatter, event});
}

float Battle::hit_damage(const WeaponHit &hit, Combatant &victim) {
  if (victim.invulnerable > 0.0F) {
    return 0.0F;
  }
  std::uniform_real_distribution<float> unit(0.0F, 1.0F);
  if (unit(random_) < victim.combat.dodge_chance) {
    victim.invulnerable = dodge_invulnerable_seconds;
    chatter(victim, chatter_event::dodge);
    return 0.0F;
  }
  float damage = hit.damage;
  if (hit.directional &&
      dot(hit.direction, heading_forward(victim.mover.heading())) > behind_cosine) {
    damage *= behind_factor;
    chatter(victim, chatter_event::hit_from_behind);
  }
  // 0x0988e1: a beam loses the share the modules' laser reduction takes,
  // the share's damage truncated before it comes off (0x1138ec).
  if (hit.weapon != nullptr && hit.weapon->hud_kind == hud_kind::beam) {
    damage -= std::trunc(damage * std::clamp(victim.combat.beam_resistance, 0.0F, 1.0F));
  }
  const bool attacker = hit.attacker >= 0 && hit.attacker < static_cast<int>(robots_.size());
  const float critical =
      attacker ? robots_[static_cast<std::size_t>(hit.attacker)].combat.critical_percent : 0.0F;
  if (unit(random_) * percent < critical) {
    damage *= critical_factor;
    chatter(victim, chatter_event::critical_hit);
    const float knockdown = hit.weapon != nullptr ? hit.weapon->knockdown_percent : 0.0F;
    if (knockdown - victim.combat.knockdown_resistance > unit(random_) * percent) {
      knock_off_weapon(victim, hit.direction);
    }
  }
  return damage;
}

// A knockdown knocks off the arm or the shoulder weapon, either at even
// odds, on the side the hit struck. The robot remarks on it (0x08d686):
// below a quarter of its armour, on the loss of an arm, or of its last.
void Battle::knock_off_weapon(Combatant &victim, const Vec3 direction) {
  const bool right = dot(direction, heading_right(victim.mover.heading())) < 0.0F;
  std::uniform_int_distribution<int> coin(0, 1);
  const bool shoulder = coin(random_) == 1;
  const WeaponSlot mount = shoulder
                               ? (right ? WeaponSlot::right_shoulder : WeaponSlot::left_shoulder)
                               : (right ? WeaponSlot::right_arm : WeaponSlot::left_arm);
  const auto slot = static_cast<std::size_t>(mount);
  if (victim.weapons[slot] == nullptr || victim.lost[slot]) {
    return;
  }
  victim.lost[slot] = true;
  const auto arm = [&](const WeaponSlot side) {
    return victim.weapons[static_cast<std::size_t>(side)] != nullptr &&
           !victim.lost[static_cast<std::size_t>(side)];
  };
  if (victim.hp / victim.max_hp < chatter_low_armour) {
    chatter(victim, chatter_event::knockdown_low_armour);
  } else if (!shoulder) {
    const bool arm_left = arm(WeaponSlot::right_arm) || arm(WeaponSlot::left_arm);
    chatter(victim, arm_left ? chatter_event::arm_lost : chatter_event::arms_lost);
  }
}

// Armour falling below a quarter, or else below half (0x07675e); the
// quarter has its own line when the other side is hitting.
void Battle::armour_chatter(const Combatant &victim, const float damage) {
  const float before = victim.hp / victim.max_hp;
  const float after = (victim.hp - damage) / victim.max_hp;
  if (before > chatter_low_armour && after <= chatter_low_armour) {
    bool by_rival = false;
    for (std::size_t index = 0; index < victim.damage_from.size(); ++index) {
      by_rival = by_rival || (victim.damage_from[index] > 0.0F && robots_[index].rival);
    }
    chatter(victim, by_rival ? chatter_event::quarter_by_rival : chatter_event::quarter);
  } else if (before > chatter_half_armour && after <= chatter_half_armour) {
    chatter(victim, chatter_event::half);
  }
}

// 0x047ed9: a melee weapon or a beam striking a robot has its owner cry out.
void Battle::strike_chatter(const Combatant &attacker, const WeaponTuning &weapon) {
  if (weapon.hud_kind == hud_kind::melee) {
    chatter(attacker, chatter_event::melee_strike);
  } else if (weapon.hud_kind == hud_kind::beam) {
    chatter(attacker, chatter_event::beam_strike);
  }
}

// A landed hit tells its attacker HIT, pays it hit_reward_ny and counts
// toward who shot the victim down (0x09d2ce).
void Battle::apply_hits(const std::vector<WeaponHit> &hits) {
  const auto count = robots_.size();
  std::vector<float> piloted_damage(count, 0.0F);
  for (const WeaponHit &hit : hits) {
    if (hit_wreck(hit) || hit_object(hit) || hit_supply(hit)) {
      continue;
    }
    const auto victim_index = static_cast<std::size_t>(hit.victim);
    if (victim_index >= robots_.size()) {
      continue;
    }
    Combatant &victim = robots_[victim_index];
    if (hit.attacker == 0 && beam_trace_enabled()) {
      beam_trace_line(std::format(
          "apply victim={} weapon={} hp={:.1f} invulnerable={:.2f} shield={:.2f} alive={} "
          "directional={}",
          hit.victim, hit.weapon != nullptr ? hit.weapon->name : std::string("-"), victim.hp,
          victim.invulnerable, victim.shield, victim.alive, hit.directional));
    }
    if (!victim.alive || victim.shield > 0.0F) {
      continue; // shielded while it enters or leaves (entry_shield.hpp)
    }
    if (players_spared_ && victim.piloted) {
      continue; // a boss's entrance holds the camera (BattleScene::start_walking)
    }
    const bool attacker = hit.attacker >= 0 && hit.attacker < static_cast<int>(count);
    if (cooperative_ && attacker && hit.attacker != hit.victim && victim.piloted &&
        robots_[hit.attacker].piloted) {
      continue; // All damage, knockback and stun are blocked between co-op players.
    }
    if (attacker && hit.attacker != hit.victim && hit.weapon != nullptr) {
      strike_chatter(robots_[static_cast<std::size_t>(hit.attacker)], *hit.weapon);
    }
    const float damage = hit_damage(hit, victim);
    if (victim.invulnerable > 0.0F) {
      continue; // dodged
    }
    if (attacker && damage > 0.0F && hit.attacker != hit.victim) {
      BattleEvent landed{hit.attacker, BattleEventKind::hit};
      landed.amount = static_cast<float>(settings_.hit_reward_ny / ny_per_stored_unit);
      events_.push_back(landed);
      victim.damage_from.resize(count, 0.0F);
      victim.damage_from[static_cast<std::size_t>(hit.attacker)] += damage;
    }
    if (damage > 0.0F) {
      armour_chatter(victim, damage);
    }
    victim.hp -= damage;
    if (cheats_.never_die && victim.piloted) {
      victim.hp = std::max(victim.hp, 1.0F);
    }
    if (damage > 0.0F && victim.camouflaged) {
      // 0x07675e: damage breaks a running camouflage (0x075444, state 7).
      victim.camouflaged = false;
      victim.camouflage_broken = true;
    }
    if (victim.piloted && damage > 0.0F) {
      piloted_damage[victim_index] += damage;
    }
    if (!hit.directional && damage > 0.0F && victim.hp > 0.0F) {
      chatter(victim, chatter_event::blast); // 0x09c9c2: a blast it lived through
    }
    victim.hit_flash = settings_.hit_flash_seconds;
    if (hit.stagger > 0.0F) {
      victim.mover.stagger(hit.stagger, hit.direction);
    }
    if (hit.weapon != nullptr) {
      const float resisted =
          std::clamp(victim.combat.knockback_resistance, 0.0F, percent) / percent;
      victim.push = victim.push + hit.direction * (hit.weapon->knockback * (1.0F - resisted));
    }
    if (victim.hp <= 0.0F) {
      shot_down(hit.victim);
    }
  }
  for (std::size_t index = 0; index < count; ++index) {
    if (piloted_damage[index] > 0.0F) {
      BattleEvent event{static_cast<int>(index), BattleEventKind::damaged};
      event.amount = piloted_damage[index];
      events_.push_back(event);
    }
  }
}

// Runs a robot's timers down and carries it on its knockback, before the
// stage pushes it back out of walls.
void Battle::recover(Combatant &robot, const float seconds) const {
  robot.invulnerable = std::max(0.0F, robot.invulnerable - seconds);
  robot.shield = std::max(0.0F, robot.shield - seconds);
  robot.shield_glow = entry_shield_glow(settings_.entry_shield, robot.shield_glow, robot.shield,
                                        robot.hp >= 1.0F, seconds);
  const float speed = length(robot.push);
  if (speed < push_rest) {
    robot.push = {};
    return;
  }
  if (speed > push_cap) {
    robot.push = robot.push * (push_cap / speed);
  }
  robot.mover.set_position(carried(robot, robot.mover.position(), robot.push * seconds));
  robot.push = robot.push * std::pow(push_fade_per_tick, seconds * battle_ticks_per_second);
}

// A hard hit carries a robot up to push_cap a tick, farther than its radius:
// in one step it went past a thin wall's middle and the stage pushed it out
// the far side (Shinjuku near (186, -3, 5) put a robot outside the arena for
// 130 s). Two robots landing on each other part as far, and that put one
// through Shibuya's pit wall into the solid under its ledge for 250 s. A long
// move goes in steps of part of the radius, the stage pushing the robot out
// of walls after each.
Vec3 Battle::carried(const Combatant &robot, Vec3 at, const Vec3 carry) const {
  const int steps =
      std::max(1, static_cast<int>(std::ceil(length(carry) / (robot.radius * push_step_share))));
  if (steps == 1) {
    return at + carry; // collide() pushes it out of the walls this tick
  }
  for (int step = 0; step < steps; ++step) {
    Vec3 foot = at + carry * (1.0F / static_cast<float>(steps)) + Vec3{0.0F, robot.radius, 0.0F};
    stage_.push_sphere(foot, robot.radius);
    at = foot - Vec3{0.0F, robot.radius, 0.0F};
  }
  return at;
}

// A port safety net. Inside a building's footprint the stage has no ground
// (0x053337 answers -1000 there too), so a push past a wall dropped a robot
// into the void, alive and out of reach. It goes back to the last place it
// stood with ground under it instead.
float Battle::stage_floor(Combatant &robot) const {
  RobotMover &mover = robot.mover;
  const float floor =
      stage_.ground_height(mover.position(), ground_snap, robot.radius, &robot.floor_surface);
  if (floor != no_ground) {
    // Only a place it stands on that floor: propped on a wall's edge above
    // it, it is grounded too, but put back there it falls past the edge
    // again, never lands and so never jumps out (Shinjuku near
    // (120, 1, -37) held a robot that way for 200 s).
    if (mover.grounded() && mover.position().y <= floor + ground_snap) {
      robot.last_ground = mover.position();
    }
    return floor;
  }
  if (!robot.last_ground || mover.mode() == MoveMode::scripted ||
      mover.mode() == MoveMode::destroyed) {
    return floor;
  }
  robot.push = {};
  const Vec3 back = *robot.last_ground;
  const float ground = stage_.ground_height(back, ground_snap, robot.radius, &robot.floor_surface);
  mover.set_floor(ground);
  mover.place(back, mover.heading());
  return ground;
}

Vec3 Battle::placed(Vec3 position, const float radius, RobotMover &mover) const {
  // Enemy path points can lie a little under the street (ENPT heights of
  // -4.0 over ground at -3), so a placed robot stands on the highest
  // ground below its top (a port reading).
  float floor =
      stage_.ground_height(position, std::max(settings_.robot_height, ground_snap), radius);
  if (floor == no_ground) {
    // Deeper still under a ramp (Shinjuku's ENPT -4.0 under a street rising
    // to +1): look higher before letting it fall through the stage.
    floor = stage_.ground_height(position, settings_.robot_height * deep_placement_heights, radius);
  }
  mover.set_floor(floor);
  position.y = std::max(position.y, floor);
  return position;
}

void Battle::withdraw(const int index) {
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  release(index);
  robot.alive = false;
  robot.dying = false;
  robot.camouflaged = false;
  robot.push = {};
  robot.respawn_left = settings_.drone_respawn_seconds;
}

// A robot shot down (0x07675e): it is told so when others damaged it, and
// every robot that damaged it scores the kill and is told BINGO!. Each
// remarks on it: the victim by who shot it, a player's robot on the
// battle's first rival shot down or else by its own armour.
void Battle::shot_down(const int victim_index) {
  Combatant &victim = robots_[static_cast<std::size_t>(victim_index)];
  victim.alive = false;
  victim.dying = true;
  victim.death_seconds = 0.0F;
  victim.mover.destroy();
  victim.firing.fill(false);
  victim.locks.fill(-1);
  victim.lock_candidates.fill(-1);
  victim.queued_rounds.fill(0);
  victim.shield = victim.shield_glow = victim.camouflage_sweep = 0.0F;
  pose_robot(victim_index, 0.0F);
  victim.camouflaged = false;
  victim.push = {};
  victim.respawn_left = settings_.drone_respawn_seconds;
  ++destroyed_;
  BattleEvent wreck{victim_index, BattleEventKind::destroyed};
  wreck.at = victim.aim_origin();
  events_.push_back(wreck);
  const auto most = std::ranges::max_element(victim.damage_from);
  victim.shot_by = most != victim.damage_from.end() && *most > 0.0F
                       ? static_cast<int>(most - victim.damage_from.begin())
                       : -1;
  bool shot = false;
  for (std::size_t index = 0; index < victim.damage_from.size(); ++index) {
    if (victim.damage_from[index] <= 0.0F) {
      continue;
    }
    shot = true;
    Combatant &killer = robots_[index];
    ++killer.kills;
    events_.push_back({static_cast<int>(index), BattleEventKind::bingo});
    chatter(victim, killer.rival ? chatter_event::shot_down_by_rival : chatter_event::shot_down);
    if (!killer.piloted) {
      continue;
    }
    if (victim.rival && !first_kill_told_) {
      chatter(killer, chatter_event::first_kill);
    } else {
      chatter(killer, killer.hp / killer.max_hp >= chatter_low_armour
                          ? chatter_event::kill
                          : chatter_event::kill_low_armour);
    }
    first_kill_told_ = true;
  }
  if (shot) {
    events_.push_back({victim_index, BattleEventKind::shot_down});
  }
  victim.damage_from.clear();
}

} // namespace phantomcrash
