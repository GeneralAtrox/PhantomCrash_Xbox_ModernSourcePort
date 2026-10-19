#include "desktop_source_model_actions.hpp"
#include "desktop_battle_view.hpp"
#include "phantomcrash/json_fields.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace phantomcrash {
DesktopSourceModelActions::DesktopSourceModelActions(
    const DataRoots &roots, SourceParticles &particles,
    const std::map<int, SourceParticleDefinition> &definitions, const json::JsonValue &table,
    const float hz, const float world_scale)
    : particles_(particles), definitions_(definitions), hz_(hz), world_scale_(world_scale) {
  if (const auto *units = table.find("world_units")) {
    basis_scale_ = static_cast<float>(json::required(*units, "part_root_scale").number());
  }
  std::size_t records = 0;
  if (const auto *routes = table.find("leg_actions")) {
    for (const auto &route : routes->array()) {
      auto actions =
          parse_source_motion_actions(json::read_file(roots.asset(route.string_or("bank", ""))));
      records += actions.size();
      banks_.emplace(static_cast<int>(route.number_or("model", -1)), std::move(actions));
    }
  }
  SDL_Log("S.L.A.I. model action banks: %zu leg routes; %zu primary motion records", banks_.size(),
          records);
}

bool DesktopSourceModelActions::alive(const std::uint64_t key) const {
  const auto effect = effects_.find(key);
  return effect != effects_.end() && !effect->second.stopped &&
         std::ranges::any_of(particles_.emitters(), [key](const auto &e) { return e.key == key; });
}

std::optional<SourceParticleParent> DesktopSourceModelActions::parent(const Battle &battle,
                                                                      const BattleView &view,
                                                                      const Effect &effect) const {
  const auto &definition = effect.definition;
  if (effect.owner < 0 || effect.owner >= static_cast<int>(battle.robots().size())) {
    return std::nullopt;
  }
  const auto &robot = battle.robots()[static_cast<std::size_t>(effect.owner)];
  const Vec3 offset{definition.offset.x * world_scale_, -definition.offset.y * world_scale_,
                    -definition.offset.z * world_scale_};
  SourceParticleParent result;
  if (definition.particle == 27) {
    // 0x508760..7b8 overrides the descriptor's mask and rebases its offset
    // into the model object's world translation. 0x19c190 discards the joint frame.
    result.frame = translation(robot.mover.position() + offset);
    result.flags = 0x400;
  } else {
    const auto joint = view.source_model_frame(battle, effect.owner, definition.part_selector,
                                               definition.parent_node);
    if (!joint) {
      return std::nullopt;
    }
    result.frame = source_action_parent_frame(*joint, definition.parent_flags, offset,
                                              definition.particle == 46, basis_scale_);
    result.flags = definition.parent_flags;
  }
  result.scale = definition.scale;
  if (const auto player = players_.find(effect.owner); player != players_.end()) {
    result.movement = player->second.displacement;
  }
  result.vertical_gate = robot.mover.grounded() ? 1 : 0;
  result.inertia_weight = definition.inertia_weight;
  result.inertia_decay = definition.inertia_decay;
  return result;
}

std::uint64_t DesktopSourceModelActions::start(const Battle &battle, const BattleView &view,
                                               const int owner,
                                               const SourceActionEffect &definition) {
  const auto resource = definitions_.find(definition.particle);
  if (resource == definitions_.end()) {
    return 0;
  }
  Effect effect{owner, definition};
  const auto frame = parent(battle, view, effect);
  if (!frame) {
    ++missing_frames_;
    return 0;
  }
  const auto key = ++next_key_;
  if (!particles_.start_managed(resource->second, *frame, key, definition.particle,
                                definition.priority, owner)) {
    return 0;
  }
  effects_.emplace(key, std::move(effect));
  ++started_;
  if (definition.particle == 27) {
    ++death_started_;
  }
  return key;
}

void DesktopSourceModelActions::update_parents(const Battle &battle, const BattleView &view) {
  std::erase_if(effects_, [&](const auto &entry) {
    const auto key = entry.first;
    return !std::ranges::any_of(particles_.emitters(),
                                [key](const auto &e) { return e.key == key; });
  });
  for (auto &[key, effect] : effects_) {
    if (effect.owner < 0 || effect.owner >= static_cast<int>(battle.robots().size()) ||
        (!battle.robots()[static_cast<std::size_t>(effect.owner)].alive &&
         !battle.robots()[static_cast<std::size_t>(effect.owner)].dying)) {
      particles_.stop(key);
      effect.stopped = true;
    } else if (const auto frame = parent(battle, view, effect)) {
      particles_.set_parent(key, *frame);
    }
  }
}

void DesktopSourceModelActions::retire_owner(const int owner) {
  players_.erase(owner);
  for (auto &[key, effect] : effects_) {
    if (effect.owner == owner) {
      particles_.stop(key);
      effect.stopped = true;
    }
  }
}

void DesktopSourceModelActions::observe(const Battle &battle, const BattleView &view,
                                        const float seconds) {
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::respawned) {
      retire_owner(event.robot);
    }
  }
  for (std::size_t owner = 0; owner < battle.robots().size(); ++owner) {
    const auto &robot = battle.robots()[owner];
    if (!robot.alive && !robot.dying) {
      continue;
    }
    const auto bank = banks_.find(view.legs_model(owner));
    if (bank == banks_.end()) {
      retire_owner(static_cast<int>(owner));
      continue;
    }
    if (const auto previous = players_.find(static_cast<int>(owner));
        previous != players_.end() && previous->second.model != bank->first) {
      retire_owner(static_cast<int>(owner));
    }
    auto &player = players_[static_cast<int>(owner)];
    if (player.model != bank->first || !player.actions) {
      player = {};
      player.model = bank->first;
      player.actions = std::make_unique<SourceMotionActionPlayer>(bank->second);
    }
    const auto position = robot.mover.position();
    player.displacement =
        player.ready && seconds > 0 ? (position - player.previous) * (1 / (seconds * hz_)) : Vec3{};
    player.previous = position;
    player.ready = true;
  }
  update_parents(battle, view);
  clock_ += std::max(seconds, 0.0F);
  while (clock_ >= 1 / hz_) {
    clock_ -= 1 / hz_;
    for (auto &[owner, player] : players_) {
      if (owner < 0 || owner >= static_cast<int>(battle.robots().size()) ||
          (!battle.robots()[static_cast<std::size_t>(owner)].alive &&
           !battle.robots()[static_cast<std::size_t>(owner)].dying) ||
          !player.actions) {
        continue;
      }
      auto clock = view.source_motion_clock(static_cast<std::size_t>(owner));
      clock.speed = battle.robots()[static_cast<std::size_t>(owner)].mover.motion_speed();
      player.actions->tick(
          clock, [&](const auto &definition) { return start(battle, view, owner, definition); },
          [&](const auto key) { return alive(key); },
          [&](const int resource) { return particles_.managed_count(resource, owner); });
    }
  }
  log_clock_ += seconds;
  if (log_clock_ >= 2) {
    log_clock_ = 0;
    SDL_Log("S.L.A.I. model action playback: starts=%llu deaths=%llu live=%zu missing_frames=%llu",
            static_cast<unsigned long long>(started_),
            static_cast<unsigned long long>(death_started_), effects_.size(),
            static_cast<unsigned long long>(missing_frames_));
  }
}
} // namespace phantomcrash
