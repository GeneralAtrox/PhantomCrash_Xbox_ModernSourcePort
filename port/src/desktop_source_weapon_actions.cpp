#include "desktop_source_weapon_actions.hpp"
#include "desktop_battle_view.hpp"
#include "phantomcrash/json_fields.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace phantomcrash {
DesktopSourceWeaponActions::DesktopSourceWeaponActions(
    const DataRoots &roots, SourceParticles &particles,
    const std::map<int, SourceParticleDefinition> &definitions, const json::JsonValue &table,
    const float hz, const float scale)
    : particles_(particles), definitions_(definitions), hz_(hz), world_scale_(scale) {
  if (const auto *units = table.find("world_units")) {
    source_basis_scale_ = static_cast<float>(json::required(*units, "part_root_scale").number());
  }
  const auto *routes = table.find("weapon_actions");
  if (!routes) {
    return;
  }
  for (const auto &route : routes->array()) {
    const auto bank = json::read_file(roots.asset(route.string_or("bank", "")));
    const bool shoulder = route.string_or("mount", "") == "shoulder";
    auto &slots = banks_[{static_cast<int>(route.number_or("model", -1)), shoulder}];
    for (int slot = shoulder ? 2 : 0; slot < (shoulder ? 4 : 2); ++slot) {
      slots[static_cast<std::size_t>(slot)] = parse_source_weapon_actions(bank, slot);
    }
  }
  SDL_Log("S.L.A.I. action banks: %zu weapon routes; source fire and part-state gates enabled",
          banks_.size());
}

bool DesktopSourceWeaponActions::alive(const std::uint64_t key) const {
  const auto effect = effects_.find(key);
  return effect != effects_.end() && !effect->second.stopped &&
         std::ranges::any_of(particles_.emitters(), [key](const auto &e) { return e.key == key; });
}

std::optional<SourceParticleParent> DesktopSourceWeaponActions::parent(const Battle &battle,
                                                                       const BattleView &view,
                                                                       const Effect &effect) const {
  const auto joint =
      view.source_weapon_frame(battle, effect.owner, effect.slot, effect.definition.parent_node);
  if (!joint) {
    return std::nullopt;
  }
  const auto &robot = battle.robots().at(static_cast<std::size_t>(effect.owner));
  const auto &definition = effect.definition;
  // The glTF and particle adapters both use (source X, -Y, -Z) in native space.
  const Vec3 offset{definition.offset.x * world_scale_, -definition.offset.y * world_scale_,
                    -definition.offset.z * world_scale_};
  SourceParticleParent result;
  result.frame = source_action_parent_frame(*joint, definition.parent_flags, offset,
                                            definition.particle == 46, source_basis_scale_);
  result.flags = definition.parent_flags;
  result.scale = definition.scale;
  const auto movement = movements_.find(effect.owner);
  result.movement = movement == movements_.end() ? Vec3{} : movement->second.displacement;
  result.vertical_gate = robot.mover.grounded() ? 1 : 0;
  result.inertia_weight = definition.inertia_weight;
  result.inertia_decay = definition.inertia_decay;
  return result;
}

void DesktopSourceWeaponActions::update_parents(const Battle &battle, const BattleView &view) {
  for (auto iterator = effects_.begin(); iterator != effects_.end();) {
    const auto key = iterator->first;
    if (!std::ranges::any_of(particles_.emitters(),
                             [key](const auto &e) { return e.key == key; })) {
      iterator = effects_.erase(iterator);
      continue;
    }
    auto &effect = iterator->second;
    ++iterator;
    if (effect.owner >= static_cast<int>(battle.robots().size())) {
      particles_.stop(key);
      effect.stopped = true;
      continue;
    }
    const auto &robot = battle.robots()[static_cast<std::size_t>(effect.owner)];
    const bool active = effect.definition.particle == 46
                            ? robot.mover.blade_armed()
                            : robot.firing[static_cast<std::size_t>(effect.slot)];
    // Native battle firing/strike state supplies the held-effect stop gate.
    // Original charge, collision-mask and special boss stop producers remain pending.
    const bool loss_effect = effect.definition.weapon_state_slot >= 0;
    if ((!robot.alive && !(loss_effect && robot.dying)) ||
        (!loss_effect &&
         (robot.lost[static_cast<std::size_t>(effect.slot)] || (effect.held && !active)))) {
      particles_.stop(key);
      effect.stopped = true;
    }
    if (const auto frame = parent(battle, view, effect)) {
      particles_.set_parent(key, *frame);
    }
  }
}

