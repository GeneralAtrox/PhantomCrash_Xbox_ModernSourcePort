#include "phantomcrash/stage_collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace phantomcrash {
namespace {

constexpr float boundary_epsilon = 1e-4F; // same contact tolerance as stage collision

// A horizontal ray crosses a projected triangle over this x interval.
// Vertical fence triangles collapse to a line; leaning fences form a strip.
std::optional<std::array<float, 2>> crossing(const CollisionTriangle &triangle, Vec3 at) {
  float low = std::numeric_limits<float>::max();
  float high = std::numeric_limits<float>::lowest();
  for (std::size_t i = 0; i < triangle.points.size(); ++i) {
    const Vec3 a = triangle.points[i];
    const Vec3 b = triangle.points[(i + 1) % triangle.points.size()];
    if (std::abs(a.z - at.z) <= boundary_epsilon && std::abs(b.z - at.z) <= boundary_epsilon &&
        at.x >= std::min(a.x, b.x) - boundary_epsilon &&
        at.x <= std::max(a.x, b.x) + boundary_epsilon) {
      return std::array{at.x, at.x};
    }
    if ((a.z > at.z) == (b.z > at.z)) {
      continue;
    }
    const float x = a.x + (b.x - a.x) * ((at.z - a.z) / (b.z - a.z));
    low = std::min(low, x);
    high = std::max(high, x);
  }
  return low <= high ? std::optional{std::array{low, high}} : std::nullopt;
}

} // namespace

bool StageCollision::in_play_area(const Vec3 at) const {
  if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z) ||
      ground_height(at, boundary_epsilon) == no_ground) {
    return false;
  }
  // Navigation and scenery floors extend outside the playable arena. Test
  // the converted force-field footprint, even where its mesh starts above
  // the floor. This also handles concave outlines and clipped corners.
  bool has_boundary = false;
  std::vector<std::array<float, 2>> spans;
  for (const auto &triangle : shape_->walls) {
    if ((surface_flags(triangle.surface) & stage_contact::fence) == 0 ||
        dot(triangle.normal, triangle.normal) < boundary_epsilon * boundary_epsilon) {
      continue;
    }
    const auto span = crossing(triangle, at);
    if (std::abs(triangle.normal.y) > ground_normal_y) {
      // The ceiling's downward-facing plane: only where its projected
      // triangle covers the point, not an infinite height limit.
      if (span && at.x >= (*span)[0] && at.x <= (*span)[1] && triangle.normal.y < 0 &&
          dot(at - triangle.points[0], triangle.normal) < -boundary_epsilon) {
        return false;
      }
      continue;
    }
    has_boundary = true;
    if (span && (*span)[1] >= at.x - boundary_epsilon) {
      spans.push_back(*span);
    }
  }
  if (!has_boundary) {
    return true;
  }
  std::ranges::sort(spans);
  // Merge both triangles of a panel, leaning strips, and duplicate faces
  // before counting crossings. Shared vertices use the half-open edge rule
  // in crossing(), so a polygon vertex is not counted twice.
  int count = 0;
  float end = std::numeric_limits<float>::lowest();
  for (const auto &span : spans) {
    if (span[0] <= at.x + boundary_epsilon) {
      return true; // touching the boundary is still in play
    }
    if (span[0] > end + boundary_epsilon) {
      ++count;
    }
    end = std::max(end, span[1]);
  }
  return count % 2 != 0;
}

} // namespace phantomcrash
