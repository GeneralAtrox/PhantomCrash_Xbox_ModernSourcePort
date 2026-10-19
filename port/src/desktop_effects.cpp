#include "desktop_effects.hpp"

#include "phantomcrash/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace phantomcrash {
namespace {

constexpr const char *bank_asset = "tables/effects.json";
constexpr float short_streak = 0.2F; // 0x3e4ccccd: streak_short's share of a frame's motion
constexpr float byte_max = 255.0F;

Vec3 between(const Vec3 from, const Vec3 to, const float t) {
  return from + (to - from) * t;
}

} // namespace

std::unique_ptr<DesktopEffects> DesktopEffects::load(WorldRenderer *world, const DataRoots &roots) {
  if (world == nullptr || !world->available()) {
    return nullptr;
  }
  const auto file = roots.find_asset(bank_asset);
  if (!file) {
    SDL_Log("%s is missing: convert the robots again for the effects", bank_asset);
    return nullptr;
  }
  try {
    auto bank = std::make_shared<const EffectBank>(parse_effect_bank(json::read_file(*file)));
    const int atlas = world->texture(bank->atlas.texture);
    if (atlas < 0) {
      SDL_Log("The effect atlas %s is missing", bank->atlas.texture.c_str());
      return nullptr;
    }
    auto result = std::make_unique<DesktopEffects>(std::move(bank), atlas);
    result->source_particles_ = DesktopSourceParticles::load(*world, roots);
    return result;
  } catch (const std::exception &error) {
    SDL_Log("%s: %s", bank_asset, error.what());
    return nullptr;
  }
}

DesktopEffects::DesktopEffects(std::shared_ptr<const EffectBank> bank, const int atlas)
    : system_(std::move(bank)), atlas_(atlas) {}

void DesktopEffects::tick(const float seconds) {
  system_.tick(seconds);
  if (source_particles_) {
    source_particles_->tick(seconds);
  }
  log_seconds_ += seconds;
  constexpr float log_interval = 2.0F;
  if (log_seconds_ < log_interval) {
    return;
  }
  log_seconds_ = 0;
  int live = 0, clusters = 0, trailing = 0, fragments = 0, resting = 0;
  float largest_radius = 0, fastest = 0;
  for (const auto &emitter : system_.emitters()) {
    for (const auto &particle : emitter.particles) {
      if (!particle.alive) {
        continue;
      }
      ++live;
      clusters += particle.kind == 1 || particle.kind == 2;
      trailing += (emitter.flags & effect_flags::inherit_velocity) != 0;
      fragments += (emitter.flags & effect_flags::fragments) != 0;
      resting += particle.resting;
      largest_radius = std::max(largest_radius, particle.sprite_half_extent());
      fastest = std::max(fastest, length(particle.velocity));
    }
  }
  const bool active = live != 0 || !system_.flashes().empty();
  if (active || logged_active_) {
    SDL_Log("VFX: %.0f Hz, emitters=%zu particles=%d clusters=%d trailing=%d fragments=%d "
            "resting=%d flashes=%zu max radius=%.3f max speed/frame=%.3f",
            system_.bank().frame_rate, system_.emitters().size(), live, clusters, trailing,
            fragments, resting, system_.flashes().size(), largest_radius, fastest);
  }
  logged_active_ = active;
}

void DesktopEffects::add_sprites(std::vector<WorldSprite> &sprites) const {
  if (source_particles_) {
    source_particles_->add_sprites(sprites);
  }
  const EffectBank &bank = system_.bank();
  const float t = system_.between();
  const auto cells = static_cast<int>(bank.atlas.cells.size());
  for (const EffectEmitter &emitter : system_.emitters()) {
    const EffectDefinition &d = bank.definitions[static_cast<std::size_t>(emitter.definition)];
    const bool streak = (emitter.flags & effect_flags::streaks) != 0;
    const float reach = (emitter.flags & effect_flags::streak_short) != 0 ? short_streak : 1.0F;
    const auto centre =
        std::find_if(emitter.particles.begin(), emitter.particles.end(),
                     [](const EffectParticle &p) { return p.alive && p.kind == 1; });
    for (const EffectParticle &particle : emitter.particles) {
      if (!particle.alive) {
        continue;
      }
      WorldSprite sprite;
      sprite.texture = atlas_;
      sprite.position = between(particle.previous, particle.position, t);
      sprite.size = particle.sprite_half_extent();
      sprite.rotation = particle.rotation;
      sprite.particle_kind = particle.kind;
      if (centre != emitter.particles.end()) {
        sprite.cluster_centre = between(centre->previous, centre->position, t);
      }
      if (streak) {
        sprite.streak = true;
        sprite.end = sprite.position + particle.velocity * reach;
      }
      if (particle.cell >= 0 && particle.cell < cells) {
        const auto &corner = bank.atlas.cells[static_cast<std::size_t>(particle.cell)];
        sprite.uv = {corner[0], corner[1], corner[0] + bank.atlas.cell_size[0],
                     corner[1] + bank.atlas.cell_size[1]};
      }
      for (std::size_t c = 0; c < 4; ++c) {
        sprite.colour[c] = static_cast<float>(particle.colour[c]) / byte_max;
      }
      sprite.additive = d.additive;
      sprites.push_back(sprite);
    }
  }
}

} // namespace phantomcrash
