#include "phantomcrash/battle_sounds.hpp"

#include <algorithm>

namespace phantomcrash {
namespace {
// 0x511270 queries base/base+1 with mount -1: one pair per robot, not per mount.
constexpr std::uint64_t pair_key_base = 0xB8'0000'0000;
std::uint64_t pair_key(std::size_t robot, int group, int layer) {
  return pair_key_base + (robot << 16) + group * 2 + layer;
}
} // namespace

void BattleSounds::observe_fire_pairs(const Battle &battle) {
  std::vector<std::array<bool, weapon_slots>> accepted(battle.robots().size());
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::fired && event.robot >= 0 &&
        event.robot < static_cast<int>(accepted.size()) && event.slot >= 0 &&
        event.slot < weapon_slots &&
        event.weapon == battle.robots()[event.robot].weapons[event.slot]) {
      accepted[event.robot][event.slot] = true;
    }
  }
  for (std::size_t index = 0; index < battle.robots().size(); ++index) {
    const auto &robot = battle.robots()[index];
    auto &pairs = watch_[index].fire_pairs;
    std::array<const WeaponSoundRule *, weapon_slots> rules{};
    std::map<int, bool> supported;
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      const auto *weapon = robot.weapons[slot];
      const auto *sound =
          weapon ? weapon_sound(table_.fire, *weapon, static_cast<int>(slot)) : nullptr;
      if (!robot.alive || robot.lost[slot] || !sound || sound->trigger_policy != 2) {
        continue;
      }
      const auto rule =
          std::ranges::find_if(table_.fire, [&](const auto &r) { return &r.sound == sound; });
      if (rule == table_.fire.end()) {
        continue;
      }
      auto &state = pairs[rule->cycle.group];
      state.rule = &*rule;
      rules[slot] = &*rule;
      // 0x51144c reads held buttons, separately from the accepted-shot marker.
      // 0x5114c8 requires positive ammo; native unlimited magazines support it.
      const auto &ammo = robot.magazines[slot];
      supported[rule->cycle.group] |=
          robot.trigger_held[slot] && (!ammo.limited() || ammo.ammo > 0);
    }
    const Vec3 at = robot.aim_origin();
    std::map<int, std::array<bool, 2>> started;
    for (auto &[group, state] : pairs) {
      if (playing_) {
        for (int layer = 0; layer < 2; ++layer) {
          state.submitted[layer] = playing_(pair_key(index, group, layer));
        }
      }
    }
    const auto stop = [&](int group, Watch::FirePair &state) {
      if (state.submitted[0] || state.submitted[1]) {
        for (int layer = 0; layer < 2; ++layer) {
          cues_.push_back(
              {SoundCue{SoundCue::Action::stop, {}, {}, false, pair_key(index, group, layer)}, at});
        }
        if (robot.alive) {
          play(state.rule->cycle.ending.pick(robot.piloted), at, AudioBus::weapons);
        }
      }
      state.submitted = {};
    };
    // 0x511050 processes all four mounts in order. A zero-ammo shot may start
    // a pair, then a later inactive mount can end it in this same update.
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if (!rules[slot]) {
        continue;
      }
      const auto &rule = *rules[slot];
      const int group = rule.cycle.group;
      auto &state = pairs[group];
      state.rule = &rule;
      const auto start = [&](int layer) {
        const auto &sound = layer == 0 ? rule.sound : rule.cycle.secondary;
        const auto &name = sound.pick(robot.piloted);
        if (name.empty()) {
          return;
        }
        SoundCue cue{SoundCue::Action::play, name, {}, sound.loop, pair_key(index, group, layer)};
        cue.bus = AudioBus::weapons;
        cues_.push_back({std::move(cue), at});
        state.submitted[layer] = started[group][layer] = true;
      };
      if (accepted[index][slot] && robot.magazines[slot].ammo >= 0) {
        if (!state.submitted[0] && !state.submitted[1]) {
          start(0);
          if (rule.cycle.start_together) {
            start(1);
          }
        } else if (!state.submitted[1]) {
          start(1);
        }
      } else if (!supported[group]) {
        stop(group, state);
      }
    }
    for (auto &[group, state] : pairs) {
      if (!supported.contains(group)) {
        stop(group, state); // destroyed, lost or replaced mounts
      }
      for (int layer = 0; layer < 2; ++layer) {
        if (state.submitted[layer] && !started[group][layer]) {
          SoundCue cue{SoundCue::Action::move, {}, {}, false, pair_key(index, group, layer)};
          cue.bus = AudioBus::weapons;
          cues_.push_back({std::move(cue), at});
        }
      }
    }
  }
}
} // namespace phantomcrash
