#include "phantomcrash/stage_collision.hpp"

#include "collision_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace phantomcrash {
namespace {

using namespace collision_geometry;

// The contact flags per surface % 1000 (0x2057d8).
constexpr std::array<unsigned, 11> surface_table{0x1,   0x40,   0x80,   0x100,  0x200, 0x400,
                                                 0x800, 0x1000, 0x2000, 0x4000, 0x8000};
constexpr int surface_modulus = 1000;
constexpr float grid_cell = 8.0F;        // port grid, world units
constexpr float bounds_margin = 0.01F;   // keeps rounding at the bounding box's faces
constexpr int push_passes = 3;           // corners need a second push
constexpr int box_wall_surface = 9;      // a plain wall
constexpr float contact_epsilon = 1e-4F; // retain tangency after floating-point push-out
constexpr float reject_slack = 0.01F;    // keeps bounding-box rejects conservative under rounding

// Squared distance from a point to a triangle's bounding box, across x and z
// or all three axes. A triangle farther than a query reaches cannot touch it.
float flat_gap(const float x, const float z, const Vec3 low, const Vec3 high) {
  const float dx = std::max({low.x - x, 0.0F, x - high.x});
  const float dz = std::max({low.z - z, 0.0F, z - high.z});
  return dx * dx + dz * dz;
}

float box_gap(const Vec3 p, const Vec3 low, const Vec3 high) {
  const float dy = std::max({low.y - p.y, 0.0F, p.y - high.y});
  return flat_gap(p.x, p.z, low, high) + dy * dy;
}

CollisionTriangle triangle(const Vec3 a, const Vec3 b, const Vec3 c, const int surface) {
  return {{a, b, c}, normalize(cross(b - a, c - a)), surface};
}

// Two triangles facing `inward` for the quad a b c d (in order round it).
void quad(std::vector<CollisionTriangle> &out, const Vec3 a, const Vec3 b, const Vec3 c,
          const Vec3 d, const Vec3 inward) {
  CollisionTriangle first = triangle(a, b, c, box_wall_surface);
  CollisionTriangle second = triangle(a, c, d, box_wall_surface);
  if (dot(first.normal, inward) < 0.0F) {
    first = triangle(a, c, b, box_wall_surface);
    second = triangle(a, d, c, box_wall_surface);
  }
  out.push_back(first);
  out.push_back(second);
}

Vec3 point(const json::JsonValue &value) {
  const auto &xyz = value.array();
  if (xyz.size() != 3) {
    throw std::runtime_error("collision point needs [x, y, z]");
  }
  return {static_cast<float>(xyz[0].number()), static_cast<float>(xyz[1].number()),
          static_cast<float>(xyz[2].number())};
}

std::vector<CollisionTriangle> triangles(const json::JsonValue &value, const char *key) {
  std::vector<CollisionTriangle> out;
  const json::JsonValue *list = value.find(key);
  if (list == nullptr) {
    return out;
  }
  for (const json::JsonValue &entry : list->array()) {
    const json::JsonValue *corners = entry.find("points");
    if (corners == nullptr || corners->array().size() != 3) {
      throw std::runtime_error("collision triangle needs three points");
    }
    const auto &points = corners->array();
    out.push_back(triangle(point(points[0]), point(points[1]), point(points[2]),
                           static_cast<int>(entry.number_or("surface", box_wall_surface))));
  }
  return out;
}

} // namespace

unsigned surface_flags(const int surface) {
  const auto index = static_cast<std::size_t>(std::abs(surface) % surface_modulus);
  return index < surface_table.size() ? surface_table[index] : 0U;
}

StageCollision::StageCollision() {
  static const auto empty = std::make_shared<const Shape>();
  shape_ = empty;
}

