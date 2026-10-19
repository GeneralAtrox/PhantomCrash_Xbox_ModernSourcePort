#include "phantomcrash/battle_sounds.hpp"
#include <algorithm>

namespace phantomcrash {
namespace {
constexpr std::uint64_t beam_key_base = 0xB6'0000;
constexpr std::uint64_t impact_key_base = 0xB7'0000;
constexpr int beam_sound_handler = 5;
} // namespace

void BattleSounds::observe_events(const Battle &battle) {
  const std::vector<Combatant> &robots = battle.robots();
  for (const BattleEvent &event : battle.events()) {
    const bool known = event.robot >= 0 && event.robot < static_cast<int>(robots.size());
    const bool piloted = known && robots[static_cast<std::size_t>(event.robot)].piloted;
    if (event.kind == BattleEventKind::destroyed) {
      play(table_.shot_down.pick(piloted), event.at);
    }
    if (event.kind == BattleEventKind::wreck_destroyed) {
      play(battle.settings().robot_wrecks.break_sound, event.at, AudioBus::robots);
    }
    if (event.weapon == nullptr) {
      continue;
    }
    if (event.kind == BattleEventKind::fired) {
      const SoundPair *sound = weapon_sound(table_.fire, *event.weapon, event.slot);
      if (sound && sound->requires_free_arm_action &&
          (!known || !robots[event.robot].alive || !robots[event.robot].source_free_arm_action)) {
        continue;
      }
      if ((sound && sound->trigger_policy >= 1) ||
          ((!sound || sound->trigger_policy < 0) &&
           event.weapon->fire_sound == beam_sound_handler)) {
        continue; // owned by the held, paired or sustained observer below
      }
      if (sound == nullptr && event.weapon->fire_sound < 0) {
        sound = &table_.fire_fallback;
      }
      if (sound != nullptr) {
        play(sound->pick(piloted), event.at, AudioBus::weapons);
      }
    } else if (event.kind == BattleEventKind::impact) {
      if (const SoundPair *sound = weapon_sound(table_.impact, *event.weapon, -1, event.on_robot)) {
        std::string name = sound->pick(piloted);
        const auto rule =
            std::ranges::find_if(table_.impact, [&](const auto &r) { return &r.sound == sound; });
        if (rule != table_.impact.end() && !rule->variants.empty()) {
          std::vector<double> weights;
          for (const auto &variant : rule->variants) {
            weights.push_back(variant.weight);
          }
          std::discrete_distribution<std::size_t> choose(weights.begin(), weights.end());
          name = rule->variants[choose(variant_random_)].sound;
        }
        if (known && (sound->trigger_policy == 1 || sound->trigger_policy == 3) &&
            event.slot >= 0 && event.slot < weapon_slots) {
          held_impact(event, name);
        } else {
          play(name, event.at, AudioBus::weapons);
        }
      }
    }
  }
}

void BattleSounds::held_impact(const BattleEvent &event, const std::string &name) {
  auto &held = watch_[static_cast<std::size_t>(event.robot)].held_impacts[event.slot];
  const auto key = impact_key_base + event.robot * weapon_slots + event.slot;
  if (!held.sound.empty() && (held.sound != name || held.weapon != event.weapon)) {
    cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, key}, held.at});
    held = {};
  }
  if (name.empty()) {
    return;
  }
  SoundCue cue{
      held.sound.empty() ? SoundCue::Action::play : SoundCue::Action::move, name, {}, true, key};
  cue.bus = AudioBus::weapons;
  cues_.push_back({std::move(cue), event.at});
  held = {event.weapon, name, event.at};
}

void BattleSounds::observe_beams(const Battle &battle, const float seconds) {
  for (std::size_t index = 0; index < battle.robots().size(); ++index) {
    const Combatant &robot = battle.robots()[index];
    Watch &watch = watch_[index];
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const WeaponTuning *weapon = robot.weapons[slot];
      const SoundPair *sound =
          weapon ? weapon_sound(table_.fire, *weapon, static_cast<int>(slot)) : nullptr;
      const bool source_loop = sound && sound->trigger_policy == 1;
      const bool source_sustained = sound && sound->trigger_policy == 3;
      const bool native_beam = (!sound || sound->trigger_policy < 0) && weapon &&
                               weapon->fire_sound == beam_sound_handler;
      const bool active = robot.alive && !robot.lost[slot] && robot.firing[slot] && weapon &&
                          (source_loop || native_beam);
      const std::uint64_t key = beam_key_base + index * weapon_slots + slot;
      const Vec3 at = battle.muzzle(robot, static_cast<WeaponSlot>(slot));
      if (active) {
        watch.beam_left[slot] -= seconds;
        if (!watch.beams[slot] || (!source_loop && watch.beam_left[slot] <= 0)) {
          if (sound) {
            const auto &name = sound->pick(robot.piloted);
            if (!name.empty()) {
              SoundCue cue{SoundCue::Action::play, name, {}, source_loop, key};
              cue.bus = AudioBus::weapons;
              cues_.push_back({std::move(cue), at});
            }
          }
          watch.beam_left[slot] = table_.beam_restart_seconds;
        } else {
          cues_.push_back({SoundCue{SoundCue::Action::move, {}, {}, false, key}, at});
        }
      } else if (watch.beams[slot]) {
        cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, key}, at});
      }
      watch.beams[slot] = active;
      auto &impact = watch.held_impacts[slot];
      if (!source_sustained && !impact.sound.empty() &&
          (!robot.alive || robot.lost[slot] || !robot.firing[slot] || weapon != impact.weapon)) {
        const auto impact_key = impact_key_base + index * weapon_slots + slot;
        cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, impact_key}, impact.at});
        impact = {};
      }
    }
  }
}

} // namespace phantomcrash
