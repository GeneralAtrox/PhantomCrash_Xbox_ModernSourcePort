// Original weapon requests and triggers (0x05b139 / 0x05b360).
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"
#include "target_view.hpp"
#include <algorithm>

namespace phantomcrash {
namespace {
// 0x05b360's forced-weapon flags (+0x158) by record subtype (+0x1c).
unsigned forced_flag(const WeaponTuning &weapon) {
  return weapon.hud_kind == hud_kind::beam          ? 1U
         : weapon.hud_kind == hud_kind::missile     ? 2U
         : weapon.hud_kind == hud_kind::cannon      ? 4U
         : weapon.hud_kind == hud_kind::machine_gun ? 8U
                                                    : 0U;
}

// Aim modes 2 and 3 (+0x1e) pull only on a locked target; a weapon without
// a record keeps its kind's lock.
bool pulls_on_lock(const WeaponTuning &weapon) {
  return weapon.aim_mode < 0 ? needs_lock_time(weapon)
                             : weapon.aim_mode == 2 || weapon.aim_mode == 3;
}

// A cached time (the setter's ftol to ticks) stretched by 1 + jitter x U and
// truncated again (0x05bf22, 0x05b360).
int jittered_ticks(const float seconds, const OriginalAiData &data, std::minstd_rand &random) {
  const auto ticks = static_cast<float>(static_cast<int>(seconds * data.value("tick_hz")));
  return static_cast<int>(ticks * (1.0F + data.value("fire_hold_jitter") * ai_random(random)));
}
} // namespace

void Battle::ai_setup_weapons(Combatant &drone, const bool champion) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto &setup = data.levels[static_cast<std::size_t>(s.setup_ability)];
  // 0x05bf22: each weapon record starts with a jittered hold and delay.
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    s.hold[slot] = jittered_ticks(setup.fire_interval, data, random_);
    s.delay[slot] = jittered_ticks(setup.fire_delay, data, random_);
  }
  // 0x05ed21: the showcase's opponents and the champions pull the cannon
  // and the machine gun on every chance; a ranker holds a beam 5 s past its
  // last roll, a champion 0.5 s.
  const auto forced = static_cast<unsigned>(data.value("forced_weapon_mask"));
  const float hz = data.value("tick_hz");
  if (settings_.ai.showcase) {
    s.forced_weapons |= forced;
  }
  if (std::ranges::find(data.ranker_entrants, s.entrant) != data.ranker_entrants.end()) {
    s.beam_sustain_ticks = static_cast<int>(data.value("beam_sustain_ranker_seconds") * hz);
  } else if (champion) {
    s.beam_sustain_ticks = static_cast<int>(data.value("beam_sustain_champion_seconds") * hz);
    s.forced_weapons |= forced;
  }
}

