#include "collision_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace phantomcrash::collision_geometry {
namespace {

constexpr int support_search_steps = 32; // ternary steps along an edge

// The highest centre of a sphere over (x, z) resting on the segment a-b,
// within `reach` horizontally. The centre height y(t) + sqrt(r² - d(t)²) is
// concave along the segment, so a ternary search finds its peak.
std::optional<float> segment_support(const Vec3 a, const Vec3 b, const float x, const float z,
                                     const float radius, const float reach) {
  const float hx = a.x - x, hz = a.z - z;
  const float dx = b.x - a.x, dz = b.z - a.z;
  const float qa = dx * dx + dz * dz;
  const float qb = 2.0F * (hx * dx + hz * dz);
  const float qc = hx * hx + hz * hz - reach * reach;
  float first = 0.0F, last = 1.0F;
  if (qa < parallel) {
    if (qc > 0.0F) {
      return std::nullopt; // an upright edge out of reach
    }
  } else {
    const float discriminant = qb * qb - 4.0F * qa * qc;
    if (discriminant < 0.0F) {
      return std::nullopt;
    }
    const float root = std::sqrt(discriminant);
    first = std::max(0.0F, (-qb - root) / (2.0F * qa));
    last = std::min(1.0F, (-qb + root) / (2.0F * qa));
    if (first > last) {
      return std::nullopt;
    }
  }
  const auto centre = [&](const float t) {
    const float ex = hx + dx * t, ez = hz + dz * t;
    const float rise = std::max(0.0F, radius * radius - ex * ex - ez * ez);
    return a.y + (b.y - a.y) * t + std::sqrt(rise);
  };
  for (int step = 0; step < support_search_steps; ++step) {
    const float left = first + (last - first) / 3.0F;
    const float right = last - (last - first) / 3.0F;
    if (centre(left) < centre(right)) {
      first = left;
    } else {
      last = right;
    }
  }
  return centre((first + last) * 0.5F);
}

} // namespace

Closest closest_point(const Vec3 p, const std::array<Vec3, 3> &t) {
  const Vec3 a = t[0], b = t[1], c = t[2];
  const Vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0.0F && d2 <= 0.0F) {
    return {a};
  }
  const Vec3 bp = p - b;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0.0F && d4 <= d3) {
    return {b};
  }
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
    return {a + ab * (d1 / (d1 - d3))};
  }
  const Vec3 cp = p - c;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0.0F && d5 <= d6) {
    return {c};
  }
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
    return {a + ac * (d2 / (d2 - d6))};
  }
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0F && d4 - d3 >= 0.0F && d5 - d6 >= 0.0F) {
    return {b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))};
  }
  const float scale = 1.0F / (va + vb + vc);
  return {a + ab * (vb * scale) + ac * (vc * scale), true};
}

std::optional<float> height_at(const CollisionTriangle &t, const float x, const float z) {
  if (std::abs(t.normal.y) < parallel) {
    return std::nullopt;
  }
  const Vec3 a = t.points[0], b = t.points[1], c = t.points[2];
  const float det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
  if (std::abs(det) < parallel) {
    return std::nullopt;
  }
  const float u = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / det;
  const float v = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / det;
  if (u < 0.0F || v < 0.0F || u + v > 1.0F) {
    return std::nullopt;
  }
  return a.y * u + b.y * v + c.y * (1.0F - u - v);
}

std::optional<float> segment_cross(const Vec3 from, const Vec3 delta, const CollisionTriangle &t) {
  const Vec3 e1 = t.points[1] - t.points[0];
  const Vec3 e2 = t.points[2] - t.points[0];
  const Vec3 p = cross(delta, e2);
  const float det = dot(e1, p);
  if (std::abs(det) < parallel) {
    return std::nullopt;
  }
  const Vec3 s = from - t.points[0];
  const float u = dot(s, p) / det;
  const Vec3 q = cross(s, e1);
  const float v = dot(delta, q) / det;
  const float along = dot(e2, q) / det;
  if (u < 0.0F || v < 0.0F || u + v > 1.0F || along < 0.0F || along > 1.0F) {
    return std::nullopt;
  }
  return along;
}

std::optional<float> edge_support(const CollisionTriangle &t, const float x, const float z,
                                  const float radius, const float reach) {
  std::optional<float> best;
  for (std::size_t corner = 0; corner < t.points.size(); ++corner) {
    const Vec3 a = t.points[corner];
    const Vec3 b = t.points[(corner + 1) % t.points.size()];
    if (const auto centre = segment_support(a, b, x, z, radius, reach)) {
      best = std::max(best.value_or(*centre), *centre);
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return *best - radius;
}

} // namespace phantomcrash::collision_geometry
