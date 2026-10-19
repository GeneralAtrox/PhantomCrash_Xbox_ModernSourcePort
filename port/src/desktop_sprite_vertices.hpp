#pragma once

#include "phantomcrash/math3d.hpp"

namespace phantomcrash {
struct WorldSprite;
struct SpriteVertex {
  Vec3 position;
  float u, v;
  std::array<float, 4> colour;
};
static_assert(sizeof(SpriteVertex) == 36, "the sprite layout expects 36-byte vertices");
inline constexpr unsigned corners_per_sprite = 6;

// CPU geometry consumed directly by the sprite vertex buffer.
void sprite_vertices(const WorldSprite &sprite, Vec3 right, Vec3 up, Vec3 eye, SpriteVertex *out);
} // namespace phantomcrash