StageCollision::StageCollision(std::vector<CollisionTriangle> walls,
                               std::vector<CollisionTriangle> floor) {
  auto shape = std::make_shared<Shape>();
  shape->walls = std::move(walls);
  shape->floor = std::move(floor);
  float x0 = 0.0F, z0 = 0.0F, x1 = 0.0F, z1 = 0.0F;
  bool first = true;
  for (const auto *set : {&shape->walls, &shape->floor}) {
    for (const CollisionTriangle &t : *set) {
      for (const Vec3 &p : t.points) {
        x0 = first ? p.x : std::min(x0, p.x);
        z0 = first ? p.z : std::min(z0, p.z);
        x1 = first ? p.x : std::max(x1, p.x);
        z1 = first ? p.z : std::max(z1, p.z);
        shape->low.y = first ? p.y : std::min(shape->low.y, p.y);
        shape->high.y = first ? p.y : std::max(shape->high.y, p.y);
        first = false;
      }
    }
  }
  if (!first) {
    shape->low = {x0 - bounds_margin, shape->low.y - bounds_margin, z0 - bounds_margin};
    shape->high = {x1 + bounds_margin, shape->high.y + bounds_margin, z1 + bounds_margin};
  }
  shape->origin_x = x0;
  shape->origin_z = z0;
  shape->columns = std::max(1, static_cast<int>(std::ceil((x1 - x0) / grid_cell)) + 1);
  shape->rows = std::max(1, static_cast<int>(std::ceil((z1 - z0) / grid_cell)) + 1);
  shape->grid.assign(static_cast<std::size_t>(shape->columns * shape->rows), {});
  const auto add = [&](const std::vector<CollisionTriangle> &set, std::vector<Bounds> &bounds,
                       const bool floor) {
    bounds.reserve(set.size());
    for (std::uint32_t index = 0; index < set.size(); ++index) {
      const auto &p = set[index].points;
      const Bounds &box = bounds.emplace_back(
          Vec3{std::min({p[0].x, p[1].x, p[2].x}), std::min({p[0].y, p[1].y, p[2].y}),
               std::min({p[0].z, p[1].z, p[2].z})},
          Vec3{std::max({p[0].x, p[1].x, p[2].x}), std::max({p[0].y, p[1].y, p[2].y}),
               std::max({p[0].z, p[1].z, p[2].z})});
      const CellRange range = cell_range(*shape, box.low.x, box.low.z, box.high.x, box.high.z);
      for (int c = range.first_column; c <= range.last_column; ++c) {
        for (int r = range.first_row; r <= range.last_row; ++r) {
          Cell &cell = shape->grid[static_cast<std::size_t>(c * shape->rows + r)];
          (floor ? cell.floor : cell.walls).push_back(index);
        }
      }
    }
  };
  add(shape->walls, shape->wall_bounds, false);
  add(shape->floor, shape->floor_bounds, true);
  shape_ = std::move(shape);
}

StageCollision StageCollision::box(const ArenaBounds &arena, const float wall_height) {
  std::vector<CollisionTriangle> walls;
  const float h = arena.half_size;
  const float top = wall_height;
  // The outer walls face the middle.
  const std::array<Vec3, 4> corners{Vec3{-h, 0, -h}, Vec3{h, 0, -h}, Vec3{h, 0, h}, Vec3{-h, 0, h}};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const Vec3 a = corners[i];
    const Vec3 b = corners[(i + 1) % corners.size()];
    quad(walls, a, b, b + Vec3{0, top, 0}, a + Vec3{0, top, 0}, (a + b) * -1.0F);
  }
  // The blocks face out.
  for (const auto &block : arena.blocks) {
    const std::array<Vec3, 4> box{Vec3{block[0], 0, block[1]}, Vec3{block[2], 0, block[1]},
                                  Vec3{block[2], 0, block[3]}, Vec3{block[0], 0, block[3]}};
    const Vec3 middle{(block[0] + block[2]) * 0.5F, 0.0F, (block[1] + block[3]) * 0.5F};
    for (std::size_t i = 0; i < box.size(); ++i) {
      const Vec3 a = box[i];
      const Vec3 b = box[(i + 1) % box.size()];
      quad(walls, a, b, b + Vec3{0, top, 0}, a + Vec3{0, top, 0}, (a + b) * 0.5F - middle);
    }
  }
  std::vector<CollisionTriangle> floor;
  quad(floor, corners[0], corners[1], corners[2], corners[3], {0.0F, 1.0F, 0.0F});
  return {std::move(walls), std::move(floor)};
}

