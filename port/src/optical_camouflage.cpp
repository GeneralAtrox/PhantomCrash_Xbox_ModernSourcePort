#include "phantomcrash/optical_camouflage.hpp"

#include <algorithm>

namespace phantomcrash {

OpticalCamouflage parse_optical_camouflage(const json::JsonValue &value) {
  OpticalCamouflage look;
  const auto number = [&](const char *key, const float fallback) {
    return static_cast<float>(value.number_or(key, fallback));
  };
  look.rise = number("rise", look.rise);
  look.radius = number("radius", look.radius);
  look.edge = std::max(number("edge", look.edge), 1e-3F);
  look.reach = std::max(number("reach", look.reach), 1e-3F);
  look.push = number("push", look.push);
  look.least = number("least", look.least);
  return look;
}

OpticalSphere optical_sphere(const OpticalCamouflage &look, const Vec3 base, const float sweep) {
  OpticalSphere sphere;
  // 0x074e80: (x, y + fade + 10, z, 5) with the fade from -10 (off) to 0.
  sphere.centre = base + Vec3{0.0F, look.rise * std::clamp(sweep, 0.0F, 1.0F), 0.0F};
  sphere.radius = look.radius;
  sphere.edge = look.edge;
  sphere.reach = look.reach;
  sphere.push = look.push;
  sphere.least = look.least;
  return sphere;
}

float optical_seen(const OpticalSphere &sphere, const Vec3 point) {
  const float distance = length(point - sphere.centre);
  return std::clamp((sphere.radius - distance) / sphere.edge, 0.0F, 1.0F);
}

float optical_push(const OpticalSphere &sphere, const Vec3 point) {
  const float distance = length(point - sphere.centre);
  const float share = (sphere.radius + sphere.reach - distance) / sphere.reach;
  return sphere.push * std::clamp(share, sphere.least, 1.0F);
}

} // namespace phantomcrash
