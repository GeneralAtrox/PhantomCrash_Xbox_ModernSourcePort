#include "phantomcrash/source_particles.hpp"
#include "phantomcrash/skeleton.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
constexpr float source_fill_minimum = .1F;         // 0x198fd0
constexpr float frame_alignment_epsilon = .00001F; // 0x199490
enum Shape { point = 0, ellipse = 1, rectangle = 2, ellipsoid = 3, reversed_ellipsoid = 4 };
Vec3 native_axes(Vec3 source) {
  return {source.x, -source.y, -source.z};
}
float source_angle(float degrees) {
  // 0x19e5c0 followed by 0x19e570 in the parameter cache loader.
  return std::fmod(degrees * pi / 180 + pi, 2 * pi) - pi;
}
} // namespace

SourceParticleSpawn sample_source_particle_spawn(const SourceGenerator &g, std::mt19937 &engine) {
  const auto random = [&](float a, float b) {
    return std::uniform_real_distribution<float>(std::min(a, b), std::max(a, b))(engine);
  };
  // 0x198f10..1991b0: source geometry and its shared azimuth.
  Vec3 position{};
  float azimuth = 0;
  if (g.shape == ellipse) {
    azimuth = random(-pi, pi);
    const float x = (g.flags & 8) ? random(source_fill_minimum, g.dimensions.x) : g.dimensions.x;
    const float z = (g.flags & 8) ? random(source_fill_minimum, g.dimensions.z) : g.dimensions.z;
    position = {std::cos(azimuth) * x, 0, std::sin(azimuth) * z};
  } else if (g.shape == rectangle) {
    azimuth = random(-pi, pi);
    position = {random(-g.dimensions.x / 2, g.dimensions.x / 2), 0,
                random(-g.dimensions.z / 2, g.dimensions.z / 2)};
  } else if (g.shape == ellipsoid || g.shape == reversed_ellipsoid) {
    const float elevation = random(-pi, pi);
    position = {std::cos(elevation) * g.dimensions.x, std::sin(elevation) * g.dimensions.y,
                std::cos(elevation) * g.dimensions.z};
    azimuth = random(-pi, pi);
    if (g.flags & 8) {
      position.x = random(source_fill_minimum, position.x);
      position.z = random(source_fill_minimum, position.z);
    }
    position.x *= std::cos(azimuth);
    position.z *= std::sin(azimuth);
  }

  // 0x199208..199370: points have a fixed direction; planar generators
  // rotate +X by the cone and azimuth; the two sphere shapes are opposite.
  Vec3 direction{0, 1, 0}, reference{0, 0, -1};
  if (g.shape == ellipse || g.shape == rectangle) {
    const float spread = std::min(g.cone_spread * pi / 180, pi);
    const float center = source_angle(g.cone);
    const float cone = random(center - spread, center + spread);
    direction = transform_point({1, 0, 0}, rotation_z(cone) * rotation_y(azimuth));
    reference = {0, -1, 0};
  } else if (g.shape == ellipsoid || g.shape == reversed_ellipsoid) {
    direction = normalize(position * (g.shape == reversed_ellipsoid ? -1.0F : 1.0F));
    direction.y = -direction.y;
    direction.z = -direction.z;
    reference = {0, -1, 0};
  }
  // 0x10ea20 is a vector addition; 0x10ea38 is subtraction.
  Vec3 forward = position + direction;
  // The cache's second angle goes to X, then its first angle to Y.
  // 0x199388..1993e4 does not apply the cached third angle here.
  const Mat4 rotation =
      rotation_x(source_angle(g.rotation.y)) * rotation_y(source_angle(g.rotation.x));
  position = transform_point(position, rotation);
  direction = transform_point(direction, rotation);
  reference = transform_point(reference, rotation);
  forward = normalize(transform_point(forward, rotation));
  const Vec3 velocity = direction * -random(g.speed - g.speed_spread, g.speed + g.speed_spread);
  // 0x19df4c..74 preflips offset Y/Z; 0x199448..78 adds it after rotation.
  position = position + native_axes(g.offset);

  // 0x199488 tests dot(forward, velocity), not the speed or forward length.
  // The source fallback copies the emitter frame. Standalone frame is identity.
  std::array<Vec3, 2> axes{{{1, 0, 0}, {0, 0, -1}}};
  const float alignment = dot(forward, velocity);
  if (alignment < -frame_alignment_epsilon || alignment > frame_alignment_epsilon) {
    const Vec3 right = normalize(cross(reference, forward));
    axes = {native_axes(right), native_axes(forward)};
  }
  // Source vertices at 0x2b9b10 are X/Z, with V=0 at Z=-.5. The draw's
  // X rotation by pi flips Z, so its top axis is the frame's positive Z.
  return {native_axes(position), native_axes(velocity), axes};
}
} // namespace phantomcrash
