#pragma once

#include "phantomcrash/math3d.hpp"

namespace phantomcrash {

struct EffectSpriteGeometry {
  Vec3 position;
  float half_extent{};
};

// 0x0bf4cf / 0x0bfae4: cluster children store relative offsets; ordinary
// particles and cluster centres store world positions. Resolve at draw time
// so monitor cameras and alternate replay views use their own view space.
[[nodiscard]] inline EffectSpriteGeometry
effect_sprite_geometry(const int kind, const Vec3 position, const Vec3 centre,
                       const float half_extent, const Mat4 &view) {
  const Vec3 forward{view.at(0, 2), view.at(1, 2), view.at(2, 2)};
  if (kind == 1) {
    constexpr float centre_depth_bias = 1.0F;
    return {position - forward * centre_depth_bias, half_extent};
  }
  if (kind == 9) {
    constexpr float debris_depth_bias = 2.0F; // 0x0bfae4 / 0x17ccbc
    return {position - forward * debris_depth_bias, half_extent};
  }
  if (kind != 2) {
    return {position, half_extent};
  }
  constexpr float submitted_size_per_half_extent = 2.0F;
  const Vec3 right{view.at(0, 0), view.at(1, 0), view.at(2, 0)};
  const Vec3 up{view.at(0, 1), view.at(1, 1), view.at(2, 1)};
  // Keep cluster spread and particle size in world units. Camera depth must
  // only affect perspective projection, not enlarge the explosion itself.
  const Vec3 offset = position * (half_extent * submitted_size_per_half_extent);
  return {centre + right * offset.x + up * offset.y + forward * offset.z, half_extent};
}

} // namespace phantomcrash
