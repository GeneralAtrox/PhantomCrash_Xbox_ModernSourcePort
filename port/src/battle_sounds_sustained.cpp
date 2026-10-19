#include "phantomcrash/battle_sounds.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {
constexpr std::uint64_t sustained_key_base = 0xB9'0000;
constexpr std::uint64_t impact_key_base = 0xB7'0000;
} // namespace

void BattleSounds::observe_sustained(const Battle &battle) {
  for (std::size_t index = 0; index < battle.robots().size(); ++index) {
    const auto &robot = battle.robots()[index];
    auto &watch = watch_[index];
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const auto *weapon = robot.weapons[slot];
      const auto *sound =
          weapon ? weapon_sound(table_.fire, *weapon, static_cast<int>(slot)) : nullptr;
      auto &state = watch.sustained[slot];
      const auto key = sustained_key_base + index * weapon_slots + slot;
      const Vec3 at = battle.muzzle(robot, static_cast<WeaponSlot>(slot));
      const auto stop = [&] {
        if (state.submitted) {
          cues_.push_back({SoundCue{SoundCue::Action::stop, {}, {}, false, key}, at});
        }
        state = {};
      };
      if (state.weapon != weapon) {
        stop(); // replacement retires the old cue before starting its successor
      }
      if (playing_ && state.submitted) {
        state.submitted = playing_(key);
      }
      const bool valid = robot.alive && !robot.lost[slot] && sound && sound->trigger_policy == 3;
      // 0x5111fc queries this mount before an accepted shot starts 209/228/247.
      const bool accepted = valid && robot.magazines[slot].ammo >= 0 &&
                            std::ranges::any_of(battle.events(), [&](const auto &event) {
                              return event.kind == BattleEventKind::fired &&
                                     event.robot == static_cast<int>(index) &&
                                     event.slot == static_cast<int>(slot) && event.weapon == weapon;
                            });
      // 0x506338..3a4 stops the beam and its mount's fire/impact cues when
      // the authored fade counter reaches zero. The native beam retains that
      // counter through release; raw held input cannot stand in for its lifetime.
      const bool fading =
          valid && std::ranges::any_of(battle.armory().beams(), [&](const auto &beam) {
            return beam.owner == static_cast<int>(index) && beam.slot == static_cast<int>(slot) &&
                   beam.weapon == weapon && beam.held > 0;
          });
      if (!accepted && !fading) {
        stop();
      } else if (accepted && !state.submitted && !sound->pick(robot.piloted).empty()) {
        SoundCue cue{SoundCue::Action::play, sound->pick(robot.piloted), {}, sound->loop, key};
        cue.bus = AudioBus::weapons;
        cues_.push_back({std::move(cue), at});
        state = {weapon, true};
      } else if (state.submitted) {
        SoundCue cue{SoundCue::Action::move, {}, {}, false, key};
        cue.bus = AudioBus::weapons;
        cues_.push_back({std::move(cue), at});
      }
      auto &impact = watch.held_impacts[slot];
      if (sound && sound->trigger_policy == 3 && !impact.sound.empty() &&
          ((!accepted && !fading) || impact.weapon != weapon)) {
        cues_.push_back({SoundCue{SoundCue::Action::stop,
                                  {},
                                  {},
                                  false,
                                  impact_key_base + index * weapon_slots + slot},
                         impact.at});
        impact = {};
      }
    }
  }
}
} // namespace phantomcrash