StageCollision::CellRange StageCollision::cell_range(const Shape &shape, const float x0,
                                                     const float z0, const float x1,
                                                     const float z1) {
  const auto column = [&](const float x) {
    return std::clamp(static_cast<int>(std::floor((x - shape.origin_x) / grid_cell)), 0,
                      shape.columns - 1);
  };
  const auto row = [&](const float z) {
    return std::clamp(static_cast<int>(std::floor((z - shape.origin_z) / grid_cell)), 0,
                      shape.rows - 1);
  };
  return {column(x0), column(x1), row(z0), row(z1)};
}

std::span<const std::uint32_t> StageCollision::candidates(const float x0, const float z0,
                                                          const float x1, const float z1,
                                                          const bool floor) const {
  const Shape &shape = *shape_;
  if (shape.grid.empty()) {
    return {};
  }
  const auto list = [&](const int c, const int r) -> const std::vector<std::uint32_t> & {
    const Cell &cell = shape.grid[static_cast<std::size_t>(c * shape.rows + r)];
    return floor ? cell.floor : cell.walls;
  };
  const CellRange range = cell_range(shape, x0, z0, x1, z1);
  if (range.first_column == range.last_column && range.first_row == range.last_row) {
    return list(range.first_column, range.first_row); // already ascending and unique
  }
  // Collision runs every robot every tick (and again on each online rollback):
  // reuse one buffer rather than allocating per query.
  // Gathered whole, then sorted once: merging cell by cell grows with the
  // square of a long sight line's cells.
  thread_local std::vector<std::uint32_t> found;
  found.clear();
  for (int c = range.first_column; c <= range.last_column; ++c) {
    for (int r = range.first_row; r <= range.last_row; ++r) {
      const auto &cell = list(c, r);
      found.insert(found.end(), cell.begin(), cell.end());
    }
  }
  std::ranges::sort(found);
  found.erase(std::ranges::unique(found).begin(), found.end());
  return found;
}

float StageCollision::ground_height(const Vec3 at, const float step, const float radius,
                                    int *surface) const {
  float best = no_ground;
  int best_surface = -1;
  const auto offer = [&](const float support, const CollisionTriangle &t) {
    if (support <= at.y + step && support > best) {
      best = support;
      best_surface = t.surface;
    }
  };
  // push_sphere stands a sphere on an edge or corner while the push points
  // more upward than ground_normal_y: within this distance across.
  const float reach = radius * std::sqrt(1.0F - ground_normal_y * ground_normal_y);
  const float within = reach + reject_slack;
  const auto consider = [&](const CollisionTriangle &t, const Bounds &box) {
    if (flat_gap(at.x, at.z, box.low, box.high) > within * within) {
      return; // neither under the centre nor an edge within reach
    }
    if ((surface_flags(t.surface) & stage_contact::fence) != 0) {
      return;
    }
    if (t.normal.y > ground_normal_y) {
      if (const auto h = height_at(t, at.x, at.z)) {
        // A sphere's centre must be radius / normal.y above a sloped plane.
        // Comparing its feet with the unadjusted plane falsely reports a fall
        // after collision has already lifted it onto the ramp.
        offer(*h + radius * (1.0F / t.normal.y - 1.0F), t);
      }
    }
    // The foot sphere can also rest on a ledge's lip, a kerb or a low rim
    // past its centre. Ignoring that support made the robot "walk off" the
    // edge it stood on every tick, held in its jump.
    if (reach > 0.0F) {
      if (const auto lip = edge_support(t, at.x, at.z, radius, reach)) {
        offer(*lip, t);
      }
    }
  };
  for (const std::uint32_t index :
       candidates(at.x - reach, at.z - reach, at.x + reach, at.z + reach, true)) {
    consider(shape_->floor[index], shape_->floor_bounds[index]);
  }
  for (const std::uint32_t index :
       candidates(at.x - reach, at.z - reach, at.x + reach, at.z + reach, false)) {
    consider(shape_->walls[index], shape_->wall_bounds[index]);
  }
  if (surface != nullptr) {
    *surface = best_surface;
  }
  return best;
}

float StageCollision::floor_under(const float x, const float z) const {
  for (const std::uint32_t index : candidates(x, z, x, z, true)) {
    if (const auto height = height_at(shape_->floor[index], x, z)) {
      return *height;
    }
  }
  return no_ground;
}

