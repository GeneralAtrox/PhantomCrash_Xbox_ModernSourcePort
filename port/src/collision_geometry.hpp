#pragma once

#include "phantomcrash/stage_collision.hpp"

#include <array>
#include <optional>

// Point and segment queries on single collision triangles, shared by the
// stage collision's grid searches.
namespace phantomcrash::collision_geometry {

inline constexpr float parallel = 1e-7F;

struct Closest {
  Vec3 point;
  bool face{}; // inside the triangle, not on an edge or corner
};

// The point of the triangle nearest `p` (Ericson, Real-Time Collision
// Detection 5.1.5).
[[nodiscard]] Closest closest_point(Vec3 p, const std::array<Vec3, 3> &t);

// The triangle's height at (x, z), if the point lies over it.
[[nodiscard]] std::optional<float> height_at(const CollisionTriangle &t, float x, float z);

// Möller-Trumbore: the segment parameter where it crosses the triangle.
[[nodiscard]] std::optional<float> segment_cross(Vec3 from, Vec3 delta, const CollisionTriangle &t);

// The foot height at which a sphere of `radius` over (x, z) rests on the
// triangle's edges or corners, counting only contacts no more than `reach`
// away horizontally. The foot is the sphere's bottom, `radius` below its
// centre.
[[nodiscard]] std::optional<float> edge_support(const CollisionTriangle &t, float x, float z,
                                                float radius, float reach);

} // namespace phantomcrash::collision_geometry
