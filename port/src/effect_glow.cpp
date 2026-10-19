#include "phantomcrash/effect_glow.hpp"

#include <algorithm>
#include <map>

namespace phantomcrash {
namespace {

constexpr float byte_max = 255.0F;

struct Sum {
  int group{};
  Vec3 position;
  Vec3 colour;
  float energy{};

  void add(const Vec3 at, const Vec3 rgb, const float weight) {
    position = position + at * weight;
    colour = colour + rgb * weight;
    energy += weight;
  }
};

Vec3 between(const Vec3 a, const Vec3 b, const float t) {
  return a + (b - a) * t;
}

} // namespace

std::vector<EffectGlow> effect_glows(const EffectSystem &system, const EffectGlowTuning &tuning) {
  const EffectBank &bank = system.bank();
  const float t = system.between();
  const auto scale = [&](const int group) {
    return tuning.group_scale ? tuning.group_scale(group) : 1.0F;
  };
  std::map<int, Sum> sums; // by start()
  for (const EffectEmitter &emitter : system.emitters()) {
    if (emitter.live <= 0 || emitter.definition < 0 ||
        emitter.definition >= static_cast<int>(bank.definitions.size()) ||
        !bank.definitions[static_cast<std::size_t>(emitter.definition)].additive) {
      continue;
    }
    const float group_scale = scale(emitter.group);
    if (group_scale <= 0.0F) {
      continue;
    }
    // A cluster's parts are placed about its centre when drawn.
    const auto centre =
        std::find_if(emitter.particles.begin(), emitter.particles.end(),
                     [](const EffectParticle &p) { return p.alive && p.kind == 1; });
    Sum &sum = sums[emitter.instance];
    sum.group = emitter.group;
    for (const EffectParticle &p : emitter.particles) {
      if (!p.alive) {
        continue;
      }
      const Vec3 rgb{p.colour[0] / byte_max, p.colour[1] / byte_max, p.colour[2] / byte_max};
      const float bright = std::max({rgb.x, rgb.y, rgb.z});
      const float weight = p.colour[3] / byte_max * bright * p.sprite_half_extent() * group_scale;
      if (weight <= 0.0F) {
        continue;
      }
      const EffectParticle &placed = p.kind == 2 && centre != emitter.particles.end() ? *centre : p;
      sum.add(between(placed.previous, placed.position, t), rgb * (1.0F / bright), weight);
    }
  }
  for (const EffectFlashState &flash : system.flashes()) {
    const float weight = flash.alpha / byte_max * flash.scale * flash.grow * tuning.flash_weight *
                         scale(flash.group);
    if (weight <= 0.0F) {
      continue;
    }
    Sum &sum = sums[flash.instance];
    sum.group = flash.group;
    sum.add(between(flash.previous, flash.position, t), tuning.flash_colour, weight);
  }
  std::vector<EffectGlow> glows;
  for (const auto &[instance, sum] : sums) {
    if (sum.energy <= 0.0F) {
      continue;
    }
    EffectGlow glow;
    glow.group = sum.group;
    glow.energy = sum.energy;
    glow.position = sum.position * (1.0F / sum.energy);
    const Vec3 c = sum.colour * (1.0F / sum.energy);
    const float top = std::max({c.x, c.y, c.z, 0.0001F});
    glow.colour = c * (1.0F / top);
    glows.push_back(glow);
  }
  return glows;
}

} // namespace phantomcrash
