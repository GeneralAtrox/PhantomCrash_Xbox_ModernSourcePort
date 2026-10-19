#include "phantomcrash/source_motion_actions.hpp"
#include "phantomcrash/json_fields.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phantomcrash {
std::vector<SourceMotionAction> parse_source_motion_actions(const json::JsonValue &bank) {
  std::vector<SourceMotionAction> actions;
  for (const auto &record : json::required(bank, "records").array()) {
    const auto &trigger = json::required(record, "trigger");
    const auto flags = static_cast<unsigned>(trigger.number_or("flags", 0));
    if ((flags != 6 && flags != 7 && flags != 38 && flags != 39) ||
        trigger.number_or("condition_selector", -1) >= 0) {
      continue;
    }
    SourceMotionAction action;
    action.flags = flags;
    action.frame = static_cast<float>(trigger.number_or("frame", 0));
    for (const auto &motion : json::required(trigger, "motion_ids").array()) {
      action.motions.push_back(static_cast<int>(motion.number()));
    }
    if (action.motions.size() > 16 || !std::isfinite(action.frame)) {
      throw std::runtime_error("Invalid source motion effect trigger");
    }
    action.effects = parse_source_action_effects(record);
    for (const auto &effect : action.effects) {
      if (effect.part_selector < 0 || effect.part_selector > 1) {
        throw std::runtime_error("Unsupported source model effect attachment");
      }
    }
    // Two authored leg descriptors use -1. The source node getter indexes
    // directly; its allocation/producer semantics are not yet established.
    std::erase_if(action.effects, [](const auto &effect) {
      return effect.parent_node < 0 && effect.particle != 27 && effect.alternate_particle != 27;
    });
    actions.push_back(std::move(action));
  }
  return actions;
}

SourceMotionActionPlayer::SourceMotionActionPlayer(std::vector<SourceMotionAction> actions)
    : actions_(std::move(actions)), states_(actions_.size()) {}

void SourceMotionActionPlayer::tick(const SourceMotionClock &clock,
                                    const SourceWeaponActionPlayer::Start &start,
                                    const SourceWeaponActionPlayer::Alive &alive,
                                    const Count &count) {
  for (std::size_t i = 0; i < actions_.size(); ++i) {
    const auto &action = actions_[i];
    auto &state = states_[i];
    if ((action.flags & 0x20) && state.last_handle && alive(state.last_handle)) {
      continue;
    }
    if (clock.action < 0 ||
        std::ranges::find(action.motions, clock.action) == action.motions.end()) {
      state.frame = -1; // source resets both saved frames to -1 on a motion miss
      continue;
    }
    const bool first = state.frame == -1;
    const float previous = first ? clock.frame : state.frame;
    state.frame = clock.frame;
    float current = clock.frame;
    if (previous > current && previous + clock.speed >= clock.duration) {
      current += clock.duration;
    }
    // Bit 1 has priority over bit 4, including records that author both.
    const auto resource = [&](const SourceActionEffect &effect) {
      return clock.variant == 0x80 || clock.variant == 0x100 ? effect.alternate_particle
                                                             : effect.particle;
    };
    const bool death =
        !action.effects.empty() && resource(action.effects.front()) == 27 && clock.bank_mode == 0;
    const bool activate = death                ? (count && count(27) > 0) || current >= action.frame
                          : (action.flags & 1) ? first
                          : (action.flags & 4) ? previous < action.frame && current >= action.frame
                                               : true;
    if (activate) {
      for (auto effect : action.effects) {
        effect.particle = resource(effect);
        if (clock.bank_mode == 1 && effect.particle != 27) {
          continue; // 0x50f018..50f030 suppresses other resources, after trigger bookkeeping.
        }
        state.last_handle = start(effect);
      }
    }
  }
}
} // namespace phantomcrash
