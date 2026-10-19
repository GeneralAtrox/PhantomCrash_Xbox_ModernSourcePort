// Battle: locking on, firing, and what the hits do.
#include "phantomcrash/battle.hpp"
#include "phantomcrash/robot_combat.hpp"
#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace phantomcrash {
namespace {

// A homing missile's rounds leave 10 frames apart (0x09b4ed).
constexpr float missile_round_seconds = 10.0F / part_frames_per_second;

// A strike motion's frames when the robot has no timing table for it (the
// port's earlier values: armed half a second, over in 0.8).
// Float error in a cooldown counted down by frame times.
constexpr float cooldown_slack = 1e-4F;

constexpr float fallback_strike_armed_frames = 30.0F;
constexpr float fallback_strike_last_frames = 48.0F;

// Where a melee strike lunges: the locked robot, once the lock has
// completed (0x0520f0 sets the lock's +0x19 when the fire control's lock
// time has run out, and robot +0x25a8 to the target).
std::optional<Vec3> lunge_target(const Combatant &robot, const std::size_t slot,
                                 const std::vector<Combatant> &robots) {
  const int lock = robot.locks[slot];
  if (lock < 0 || lock >= static_cast<int>(robots.size()) ||
      robot.lock_held[slot] < robot.lock_seconds) {
    return std::nullopt;
  }
  const Combatant &target = robots[static_cast<std::size_t>(lock)];
  if (!target.alive || target.dying) {
    return std::nullopt;
  }
  return target.mover.position();
}

// 0x09c337: the arm's strike motion with its frames from the leg type's
// timing table (0x08ddb2), lunging when the lock has completed.
StrikeStart strike_start(const Combatant &robot, const WeaponTuning &weapon, const std::size_t slot,
                         const std::vector<Combatant> &robots) {
  StrikeStart start;
  start.motion =
      strike_motion(weapon.model, slot < 2 ? static_cast<int>(slot) : 0, robot.mover.grounded());
  start.lunge_at = lunge_target(robot, slot, robots);
  if (slot < 2 && (weapon.source_melee_kind == 6 || weapon.source_melee_kind == 7)) {
    start.source_free_arm_action = !start.lunge_at;
    if (robot.pose && robot.pose->assembly().source_melee_motions.size() == 8) {
      const int action = (weapon.source_melee_kind == 6 ? 39 : 35) + (start.lunge_at ? 2 : 0) +
                         static_cast<int>(slot);
      start.motion = robot.pose->assembly().source_melee_motions[action - 35];
    }
  }
  start.armed_frames = fallback_strike_armed_frames;
  start.last_frames = fallback_strike_last_frames;
  if (robot.pose) {
    const auto &timings = robot.pose->assembly().timings;
    if (start.motion < static_cast<int>(timings.size())) {
      const MotionTiming &timing = timings[static_cast<std::size_t>(start.motion)];
      if (timing.last > 0.0F) {
        start.armed_frames = timing.loop_end;
        start.last_frames = timing.last;
      }
    }
  }
  start.piloted = robot.piloted;
  return start;
}

// A trigger pull fires the weapon's rounds, each spending one round of
// ammunition, but a machine gun spends one a pull (0x09b4ed). Returns how
// many it fires: no more than are left.
int spend_rounds(const WeaponTuning &weapon, Magazine &magazine, const float seconds) {
  if (magazine.recharging()) {
    // A beam's charge: one a frame it fires (0x09b9fe), two a battle tick.
    const int frames = std::max(static_cast<int>(std::lround(seconds * part_frames_per_second)), 1);
    for (int frame = 0; frame < frames && !magazine.empty(); ++frame) {
      magazine.take();
    }
    return std::max(weapon.rounds, 1);
  }
  const int rounds = std::max(weapon.rounds, 1);
  if (weapon.hud_kind == hud_kind::machine_gun || !magazine.limited()) {
    magazine.take();
    return rounds;
  }
  const int fired = std::min(rounds, magazine.ammo);
  for (int round = 0; round < fired; ++round) {
    magazine.take();
  }
  return fired;
}

bool fires_one_at_a_time(const WeaponTuning &weapon) {
  return weapon.kind == WeaponKind::missile && weapon.homing;
}

// A robot's weapon on `slot`, or null when it has none or lost it.
const WeaponTuning *weapon_on(const Combatant &robot, const std::size_t slot) {
  return robot.lost[slot] ? nullptr : robot.weapons[slot];
}

} // namespace

std::vector<Target> Battle::targets() const {
  std::vector<Target> out;
  out.reserve(robots_.size());
  for (const Combatant &robot : robots_) {
    out.push_back({robot.centre(settings_.robot_height), robot.radius, robot.team, robot.alive});
  }
  add_object_targets(out);
  add_supply_targets(out);
  add_wreck_targets(out);
  return out;
}