unsigned StageCollision::push_sphere(Vec3 &centre, const float radius,
                                     std::optional<StageHit> *fence_hit) const {
  unsigned flags = 0;
  const auto near =
      candidates(centre.x - radius, centre.z - radius, centre.x + radius, centre.z + radius, false);
  const float within = radius + contact_epsilon + reject_slack;
  for (int pass = 0; pass < push_passes; ++pass) {
    bool pushed = false;
    for (const std::uint32_t index : near) {
      const Bounds &box = shape_->wall_bounds[index];
      if (box_gap(centre, box.low, box.high) > within * within) {
        continue; // too far to touch
      }
      const CollisionTriangle &t = shape_->walls[index];
      const unsigned material = surface_flags(t.surface);
      const bool fence = (material & stage_contact::fence) != 0;
      const Closest nearest = closest_point(centre, t.points);
      const Vec3 away = centre - nearest.point;
      const float distance = length(away);
      if (nearest.face && dot(away, t.normal) < 0.0F) {
        continue; // behind a face
      }
      if (fence_hit != nullptr && !*fence_hit && distance <= radius + contact_epsilon && fence) {
        *fence_hit = StageHit{nearest.point, t.normal, t.surface};
      }
      if (distance >= radius) {
        continue;
      }
      const Vec3 direction = distance > parallel ? away * (1.0F / distance) : t.normal;
      if (fence) {
        // Force fields are barriers, not ledges. A radial edge push could lift
        // the foot sphere and repeatedly mark the robot as having landed.
        centre = centre + t.normal * (radius - dot(away, t.normal));
      } else {
        centre = nearest.point + direction * radius;
      }
      flags |=
          stage_contact::touched | material |
          (!fence && direction.y > ground_normal_y ? stage_contact::ground : stage_contact::wall);
      pushed = true;
    }
    if (!pushed) {
      break;
    }
  }
  return flags;
}

std::optional<Vec3> StageCollision::segment_hit(const Vec3 from, const Vec3 to) const {
  const auto hit = segment_contact(from, to);
  return hit ? std::optional<Vec3>{hit->position} : std::nullopt;
}

std::optional<StageHit> StageCollision::segment_contact(const Vec3 from, const Vec3 to) const {
  const Vec3 delta = to - from;
  std::optional<float> nearest;
  const CollisionTriangle *triangle = nullptr;
  const float x0 = std::min(from.x, to.x), x1 = std::max(from.x, to.x);
  const float z0 = std::min(from.z, to.z), z1 = std::max(from.z, to.z);
  const float y0 = std::min(from.y, to.y), y1 = std::max(from.y, to.y);
  const auto test = [&](const CollisionTriangle &t, const Bounds &box) {
    // A crossing lies inside both the segment's and the triangle's boxes.
    if (x1 < box.low.x - reject_slack || x0 > box.high.x + reject_slack ||
        y1 < box.low.y - reject_slack || y0 > box.high.y + reject_slack ||
        z1 < box.low.z - reject_slack || z0 > box.high.z + reject_slack) {
      return;
    }
    if (const auto along = segment_cross(from, delta, t);
        along && (!nearest || *along < *nearest)) {
      nearest = along;
      triangle = &t;
    }
  };
  // Shots test every placed object and wreck: most pass nowhere near it.
  const Vec3 low = shape_->low, high = shape_->high;
  if (x1 < low.x || x0 > high.x || z1 < low.z || z0 > high.z || y1 < low.y || y0 > high.y) {
    return std::nullopt;
  }
  for (const std::uint32_t index : candidates(x0, z0, x1, z1, false)) {
    test(shape_->walls[index], shape_->wall_bounds[index]);
  }
  for (const std::uint32_t index : candidates(x0, z0, x1, z1, true)) {
    test(shape_->floor[index], shape_->floor_bounds[index]);
  }
  if (!nearest) {
    return std::nullopt;
  }
  return StageHit{from + delta * *nearest, triangle->normal, triangle->surface};
}

StageCollision parse_stage_collision(const json::JsonValue &value) {
  return {triangles(value, "walls"), triangles(value, "floor")};
}

float StageCollision::distance_to(const Vec3 point) const {
  float nearest = std::numeric_limits<float>::max();
  for (const auto *triangles : {&shape_->walls, &shape_->floor}) {
    for (const auto &triangle : *triangles) {
      nearest = std::min(nearest, length(closest_point(point, triangle.points).point - point));
    }
  }
  return nearest;
}

} // namespace phantomcrash
