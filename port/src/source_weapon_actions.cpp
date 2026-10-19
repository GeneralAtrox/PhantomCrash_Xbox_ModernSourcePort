#include "phantomcrash/source_weapon_actions.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace phantomcrash {
std::vector<SourceWeaponAction> parse_source_weapon_actions(const json::JsonValue &bank,
                                                            const int slot) {
  if (slot < 0 || slot >= 4) {
    throw std::runtime_error("Invalid source action owner slot");
  }
  std::vector<SourceWeaponAction> result;
  for (const auto &row : json::required(bank, "records").array()) {
    const auto &trigger = json::required(row, "trigger");
    const auto flags = static_cast<unsigned>(trigger.number_or("flags", 0));
    if (!(flags & (0x40 | 0x100))) {
      continue;
    }
    if (flags != 0x40 && flags != 0x60 && flags != 0x100) {
      throw std::runtime_error("Unsupported source weapon action flags");
    }
    SourceWeaponAction action;
    action.flags = flags;
    if (flags & 0x100) {
      // 0x50cf58..50d03c replaces +0x3f with the owner's slot, even
      // when the unbound asset authors a different selector.
      action.weapon_state_slot = slot;
    }
    for (const auto &gate : json::required(trigger, "weapon_gates").array()) {
      unsigned mask = static_cast<unsigned>(gate.number_or("mask", 0));
      if (mask == 0xff) {
        mask = 0x40U << slot; // 0x50cff8..50d008 substitutes only the owner sentinel
      }
      action.gates.push_back({mask, static_cast<int>(gate.number_or("delay", 0))});
    }
    if (action.gates.size() > 4) {
      throw std::runtime_error("Invalid source firing gate extent");
    }
    action.effects = parse_source_action_effects(row);
    // Negative authored nodes still reach an unchecked source control-array
    // lookup (0x19bf5c..64 / 0x1b3c80). Twelve loss descriptors need their
    // original frame producer resolved. Disabled descriptors retain their
    // zero return, which is significant for last-handle bookkeeping.
    std::erase_if(action.effects, [](const auto &effect) {
      return effect.parent_node < 0 && (effect.particle >= 0 || effect.alternate_particle >= 0);
    });
    for (auto &effect : action.effects) {
      effect.weapon_state_slot = action.weapon_state_slot;
    }
    result.push_back(std::move(action));
  }
  return result;
}

std::vector<SourceActionEffect> parse_source_action_effects(const json::JsonValue &row) {
  std::vector<SourceActionEffect> effects;
  for (const auto &value : json::required(row, "effects").array()) {
    SourceActionEffect effect;
    effect.part_selector = static_cast<int>(value.number_or("part_selector", 0));
    effect.particle = static_cast<int>(value.number_or("particle", -1));
    effect.alternate_particle = static_cast<int>(value.number_or("alternate_particle", -1));
    effect.parent_node = static_cast<int>(value.number_or("parent_node", -1));
    effect.parent_flags = static_cast<unsigned>(value.number_or("parent_flags", 0));
    effect.scale = static_cast<float>(value.number_or("scale", 1));
    effect.inertia_weight = static_cast<float>(value.number_or("inertia_weight", 0));
    effect.inertia_decay = static_cast<float>(value.number_or("inertia_decay", 0));
    effect.front_show = static_cast<float>(value.number_or("front_show", 0));
    effect.priority = {static_cast<int>(value.number_or("recycle_priority", 0)),
                       static_cast<int>(value.number_or("recycle_secondary", 0))};
    const auto &offset = json::required(value, "position_offset").array();
    effect.offset = {static_cast<float>(offset.at(0).number()),
                     static_cast<float>(offset.at(1).number()),
                     static_cast<float>(offset.at(2).number())};
    effects.push_back(effect);
  }
  if (effects.size() > 8) {
    throw std::runtime_error("Invalid source firing effect extent");
  }
  return effects;
}