void Battle::set_lock_seconds(const int index, const float seconds) {
  robots_.at(static_cast<std::size_t>(index)).lock_seconds = std::max(seconds, 0.0F);
}

void Battle::set_sight(const int index, const SightBoxes &boxes) {
  robots_.at(static_cast<std::size_t>(index)).sight = boxes;
}

int Battle::player_lock(const WeaponSlot slot) const {
  return robots_.empty() ? -1 : player().locks[static_cast<std::size_t>(slot)];
}

int Battle::player_lock_candidate(const WeaponSlot slot) const {
  return robots_.empty() ? -1 : player().lock_candidates[static_cast<std::size_t>(slot)];
}

float Battle::player_lock_held(const WeaponSlot slot) const {
  return robots_.empty() ? 0.0F : player().lock_held[static_cast<std::size_t>(slot)];
}

void Battle::update_lock(Combatant &robot, const std::size_t slot, const float seconds) {
  const WeaponTuning *weapon = weapon_on(robot, slot);
  const int candidate =
      weapon != nullptr ? find_lock(robot, *weapon, slot, robot.lock_candidates[slot]) : -1;
  if (candidate != robot.lock_candidates[slot]) {
    robot.lock_candidates[slot] = candidate;
    robot.lock_held[slot] = 0.0F;
  } else if (candidate >= 0) {
    robot.lock_held[slot] += seconds;
  }
  const bool held = weapon != nullptr &&
                    (!needs_lock_time(*weapon) || robot.lock_held[slot] >= robot.lock_seconds);
  const int before = robot.locks[slot];
  robot.locks[slot] = held ? candidate : -1;
  // 0x051045: a missile lock just made on a player's robot warns it (robot
  // 0 is the player's).
  if (robot.locks[slot] == 0 && before != 0 && lock_sights(*weapon)) {
    chatter(robot, chatter_event::locked_on);
  }
}

void Battle::reset_fire_inputs(Combatant &robot) {
  robot.firing.fill(false);
  robot.trigger_held.fill(false);
}

// The muzzle's flash and sound (0x098bb7, 0x099c02): once a trigger pull,
// and once more for each round of a missile volley.
void Battle::fired(const int index, const std::size_t slot, const WeaponTuning &weapon) {
  const Combatant &robot = robots_[static_cast<std::size_t>(index)];
  if (robot.drone && settings_.ai.original) {
    ++robots_[static_cast<std::size_t>(index)].ai.attacks;
  }
  BattleEvent event{index, BattleEventKind::fired};
  event.slot = static_cast<int>(slot);
  event.weapon = &weapon;
  event.at = muzzle(robot, static_cast<WeaponSlot>(slot));
  event.direction = muzzle_direction(robot, static_cast<WeaponSlot>(slot));
  events_.push_back(event);
}

