#include "desktop_source_particles.hpp"
#include "desktop_battle_view.hpp"
#include "phantomcrash/json_fields.hpp"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <filesystem>

namespace phantomcrash {
DesktopSourceParticles::~DesktopSourceParticles() {
  SDL_Log("S.L.A.I. model impacts: starts=%llu missing_frames=%llu", model_impact_starts_,
          missing_target_frames_);
}
std::unique_ptr<DesktopSourceParticles> DesktopSourceParticles::load(WorldRenderer &world,
                                                                     const DataRoots &roots) {
  const auto file = roots.find_asset("slai/native_particles.json");
  if (!file) {
    return nullptr;
  }
  try {
    const auto table = json::read_file(*file);
    auto result = std::make_unique<DesktopSourceParticles>(
        static_cast<float>(table.number_or("update_hz", 60)),
        static_cast<float>(table.number_or("world_scale", .1)));
    for (const auto &entry : json::required(table, "pool").array()) {
      auto definition = parse_source_particles(
          json::read_file(roots.asset(entry.string_or("file", ""))), entry.find("atlas_frames"));
      for (auto &g : definition.generators) {
        const auto image = std::filesystem::path(g.texture).stem().string();
        result->textures_[g.texture] = world.texture("slai/assets/textures/" + image + ".png");
      }
      result->definitions_.emplace(static_cast<int>(entry.number_or("id", -1)),
                                   std::move(definition));
    }
    for (const auto &entry : json::required(table, "impacts").array()) {
      result->rules_.push_back(
          {static_cast<int>(entry.number_or("model", -1)),
           static_cast<int>(entry.number_or("particle", -1)),
           entry.string_or("mount", "") == "shoulder",
           static_cast<unsigned>(entry.number_or(
               "target_flags", entry.boolean_or("target_translation", false) ? 0x400 : 0))});
      if (entry.number_or("target_control", 0) != 0) {
        throw std::runtime_error("Unsupported S.L.A.I. impact control");
      }
      const unsigned flags = result->rules_.back().target_flags;
      if (flags != 0 && flags != 0x400 && flags != 0x380c) {
        throw std::runtime_error("Unsupported S.L.A.I. impact attachment mask");
      }
    }
    result->actions_ = std::make_unique<DesktopSourceWeaponActions>(
        roots, result->system_, result->definitions_, table,
        static_cast<float>(table.number_or("update_hz", 60)),
        static_cast<float>(table.number_or("world_scale", .1)));
    result->model_actions_ = std::make_unique<DesktopSourceModelActions>(
        roots, result->system_, result->definitions_, table,
        static_cast<float>(table.number_or("update_hz", 60)),
        static_cast<float>(table.number_or("world_scale", .1)));
    if (const auto *routes = table.find("projectiles")) {
      result->projectiles_ = std::make_unique<DesktopSourceProjectiles>(world, *routes);
    }
    SDL_Log("S.L.A.I. particles: %zu resources, %zu impact mappings", result->definitions_.size(),
            result->rules_.size());
    return result;
  } catch (const std::exception &error) {
    SDL_Log("S.L.A.I. particles: %s", error.what());
    return nullptr;
  }
}

bool DesktopSourceParticles::impact(const BattleEvent &event, const Battle &battle,
                                    const BattleView *view) {
  if (!event.weapon || !event.weapon->part) {
    return false;
  }
  const bool shoulder = event.weapon->part->table == WeaponTable::shoulder;
  const auto rule = std::ranges::find_if(rules_, [&](const Rule &r) {
    return r.model == event.weapon->model && r.shoulder == shoulder;
  });
  if (rule == rules_.end()) {
    return false;
  }
  const auto definition = definitions_.find(rule->particle);
  if (definition == definitions_.end()) {
    return false;
  }
  const bool held = std::ranges::any_of(
      definition->second.generators, [](const auto &g) { return (g.flags & 1) && !(g.flags & 4); });
  constexpr std::uint64_t held_key_base = 0x51A1'0000'0000;
  const bool attached = rule->target_flags && event.on_robot && event.target >= 0 &&
                        event.target < static_cast<int>(battle.robots().size());
  const auto key = held ? held_key_base + (static_cast<std::uint64_t>(event.robot) << 17) +
                              (static_cast<std::uint64_t>(shoulder) << 16) + rule->model
                        : next_key_++;
  SourceParticleParent parent;
  parent.frame = translation(event.at);
  Vec3 offset;
  if (attached) {
    if (rule->target_flags == 0x400) {
      offset = event.at - battle.robots()[event.target].mover.position();
      parent.flags = rule->target_flags;
    } else {
      const auto frame = view ? view->source_impact_frame(battle, event.target) : std::nullopt;
      if (!frame) {
        ++missing_target_frames_;
        SDL_Log("S.L.A.I. impact target frame unavailable: target=%d resource=%d", event.target,
                rule->particle);
        return true;
      }
      const auto anchor = source_impact_attachment(event.at, *frame, rule->target_flags);
      offset = anchor.local_hit;
      parent = source_impact_parent(anchor, *frame);
    }
  }
  if (!system_.start_managed(definition->second, parent, key, rule->particle)) {
    return true; // The source rejects the effect; do not substitute a PC effect.
  }
  if (attached) {
    attachments_[key] = {event.target, offset, rule->target_flags};
    if (rule->target_flags == 0x380c) {
      ++model_impact_starts_;
    }
  } else {
    attachments_.erase(key);
  }
  if (held) {
    held_[key] = {event.robot, event.weapon};
  }
  return true;
}
void DesktopSourceParticles::observe(const Battle &battle, const BattleView *view) {
  std::erase_if(attachments_, [&](const auto &entry) {
    const auto &[key, attachment] = entry;
    if (!system_.contains(key) || attachment.target < 0 ||
        attachment.target >= static_cast<int>(battle.robots().size())) {
      return true;
    }
    const auto &target = battle.robots()[attachment.target];
    if (!target.alive) {
      return true;
    }
    if (attachment.flags == 0x400) {
      // 0x19c0b8..198 keeps the world offset and follows translation only.
      system_.move(key, target.mover.position() + attachment.offset);
    } else if (const auto frame =
                   view ? view->source_impact_frame(battle, attachment.target) : std::nullopt) {
      system_.set_parent(key, source_impact_parent({attachment.flags, attachment.offset}, *frame));
    }
    return false;
  });
  std::erase_if(held_, [&](const auto &entry) {
    const auto &[key, held] = entry;
    bool firing = false;
    if (held.owner >= 0 && held.owner < static_cast<int>(battle.robots().size())) {
      const auto &robot = battle.robots()[static_cast<std::size_t>(held.owner)];
      for (std::size_t slot = 0; slot < weapon_slots; ++slot) {
        firing = firing || (robot.alive && !robot.lost[slot] && robot.firing[slot] &&
                            robot.weapons[slot] == held.weapon);
      }
    }
    if (!firing) {
      system_.stop(key);
    }
    return !firing;
  });
}
void DesktopSourceParticles::add_sprites(std::vector<WorldSprite> &sprites) const {
  for (const auto &e : system_.emitters()) {
    const auto &g = *e.generator;
    for (const auto &p : e.particles) {
      WorldSprite s;
      s.texture = textures_.at(g.texture);
      s.position = p.previous + (p.position - p.previous) * system_.between();
      const float t = p.age / p.lifetime;
      const float size = source_particle_curve(g.size_curve, g.size_start, g.size_end, t);
      s.size = std::abs(g.render_size.x * size * p.scale) * .5F;
      s.aspect_ratio = g.render_size.x != 0 ? std::abs(g.render_size.y / g.render_size.x) : 1;
      s.rotation = p.rotation;
      s.fixed_plane = !(g.flags & 0x10000);
      s.source_particle_orientation = true;
      s.velocity_aligned = (g.flags & 0x100) != 0;
      s.orientation_velocity = p.velocity * (1 / system_.world_scale());
      s.plane_right = p.quad_axes[0];
      s.plane_up = p.quad_axes[1];
      s.colour = source_particle_colour(g, p);
      s.uv = source_particle_uv(g, p);
      s.additive = g.blend == 2;
      s.opaque = g.blend == 0;
      s.subtractive = g.blend == 3;
      s.depth_test = (g.flags & 0x10) != 0;
      sprites.push_back(s);
    }
  }
}
} // namespace phantomcrash
