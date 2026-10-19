#pragma once
// The battle's point lights, a port addition drawn by the optional dynamic
// lighting: burning wrecks and other glowing effects, blast fireballs,
// muzzle flashes, energy shots and held beams light what is around them
// ("point_lights" in data/world/dynamic_lighting.json).

#include "desktop_dynamic_lights.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_glow.hpp"

#include <array>
#include <utility>
#include <vector>

namespace phantomcrash {

// A light's look: colour, intensity, range in units, and for a flash how
// long it lasts.
struct PointLightLook {
  Vec3 colour{1.0F, 1.0F, 1.0F};
  float intensity{};
  float range{8.0F};
  float seconds{0.06F};
};

struct PointLightTuning {
  bool enabled{};
  int max_lights{24};       // kept per frame, those that light most near the eye
  float day_scale{1.0F};    // intensities on day stages
  int shadowed{};           // the strongest blasts and fires that cast shadows ...
  float shadow_min{};       // ... with intensity times range at least this
  float shadow_lift{};      // ... looking down from this far above
  float shadow_degrees{80}; // ... this far each side of straight down
  // Effects: intensity = intensity_per_root * sqrt(energy), range =
  // range_base + range_per_root * sqrt(energy) (effect_glow.hpp).
  float min_energy{0.5F};
  float intensity_per_root{0.6F};
  float max_intensity{6.0F};
  float range_base{4.0F};
  float range_per_root{3.0F};
  float max_range{40.0F};
  float lift{1.0F}; // above the glow's centre, so the ground it lies on is lit
  float air{0.5F};  // how strongly these lights show in the air (volumetric light)
  EffectGlowTuning glow;
  std::vector<std::pair<int, float>> group_scales;
  // Blast fireballs (Armory explosions): range is range_per_radius times
  // the blast's radius, at least `range`; both fade out over its life.
  PointLightLook fireball;
  float range_per_radius{3.0F};
  std::array<PointLightLook, 7> muzzle;     // by HUD kind (battle_weapons.hpp hud_kind)
  std::array<PointLightLook, 7> projectile; // riding each round in flight
  PointLightLook beam;                      // along a held beam, every beam_spacing units
  float beam_spacing{12.0F};
  int beam_points{4};
};

[[nodiscard]] PointLightTuning load_point_light_tuning(const DataRoots &roots);

class PointLights final {
public:
  PointLights(PointLightTuning tuning, bool night);

  [[nodiscard]] bool enabled() const noexcept {
    return tuning_.enabled;
  }
  // Starts the muzzle flashes of the rounds fired this battle tick.
  void tick(const Battle &battle, float seconds);
  // Appends the lights for a view from `eye`; spotlights already in `lights`
  // leave the rest of the shadow maps to the strongest blasts and fires.
  void add(const Battle &battle, const EffectSystem *effects, Vec3 eye,
           std::vector<WorldLight> &lights) const;

private:
  struct Flash {
    WorldLight light;
    float age{};
    float life{};
  };
  struct Candidate {
    WorldLight light;
    bool can_shadow{};
  };
  void add_effects(const EffectSystem &effects, std::vector<Candidate> &out) const;
  void add_weapons(const Battle &battle, std::vector<Candidate> &out) const;
  [[nodiscard]] WorldLight point(Vec3 at, const PointLightLook &look, float strength) const;

  PointLightTuning tuning_;
  float scale_{1.0F};
  std::vector<Flash> flashes_;
};

} // namespace phantomcrash