void Battle::fire_weapons(const int index, const FireControl &fire, const float seconds) {
  Combatant &robot = robots_[static_cast<std::size_t>(index)];
  if (!robot.mover.striking()) {
    robot.source_free_arm_action = false;
  }
  const std::vector<Target> bodies = targets();
  std::vector<WeaponHit> hits;
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    robot.cooldown[slot] =
        robot.cooldown[slot] - seconds < cooldown_slack ? 0.0F : robot.cooldown[slot] - seconds;
    Magazine &magazine = robot.magazines[slot];
    const bool beam_live = armory_.beam_live(index, static_cast<int>(slot));
    magazine.tick(seconds, !beam_live);
    // Optional, off by default: an empty weapon refills after a while
    // (port choice; the original refills only from supply pickups).
    if (settings_.ammo_refill_seconds > 0.0F && magazine.empty() && !magazine.recharging() &&
        magazine.dry_seconds >= settings_.ammo_refill_seconds) {
      magazine.refill(magazine.max);
    }
    update_lock(robot, slot, seconds);
    const bool allowed = index != 0 || (allowed_weapons_ & (1U << slot)) != 0;
    robot.trigger_held[slot] = weapon_on(robot, slot) != nullptr && allowed && fire.fire[slot];
    robot.firing[slot] = weapon_on(robot, slot) != nullptr && allowed &&
                         ((fire.fire[slot] && !magazine.empty()) || robot.queued_rounds[slot] > 0);
  }
  pose_robot(index, seconds);
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    Magazine &magazine = robot.magazines[slot];
    const WeaponTuning *weapon = weapon_on(robot, slot);
    const auto mount = static_cast<WeaponSlot>(slot);
    if (weapon == nullptr) {
      robot.queued_rounds[slot] = 0;
    } else if (robot.queued_rounds[slot] > 0) {
      robot.queue_left[slot] -= seconds;
      for (; robot.queued_rounds[slot] > 0 && robot.queue_left[slot] <= 0.0F;
           --robot.queued_rounds[slot]) {
        if (muzzle_blocked(robot, mount)) {
          robot.queued_rounds[slot] = 0;
          break;
        }
        armory_.fire(*weapon, index, robot.team, muzzle(robot, mount),
                     muzzle_direction(robot, mount), robot.locks[slot], bodies, 1,
                     static_cast<int>(slot));
        fired(index, slot, *weapon);
        if (robot.pose) {
          robot.pose->advance_muzzle(slot);
        }
        robot.queue_left[slot] += missile_round_seconds;
      }
    }
    // The mask is the player's; drones fire what they carry.
    const bool allowed = index != 0 || (allowed_weapons_ & (1U << slot)) != 0;
    if (robot.pose && !robot.pose->ready(slot)) {
      robot.firing[slot] = false;
      continue;
    }
    const bool beam = weapon != nullptr && weapon->hud_kind == hud_kind::beam;
    // 0x09ba08: a new beam starts only on a full charge; one still held or
    // fading carries on while a round is left.
    if (beam && magazine.limited() && magazine.ammo < magazine.max &&
        !armory_.beam_live(index, static_cast<int>(slot))) {
      robot.firing[slot] = false;
      continue;
    }
    if (beam && fire.fire[slot] && allowed && robot.cooldown[slot] > 0.0F && !magazine.empty() &&
        !muzzle_blocked(robot, mount)) {
      // Between pulls the beam stays on its muzzle (0x09b9fe).
      armory_.hold_beam(*weapon, index, static_cast<int>(slot), robot.team, muzzle(robot, mount),
                        muzzle_direction(robot, mount));
    }
    if (weapon == nullptr || !fire.fire[slot] || !allowed || robot.cooldown[slot] > 0.0F ||
        magazine.empty()) {
      continue;
    }
    if (weapon->kind != WeaponKind::melee && muzzle_blocked(robot, mount)) {
      robot.firing[slot] = false;
      continue;
    }
    if (weapon->kind == WeaponKind::melee && robot.mover.striking()) {
      continue; // 0x09c337: not while a strike is under way
    }
    robot.cooldown[slot] = weapon->cooldown;
    const int rounds = spend_rounds(*weapon, magazine, seconds);
    if (weapon->hud_kind == hud_kind::missile || weapon->hud_kind == hud_kind::grenade) {
      chatter(robot, chatter_event::weapon_fired); // 0x09b5db
    }
    fired(index, slot, *weapon);
    if (beam) {
      armory_.hold_beam(*weapon, index, static_cast<int>(slot), robot.team, muzzle(robot, mount),
                        muzzle_direction(robot, mount));
    } else if (weapon->kind == WeaponKind::melee) {
      const auto start = strike_start(robot, *weapon, slot, robots_);
      if (robot.mover.strike(start)) {
        robot.strike_slot = static_cast<int>(slot);
        robot.source_free_arm_action |= start.source_free_arm_action;
      }
    } else if (fires_one_at_a_time(*weapon) && rounds > 1) {
      armory_.fire(*weapon, index, robot.team, muzzle(robot, mount), muzzle_direction(robot, mount),
                   robot.locks[slot], bodies, 1, static_cast<int>(slot));
      robot.queued_rounds[slot] = rounds - 1;
      robot.queue_left[slot] = missile_round_seconds;
    } else {
      armory_.fire(*weapon, index, robot.team, muzzle(robot, mount), muzzle_direction(robot, mount),
                   robot.locks[slot], bodies, rounds, static_cast<int>(slot));
    }
    if (robot.pose) {
      robot.pose->advance_muzzle(slot);
    }
  }
  if (cheats_.infinite_ammo && robot.piloted) {
    for (Magazine &magazine : robot.magazines) {
      magazine.ammo = magazine.max;
      magazine.shown = static_cast<float>(magazine.max);
      magazine.refill_left = 0.0F;
      magazine.dry_seconds = 0.0F;
    }
  }
  // 0x09c337, 0x09c9c2: the armed blade, where the weapon's node is, tests
  // the robots each tick until it connects or the strike disarms it.
  if (robot.strike_slot >= 0) {
    const auto slot = static_cast<std::size_t>(robot.strike_slot);
    const WeaponTuning *weapon = weapon_on(robot, slot);
    robot.mover.follow_lunge(lunge_target(robot, slot, robots_));
    if (weapon == nullptr || !robot.alive || !robot.mover.blade_armed() ||
        armory_.strike(*weapon, index, robot.team, muzzle(robot, static_cast<WeaponSlot>(slot)),
                       bodies, hits)) {
      robot.strike_slot = -1;
    }
  }
  apply_hits(hits);
}

} // namespace phantomcrash
