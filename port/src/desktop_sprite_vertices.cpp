#include "desktop_sprite_vertices.hpp"
#include "desktop_world_renderer.hpp"
#include <algorithm>
#include <cmath>

namespace phantomcrash {
namespace {
float particle_rotation(const WorldSprite &sprite, const Vec3 right, const Vec3 up) {
  if (!sprite.source_particle_orientation) {
    return sprite.rotation;
  }
  if (sprite.fixed_plane) {
    // 0x19d194..1b0: velocity flag skips spin on the authored world plane.
    return sprite.velocity_aligned ? 0 : sprite.rotation;
  }
  if (!sprite.velocity_aligned) {
    // 0x19d120..144: -angle about source Y before the camera-plane basis.
    return -sprite.rotation;
  }
  // 0x19d054..114 projects PTK +0xf0 into the camera's XY plane. The
  // source reference (0,-1,0) is native screen-up after axis conversion.
  // This is the stored local velocity, not parent travel or an impact normal.
  const float x = dot(sprite.orientation_velocity, right);
  const float y = dot(sprite.orientation_velocity, up);
  const Vec3 projected{x, y, 0};
  const Vec3 direction = normalize(projected);
  constexpr float source_projection_guard = 0.00001F; // 0x3727c5ac, 0x19d07c
  if (dot(projected, direction) == source_projection_guard) {
    return 0; // 0x19d08c..a0 skips only the equal comparison.
  }
  const float angle = std::acos(std::clamp(direction.y, -1.0F, 1.0F));
  return x > 0 ? -angle : angle;
}
} // namespace

// The six corners of `sprite`, facing along the view's `right` and `up`.
void sprite_vertices(const WorldSprite &sprite, Vec3 right, Vec3 up, const Vec3 eye,
                     SpriteVertex *out) {
  if (sprite.fixed_plane) {
    right = sprite.plane_right;
    up = sprite.plane_up;
  } else if (length(sprite.surface_normal) > 0.0F) {
    const Vec3 normal = normalize(sprite.surface_normal);
    const Vec3 helper = std::fabs(normal.y) < 0.99F ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    right = normalize(cross(helper, normal));
    up = cross(normal, right);
  }
  const float alpha = sprite.opaque ? 1.0F : sprite.colour[3];
  const std::array<float, 4> colour{sprite.colour[0] * alpha, sprite.colour[1] * alpha,
                                    sprite.colour[2] * alpha,
                                    sprite.opaque ? -2.0F - sprite.colour[3]
                                    : sprite.additive || sprite.subtractive ? 0.0F
                                                                            : alpha};
  Vec3 a, b, c, d; // left-top, right-top, right-bottom, left-bottom
  if (sprite.streak) {
    const Vec3 along = sprite.end - sprite.position;
    Vec3 side = cross(along, eye - (sprite.position + sprite.end) * 0.5F);
    side = length(side) > 0.0F ? normalize(side) : right;
    const float end_size = sprite.end_size >= 0.0F ? sprite.end_size : sprite.size;
    a = sprite.position - side * sprite.size;
    b = sprite.position + side * sprite.size;
    c = sprite.end + side * end_size;
    d = sprite.end - side * end_size;
  } else {
    const float rotation = particle_rotation(sprite, right, up);
    const float cs = std::cos(rotation) * sprite.size;
    const float sn = std::sin(rotation) * sprite.size;
    const Vec3 x = right * cs + up * sn;
    const Vec3 y = (up * cs - right * sn) * sprite.aspect_ratio;
    a = sprite.position - x + y;
    b = sprite.position + x + y;
    c = sprite.position + x - y;
    d = sprite.position - x - y;
  }
  const auto &uv = sprite.uv;
  auto end_colour = colour;
  if (sprite.streak) {
    for (float &channel : end_colour) {
      channel *= sprite.end_alpha;
    }
  }
  const SpriteVertex quad[4] = {
      {a, uv[0], uv[1], colour},
      {b, sprite.transpose_uv ? uv[0] : uv[2], sprite.transpose_uv ? uv[3] : uv[1], colour},
      {c, uv[2], uv[3], end_colour},
      {d, sprite.transpose_uv ? uv[2] : uv[0], sprite.transpose_uv ? uv[1] : uv[3], end_colour}};
  const int order[corners_per_sprite] = {0, 1, 2, 0, 2, 3};
  for (unsigned i = 0; i < corners_per_sprite; ++i) {
    out[i] = quad[order[i]];
  }
}

} // namespace phantomcrash
