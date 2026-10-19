#include "phantomcrash/battle_weapons.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash {

std::optional<Vec3> Target::hit_point(const Vec3 from, const Vec3 to,
                                      const float fuse_radius) const {
  if (collision != nullptr) {
    return collision->segment_hit(from, to);
  }
  const Vec3 delta = to - from;
  const Vec3 offset = from - centre;
  const float size = radius + fuse_radius;
  const float c = dot(offset, offset) - size * size;
  if (c <= 0.0F) {
    return from;
  }
  const float a = dot(delta, delta);
  const float b = dot(offset, delta);
  const float discriminant = b * b - a * c;
  if (a <= 0.0F || discriminant < 0.0F) {
    return std::nullopt;
  }
  const float along = (-b - std::sqrt(discriminant)) / a;
  if (along < 0.0F || along > 1.0F) {
    return std::nullopt;
  }
  return from + delta * along;
}

float Target::distance_to(const Vec3 point) const {
  return collision != nullptr ? collision->distance_to(point) : length(centre - point) - radius;
}

} // namespace phantomcrash