void DesktopSourceWeaponActions::retire_owner(const int owner) {
  movements_.erase(owner);
  pending_.erase(owner);
  pending_loss_.erase(owner);
  std::erase_if(players_, [owner](const auto &entry) { return entry.first.first == owner; });
  for (auto &[key, effect] : effects_) {
    if (effect.owner == owner) {
      particles_.stop(key);
      effect.stopped = true;
    }
  }
}

void DesktopSourceWeaponActions::observe(const Battle &battle, const BattleView &view,
                                         const float seconds) {
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::respawned) {
      retire_owner(event.robot);
    }
  }
  for (std::size_t owner = 0; owner < battle.robots().size(); ++owner) {
    auto &movement = movements_[static_cast<int>(owner)];
    const auto position = battle.robots()[owner].mover.position();
    movement.displacement = movement.ready && seconds > 0
                                ? (position - movement.previous) * (1 / (seconds * hz_))
                                : Vec3{};
    movement.previous = position;
    movement.ready = true;
    unsigned lost = 0;
    for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
      if (battle.robots()[owner].lost[slot]) {
        lost |= 1U << slot;
      }
    }
    // Native part loss supplies one state-2 update. The source's earlier
    // countdown and later visibility phases are separate actor producers.
    pending_loss_[static_cast<int>(owner)] |= lost & ~movement.lost;
    movement.lost = lost;
  }
  update_parents(battle, view);
  std::erase_if(players_, [&](const auto &entry) {
    const auto [owner, slot] = entry.first;
    if (owner >= static_cast<int>(battle.robots().size())) {
      return true;
    }
    const auto &robot = battle.robots()[static_cast<std::size_t>(owner)];
    return (!robot.alive && !robot.dying) || !robot.weapons[static_cast<std::size_t>(slot)];
  });
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::fired && event.slot >= 0 && event.slot < 4) {
      pending_[event.robot] |= 0x40U << event.slot;
    }
  }
  clock_ += std::max(seconds, 0.0F);
  while (clock_ >= 1 / hz_) {
    clock_ -= 1 / hz_;
    for (std::size_t owner = 0; owner < battle.robots().size(); ++owner) {
      const auto &robot = battle.robots()[owner];
      if (!robot.alive && !robot.dying) {
        continue;
      }
      for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
        const auto *weapon = robot.weapons[slot];
        if (!weapon) {
          continue;
        }
        const auto bank = banks_.find({weapon->model, slot >= 2});
        if (bank == banks_.end()) {
          continue;
        }
        auto &player = players_[{static_cast<int>(owner), static_cast<int>(slot)}];
        if (!player.actions || player.model != weapon->model) {
          player.model = weapon->model;
          player.actions = std::make_unique<SourceWeaponActionPlayer>(bank->second[slot]);
        }
        std::array<int, 4> states{};
        if (pending_loss_[static_cast<int>(owner)] & (1U << slot)) {
          states[slot] = source_weapon_explosion_state;
        }
        player.actions->tick(
            robot.alive && !robot.lost[slot] ? pending_[static_cast<int>(owner)] : 0, 0x3c0,
            [&](const SourceActionEffect &definition) -> std::uint64_t {
              const auto resource = definitions_.find(definition.particle);
              if (resource == definitions_.end() ||
                  ((robot.lost[slot] || !robot.alive) && definition.weapon_state_slot < 0) ||
                  (definition.particle == 46 && !robot.mover.blade_armed())) {
                return 0;
              }
              Effect effect{static_cast<int>(owner), static_cast<int>(slot), definition};
              effect.held = std::ranges::any_of(resource->second.generators, [](const auto &g) {
                return (g.flags & 1) && !(g.flags & 4);
              });
              const auto frame = parent(battle, view, effect);
              if (!frame) {
                ++missing_frames_;
                return 0;
              }
              const auto key = ++next_key_;
              if (!particles_.start_managed(resource->second, *frame, key, definition.particle,
                                            definition.priority, static_cast<int>(owner))) {
                return 0;
              }
              effects_.emplace(key, std::move(effect));
              ++started_;
              if (definition.weapon_state_slot >= 0) {
                ++loss_started_;
              }
              return key;
            },
            [&](const std::uint64_t key) { return alive(key); }, states);
      }
    }
    pending_.clear();
    pending_loss_.clear();
  }
  log_clock_ += seconds;
  if (log_clock_ >= 2) {
    log_clock_ = 0;
    SDL_Log("S.L.A.I. action playback: starts=%llu part_losses=%llu live=%zu missing_frames=%llu",
            static_cast<unsigned long long>(started_),
            static_cast<unsigned long long>(loss_started_), effects_.size(),
            static_cast<unsigned long long>(missing_frames_));
  }
}
} // namespace phantomcrash