void Battle::ai_weapons(Combatant &drone, const float seconds) {
  auto &s = drone.ai;
  const auto &data = *settings_.ai.original;
  const auto &live = data.levels[static_cast<std::size_t>(s.ability)];
  const auto &setup = data.levels[static_cast<std::size_t>(s.setup_ability)];
  const Combatant *target = s.target >= 0 && s.target < static_cast<int>(robots_.size())
                                ? &robots_[static_cast<std::size_t>(s.target)]
                                : nullptr;
  const auto usable = [&](const std::size_t slot) {
    return drone.weapons[slot] != nullptr && !drone.lost[slot];
  };
  // 0x05b139, from every state's running callback while a target is held:
  // with fewer triggers held than the live ability allows, each weapon whose
  // reach (0x05d28c) holds the target is requested until it pulls.
  if (target && s.held_triggers < live.max_weapon_requests) {
    const float distance = length(target->centre(settings_.robot_height) - drone.aim_origin());
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if (!usable(slot)) {
        continue;
      }
      // 0x09d42f: a melee weapon's reach counts from the target's surface.
      const WeaponTuning &weapon = *drone.weapons[slot];
      const float reach = weapon.range + (weapon.kind == WeaponKind::melee ? target->radius : 0.0F);
      s.request[slot] = s.request[slot] || (distance <= reach && distance >= weapon.min_range);
    }
  }
  // 0x05b360 serves a target with nothing between them (0x05b992) and,
  // above ability 0, in the robot's view (the record's frustum byte).
  bool can_aim = target && target->alive && target->shield <= 0;
  if (can_aim) {
    const Vec3 centre = target->centre(settings_.robot_height);
    const std::optional<TargetView> view = sight_view(drone);
    can_aim = clear_target_sight(drone.aim_origin(), centre) &&
              (s.ability < 1 || (view && in_target_view(*view, centre)));
  }
  // 0x05cfcc's class 3: the robot's heading less the target's below cos(pi/4).
  const bool facing = target && wrap_angle(drone.mover.heading() - target->mover.heading()) <
                                    data.value("facing_fire_angle");
  if (s.beam_sustain > 0) {
    --s.beam_sustain;
  }
  const auto press = [&](const std::size_t slot) {
    s.fire[slot] = true;
    s.request[slot] = false;
    s.hold[slot] = jittered_ticks(setup.fire_interval, data, random_);
  };
  const auto release = [&](const std::size_t slot) {
    s.fire[slot] = false;
    s.hold[slot] = jittered_ticks(setup.fire_interval, data, random_);
  };
  std::array<bool, weapon_slots> kept{};
  for (std::size_t slot = 0; can_aim && slot < weapon_slots; ++slot) {
    if (!usable(slot)) {
      continue;
    }
    const WeaponTuning &weapon = *drone.weapons[slot];
    kept[slot] = true;
    const bool forced = (s.forced_weapons & forced_flag(weapon)) != 0;
    if (s.hold[slot] > 0) {
      --s.hold[slot];
    }
    // A pulled trigger stays held until its hold runs out.
    if (!forced && s.hold[slot] >= 1) {
      continue;
    }
    if (!s.request[slot]) {
      release(slot);
      continue;
    }
    // The first pull waits out the delay, counted on these ticks only.
    if (s.delay[slot]-- >= 0) {
      continue;
    }
    // 0x05d0be: halved withdrawing, half as much again facing the target.
    float probability = static_cast<float>(setup.fire_probability);
    if (s.mode == AiMode::withdraw) {
      probability *= data.value("withdraw_fire_share");
    }
    if (facing) {
      probability *= data.value("facing_fire_scale");
    }
    probability = forced ? data.value("fire_probability_scale")
                         : static_cast<float>(static_cast<int>(probability));
    if (weapon.hud_kind == hud_kind::beam) {
      s.beam_sustain = s.beam_sustain_ticks;
    }
    if (!ai_chance(random_, probability, data.value("fire_probability_scale"))) {
      release(slot);
      continue;
    }
    // Unlocked, the roll is made again next tick. 0x05b360's melee check
    // (+0x19 of the 0x046844 record) is not identified and is taken as met.
    if (pulls_on_lock(weapon) && drone.locks[slot] != s.target) {
      continue;
    }
    press(slot);
    ++s.shots;
  }
  // A sustained beam stays pulled whatever the gate; every other trigger
  // the gate did not serve is released.
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    if (usable(slot) && drone.weapons[slot]->hud_kind == hud_kind::beam && s.beam_sustain > 0) {
      kept[slot] = true;
      press(slot);
    }
    if (!kept[slot]) {
      s.fire[slot] = false;
    }
  }
  s.held_triggers = static_cast<int>(std::ranges::count(s.fire, true));
  s.camouflage_wait -= seconds;
  s.camouflage_left -= seconds;
  if (drone.camouflaged) {
    if (s.camouflage_left <= 0) {
      s.camouflage_toggle = true;
    }
  } else if (s.camouflage_wait <= 0 || s.threat) {
    if (drone.camouflage > data.value("cloak_minimum") &&
        (s.threat || (s.entrant != static_cast<int>(data.value("ranker_no_random_cloak")) &&
                      ai_chance(random_, setup.camouflage_probability,
                                data.value("fire_probability_scale"))))) {
      s.camouflage_toggle = true;
      s.camouflage_left = (drone.combat.camouflage_seconds >= 0 ? drone.combat.camouflage_seconds
                                                                : settings_.camouflage_seconds) *
                          setup.camouflage_duration * ai_random(random_);
    }
    s.camouflage_wait = setup.camouflage_interval * ai_random(random_);
  }
}

// Held triggers fire at each weapon's own rate.
FireControl Battle::ai_fire(const Combatant &drone) const {
  FireControl fire;
  if (!settings_.ai.original) {
    return fire;
  }
  for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
    fire.fire[slot] = drone.ai.fire[slot] && drone.weapons[slot] != nullptr;
  }
  return fire;
}

void Battle::fire_drones(const float seconds) {
  for (std::size_t index = 1; index < robots_.size(); ++index) {
    auto &robot = robots_[index];
    if (!robot.drone || !robot.alive || robot.frozen || robot.mover.mode() == MoveMode::scripted) {
      continue;
    }
    if (settings_.ai.original) {
      update_camouflage(robot, robot.ai.camouflage_toggle, seconds);
      robot.ai.camouflage_toggle = false;
    }
    fire_weapons(static_cast<int>(index), ai_fire(robot), seconds);
  }
}
} // namespace phantomcrash