Mat4 source_action_parent_frame(const Mat4 &joint, const unsigned flags, Vec3 offset,
                                const bool blade, const float source_basis_scale) {
  Mat4 parent;
  if (flags & 0x4000) {
    parent = joint; // 0x19c040..50 bypasses the mask with the complete model frame
    // Imported robot vertices/PMT translations bake the source root scale.
    // Reconstruct its basis for PTK coordinates; world translation is already converted.
    for (int row = 0; row < 3; ++row) {
      for (int column = 0; column < 3; ++column) {
        parent.at(row, column) *= source_basis_scale;
      }
    }
  } else {
    if (flags & (8 | 2 | 0x80)) {
      Mat4 basis = joint;
      for (int row = 0; row < 3; ++row) {
        const float length =
            std::sqrt(basis.at(row, 0) * basis.at(row, 0) + basis.at(row, 1) * basis.at(row, 1) +
                      basis.at(row, 2) * basis.at(row, 2));
        if (!(length > 0)) {
          throw std::runtime_error("Degenerate source action parent basis");
        }
        for (int column = 0; column < 3; ++column) {
          basis.at(row, column) /= length;
        }
      }
      // 0x19bac0 extracts Z/X/Y and recomposes in that order. Native Y/Z
      // reflection reverses the source Z/Y angles, already reflected here.
      const float x = std::asin(std::clamp(-basis.at(2, 1), -1.0F, 1.0F));
      const float c = std::cos(x);
      const float y = c == 0 ? std::atan2(-basis.at(0, 2), basis.at(0, 0))
                             : std::atan2(basis.at(2, 0), basis.at(2, 2));
      const float z = c == 0 ? 0 : std::asin(std::clamp(basis.at(0, 1) / c, -1.0F, 1.0F));
      parent = rotation_z((flags & 0x2000) ? z : 0) * rotation_x((flags & 0x800) ? x : 0) *
               rotation_y((flags & 0x1000) ? y : 0);
    }
    if (flags & 0x10) {
      for (int i = 0; i < 4; ++i) {
        parent.at(i, i) = joint.at(i, i);
      }
    }
    if (flags & (4 | 1 | 0x40 | 0x200)) {
      for (int i = 0; i < 3; ++i) {
        parent.at(3, i) = joint.at(3, i);
      }
    }
  }
  if ((flags & 1) && !(flags & (0x100 | 4 | 0x200))) {
    offset = {}; // 0x19c058..88 resets the local frame before composition
  }
  const Mat4 local = (blade ? rotation_z(-std::numbers::pi_v<float> / 2) : Mat4{}) *
                     translation(offset); // resource 46 constructor's source Z quarter-turn
  return local * parent;
}

SourceWeaponActionPlayer::SourceWeaponActionPlayer(std::vector<SourceWeaponAction> actions)
    : actions_(std::move(actions)), states_(actions_.size()) {}

void SourceWeaponActionPlayer::tick(const unsigned requests, const unsigned permitted,
                                    const Start &start, const Alive &alive,
                                    const std::array<int, 4> &weapon_states) {
  for (std::size_t i = 0; i < actions_.size(); ++i) {
    const auto &action = actions_[i];
    auto &state = states_[i];
    if ((action.flags & 0x20) && state.last_handle && alive(state.last_handle)) {
      continue; // 0x50e958..968 skips reset/decrement while the last effect exists
    }
    bool activate = false;
    if (action.flags & 0x100) {
      const int slot = action.weapon_state_slot;
      activate = slot >= 0 && slot < 4 &&
                 weapon_states[static_cast<std::size_t>(slot)] == source_weapon_explosion_state;
    } else {
      for (std::size_t gate = 0; gate < action.gates.size(); ++gate) {
        const auto &definition = action.gates[gate];
        if ((requests & definition.mask) && (permitted & definition.mask)) {
          state.timers[gate] = definition.delay;
        }
        if (state.timers[gate] > 0 && --state.timers[gate] == 0) {
          activate = true;
        }
      }
    }
    if (activate) {
      for (const auto &effect : action.effects) {
        state.last_handle = start(effect); // 0x50f10c remembers the last descriptor's return
      }
    }
  }
}
} // namespace phantomcrash
