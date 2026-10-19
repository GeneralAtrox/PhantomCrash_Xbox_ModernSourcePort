// A piloted robot's own sounds, the rest of 0x093637 (only for robots with
// a player, robot +8 != -1): the upper body's servo (0x0949a0), Optic
// Camouflage turning on, off or broken (0x093c5d), the warning for a lost
// arm weapon and the loop while another robot locks on. Also the supply
// carrier's engine (0x066940). See docs/BATTLE_SOUNDS.md.
#include "phantomcrash/battle_sounds.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace phantomcrash {
namespace {

constexpr std::uint64_t locked_key_base = 0xB7'0000;
constexpr std::uint64_t carrier_key = 0xB8'0000;

// Whether any other robot still standing has a lock sight on `index`
// (0x051045 sets +0x8a2 for each robot its missile and grenade sights,
// +0x194, hold, locking or locked; a gun's aim is no lock).
bool locked_on(const std::vector<Combatant> &robots, const std::size_t index) {
  for (std::size_t other = 0; other < robots.size(); ++other) {
    const Combatant &robot = robots[other];
    if (other == index || !robot.alive) {
      continue;
    }
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const WeaponTuning *weapon = robot.weapons[slot];
      if (weapon != nullptr && !robot.lost[slot] && lock_sights(*weapon) &&
          robot.lock_candidates[slot] == static_cast<int>(index)) {
        return true;
      }
    }
  }
  return false;
}

int arms_knocked_off(const Combatant &robot) {
  return static_cast<int>(robot.lost[static_cast<std::size_t>(WeaponSlot::right_arm)]) +
         static_cast<int>(robot.lost[static_cast<std::size_t>(WeaponSlot::left_arm)]);
}

} // namespace

void BattleSounds::loop(const bool on, const bool was, const std::string &name,
                        const std::uint64_t key, const Vec3 at, const AudioBus bus) {
  if (name.empty()) {
    return;
  }
  if (on && !was) {
    SoundCue cue{SoundCue::Action::play, name, {}, true, key};
    cue.bus = bus;
    cues_.push_back({std::move(cue), at});
  } else if (on) {
    cues_.push_back({SoundCue{SoundCue::Action::move, {}, {}, false, key}, at});
  } else if (was) {
    cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, key}, at});
  }
}

void BattleSounds::observe_pilot(const Battle &battle, const std::size_t index,
                                 const float seconds) {
  const Combatant &robot = battle.robots()[index];
  Watch &watch = watch_[index];
  const Vec3 at = robot.aim_origin();
  const bool live = robot.piloted && robot.alive;
  const bool locked = live && locked_on(battle.robots(), index);
  loop(locked, watch.locked, table_.locked_on, locked_key_base + index, at, AudioBus::robots);
  watch.locked = locked;
  if (!live) {
    watch.twisting = false;
    watch.camouflaged = robot.camouflaged;
    watch.arm_alarm_left = 0.0F;
    watch.arms_lost = arms_knocked_off(robot);
    return;
  }
  // 0x0949a0: robot +0x2f68 is the twist stick, +0xf8 the twist.
  const RobotMover &mover = robot.mover;
  const float limit = table_.twist_limit_degrees * std::numbers::pi_v<float> / 180.0F;
  const bool pushed = std::abs(mover.twist_push()) > table_.twist_push;
  const bool inside = std::abs(mover.twist()) < limit;
  if (!watch.twisting && pushed && inside) {
    play(table_.twist_start, at);
    watch.twisting = true;
  } else if (watch.twisting && (!pushed || !inside)) {
    play(table_.twist_stop, at);
    watch.twisting = false;
  }
  // 0x093c5d: the camouflage's state as it changes.
  if (robot.camouflaged && !watch.camouflaged) {
    play(table_.camouflage_on, at);
  } else if (!robot.camouflaged && watch.camouflaged) {
    if (robot.camouflage_broken) {
      play(table_.camouflage_break, at);
      play(table_.camouflage_break, at);
    } else {
      play(table_.camouflage_off, at);
    }
  }
  watch.camouflaged = robot.camouflaged;
  // The arms' mounts (+0x8c, +0x90): a knocked-off arm weapon warns every
  // arm_lost_seconds, the first time at once; with 0, once for each arm.
  const int arms_lost = arms_knocked_off(robot);
  const bool arm_lost = arms_lost > 0;
  if (table_.arm_lost_seconds <= 0.0F) {
    if (arms_lost > watch.arms_lost) {
      play(table_.arm_lost, at);
    }
  } else if (arm_lost) {
    watch.arm_alarm_left -= seconds;
    if (watch.arm_alarm_left <= 0.0F) {
      play(table_.arm_lost, at);
      watch.arm_alarm_left = table_.arm_lost_seconds;
    }
  } else {
    watch.arm_alarm_left = 0.0F;
  }
  watch.arms_lost = arms_lost;
}

// 0x066940 starts bank 1 entry 0x2f looping at the carrier for the whole
// battle; the wreck carries it down and 0x06828e stops it as it crashes.
void BattleSounds::observe_carrier(const Battle &battle) {
  const SupplyDrop *supply = battle.supply();
  const bool sounding = supply != nullptr && supply->active() && !supply->carrier().parked;
  const Vec3 at = supply != nullptr ? supply->carrier().position : Vec3{};
  loop(sounding, carrier_sounding_, table_.carrier_engine, carrier_key, at, AudioBus::environment);
  carrier_sounding_ = sounding;
}

} // namespace phantomcrash
