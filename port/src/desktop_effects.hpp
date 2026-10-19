#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/effect_system.hpp"
#include "desktop_source_particles.hpp"

#include <memory>
#include <vector>

namespace phantomcrash {

// The particle effects (effect_system.hpp) drawn as the world's sprites
// over the effect atlas, as 0x0625e4 hands them to 0x0bf4cf: a particle's
// full width is its scale times its size, halved; a streak runs to where its
// motion takes it in a frame (a fifth of that for streak_short).
class DesktopEffects final {
public:
  // Null when the bank or its atlas is missing (logged).
  [[nodiscard]] static std::unique_ptr<DesktopEffects> load(WorldRenderer *world,
                                                            const DataRoots &roots);

  DesktopEffects(std::shared_ptr<const EffectBank> bank, int atlas);

  [[nodiscard]] EffectSystem &system() noexcept {
    return system_;
  }
  void tick(float seconds);
  // Appends every live particle, between its last two frames.
  void add_sprites(std::vector<WorldSprite> &sprites) const;
  [[nodiscard]] DesktopSourceParticles *source_particles() {
    return source_particles_.get();
  }

private:
  EffectSystem system_;
  int atlas_;
  float log_seconds_{};
  bool logged_active_{};
  std::unique_ptr<DesktopSourceParticles> source_particles_;
};

} // namespace phantomcrash
