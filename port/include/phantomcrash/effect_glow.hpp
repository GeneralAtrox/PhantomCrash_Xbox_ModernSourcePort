#pragma once
// How brightly each playing effect glows: the additive particles and the
// flashes of one start(), summed into a light's position, colour and
// strength. A port addition for the optional point lights; the original
// lights nothing with its effects.

#include "phantomcrash/effect_system.hpp"

#include <functional>
#include <vector>

namespace phantomcrash {

struct EffectGlow {
  int group{};
  Vec3 position; // the glow-weighted centre
  Vec3 colour;   // brightest channel 1
  // The sum of each part's alpha times its brightest channel times its
  // half-size in world units; a flash counts its alpha times its scale.
  float energy{};
};

struct EffectGlowTuning {
  float flash_weight{1.0F};             // a flash's energy per unit of scale
  Vec3 flash_colour{1.0F, 0.8F, 0.55F}; // flash pictures' colour (their texture's, roughly)
  // A group's multiplier (0 leaves it dark); groups not answered count 1.
  std::function<float(int group)> group_scale;
};

// One glow per playing start() with additive particles or flashes.
[[nodiscard]] std::vector<EffectGlow> effect_glows(const EffectSystem &system,
                                                   const EffectGlowTuning &tuning);

} // namespace phantomcrash
