// Builds the navigation grid over a stage's standable ground (nav_graph.hpp).
#include "phantomcrash/nav_graph.hpp"

#include "collision_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace phantomcrash {
namespace {

constexpr float level_merge = 0.6F;      // heights this close in one cell are one level
constexpr float headroom_start = 0.3F;   // the headroom test starts this far above the feet
constexpr float knee_height = 1.0F;      // walk and drop paths are tested at these heights
constexpr float chest_height = 2.5F;     // above the feet
constexpr float lip_clearance = 1.5F;    // a jump crosses this far above its higher end
constexpr float floor_normal_min = 0.3F; // steeper floor triangles are not stood on
constexpr float shift_share = 0.5F;      // a node nudged off a wall moves at most this many cells
constexpr float climb_cost_share = 0.5F; // a jump costs this much a unit climbed
constexpr int drop_cells = 2;            // drops land this many cells away at most
constexpr float enclosure_reach = 80.0F; // how far the enclosure test looks
constexpr int enclosed_ways = 3;         // of four level looks, this many meet a wall's back
constexpr float sweep_start = 0.5F;      // the body sweep starts this far up (clear of the ground)
constexpr float sweep_step = 0.5F;       // and rises this much a test
constexpr float sweep_pushed = head_bump_drop; // pushed down this far, the body has met an overhang
constexpr float body_step = 0.5F;              // the drop sweep moves the body this far a test
constexpr float body_turned = 0.1F;            // pushed back this far, the body has met a wall

Vec3 up(const float height) {
  return {0.0F, height, 0.0F};
}

float flat_distance(const Vec3 a, const Vec3 b) {
  return std::hypot(b.x - a.x, b.z - a.z);
}

} // namespace

bool nav_walkable(const StageCollision &stage, const Vec3 from, const Vec3 to, const float step,
                  const float half_width) {
  const Vec3 delta = to - from;
  const float distance = std::hypot(delta.x, delta.z);
  const int samples = std::max(1, static_cast<int>(std::ceil(distance)));
  for (int k = 1; k <= samples; ++k) {
    const Vec3 p = from + delta * (static_cast<float>(k) / static_cast<float>(samples));
    const float ground = stage.ground_height(p, step);
    if (ground == no_ground || ground < p.y - step) {
      return false; // a hole or a drop on the way
    }
  }
  Vec3 side{};
  if (distance > 0.0F) {
    side = Vec3{-delta.z, 0.0F, delta.x} * (half_width / distance);
  }
  for (const float height : {knee_height, chest_height}) {
    if (stage.segment_hit(from + up(height), to + up(height))) {
      return false;
    }
  }
  if (half_width > 0.0F) {
    for (const Vec3 offset : {side, side * -1.0F}) {
      if (stage.segment_hit(from + offset + up(knee_height), to + offset + up(knee_height))) {
        return false;
      }
    }
  }
  return true;
}

class NavGraphBuilder {
public:
  NavGraphBuilder(const StageCollision &stage, const NavTuning &tuning)
      : stage_(stage), tuning_(tuning) {}

  NavGraph run() {
    graph_.cell_ = tuning_.cell;
    if (!sample()) {
      return graph_;
    }
    place();
    link();
    return std::move(graph_);
  }

private:
  // Heights of standable ground at every cell centre.
  bool sample() {
    std::vector<const CollisionTriangle *> standable;
    for (const auto &t : stage_.floor()) {
      if (std::abs(t.normal.y) > floor_normal_min) {
        standable.push_back(&t);
      }
    }
    for (const auto &t : stage_.walls()) {
      if (t.normal.y > ground_normal_y && (surface_flags(t.surface) & stage_contact::fence) == 0) {
        standable.push_back(&t);
      }
    }
    if (standable.empty()) {
      return false;
    }
    float x0 = standable.front()->points[0].x, x1 = x0;
    float z0 = standable.front()->points[0].z, z1 = z0;
    for (const auto *t : standable) {
      for (const Vec3 &p : t->points) {
        x0 = std::min(x0, p.x), x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z), z1 = std::max(z1, p.z);
      }
    }
    const float cell = tuning_.cell;
    graph_.origin_x_ = x0;
    graph_.origin_z_ = z0;
    graph_.columns_ = std::max(1, static_cast<int>(std::ceil((x1 - x0) / cell)));
    graph_.rows_ = std::max(1, static_cast<int>(std::ceil((z1 - z0) / cell)));
    heights_.assign(static_cast<std::size_t>(graph_.columns_) * graph_.rows_, {});
    for (const auto *t : standable) {
      float tx0 = t->points[0].x, tx1 = tx0, tz0 = t->points[0].z, tz1 = tz0;
      for (const Vec3 &p : t->points) {
        tx0 = std::min(tx0, p.x), tx1 = std::max(tx1, p.x);
        tz0 = std::min(tz0, p.z), tz1 = std::max(tz1, p.z);
      }
      const int i0 = std::max(0, static_cast<int>(std::ceil((tx0 - x0) / cell - 0.5F)));
      const int i1 = std::min(graph_.columns_ - 1, static_cast<int>((tx1 - x0) / cell - 0.5F));
      const int j0 = std::max(0, static_cast<int>(std::ceil((tz0 - z0) / cell - 0.5F)));
      const int j1 = std::min(graph_.rows_ - 1, static_cast<int>((tz1 - z0) / cell - 0.5F));
      for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
          const float x = centre_x(i), z = centre_z(j);
          if (const auto h = collision_geometry::height_at(*t, x, z)) {
            heights_[static_cast<std::size_t>(j * graph_.columns_ + i)].push_back(*h);
          }
        }
      }
    }
    return true;
  }

  // One node per cell and level with headroom, off the walls, in play.
  void place() {
    const std::size_t cells = heights_.size();
    graph_.cell_first_.assign(cells + 1, 0);
    for (std::size_t c = 0; c < cells; ++c) {
      graph_.cell_first_[c] = static_cast<std::uint32_t>(graph_.nodes_.size());
      auto &levels = heights_[c];
      std::ranges::sort(levels, std::greater<>());
      float previous = 0.0F;
      bool first = true;
      const int i = static_cast<int>(c) % graph_.columns_;
      const int j = static_cast<int>(c) / graph_.columns_;
      for (const float h : levels) {
        if (!first && previous - h < level_merge) {
          continue;
        }
        first = false;
        previous = h;
        if (const auto p = node_at({centre_x(i), h, centre_z(j)})) {
          graph_.nodes_.push_back({*p});
          node_cell_.push_back(static_cast<int>(c));
        }
      }
    }
    graph_.cell_first_[cells] = static_cast<std::uint32_t>(graph_.nodes_.size());
  }

  std::optional<Vec3> node_at(Vec3 p) const {
    // Not in_play_area: its force-field test turns away streets the ENPT
    // routes walk (Shibuya's underpass); the fences block links instead.
    if (stage_.segment_hit(p + up(headroom_start), p + up(tuning_.headroom)) || enclosed(p)) {
      return std::nullopt;
    }
    Vec3 centre = p + up(tuning_.wall_clearance + headroom_start);
    stage_.push_sphere(centre, tuning_.wall_clearance);
    const Vec3 shift{centre.x - p.x, 0.0F, centre.z - p.z};
    if (length(shift) > tuning_.cell * shift_share) {
      return std::nullopt; // wedged between walls
    }
    p = p + shift;
    const float ground = stage_.ground_height(p + up(level_merge), level_merge * 2.0F);
    if (ground == no_ground || std::abs(ground - p.y) > tuning_.step) {
      return std::nullopt;
    }
    p.y = ground;
    return p;
  }

  // Inside a building whose floor runs on under it: looking out level, most
  // ways meet a wall from behind.
  bool enclosed(const Vec3 p) const {
    int behind = 0;
    for (const Vec3 way : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 0, -1}}) {
      const auto hit =
          stage_.segment_contact(p + up(knee_height), p + up(knee_height) + way * enclosure_reach);
      behind += hit && dot(hit->normal, way) > 0.0F;
    }
    return behind >= enclosed_ways;
  }

  void link() {
    const int count = static_cast<int>(graph_.nodes_.size());
    std::vector<std::vector<NavEdge>> out(static_cast<std::size_t>(count));
    for (int a = 0; a < count; ++a) {
      const auto [i, j] = cell_xy(a);
      int connected = 0, neighbours = 0;
      for (int dj = -1; dj <= 1; ++dj) {
        for (int di = -1; di <= 1; ++di) {
          if (di == 0 && dj == 0) {
            continue;
          }
          ++neighbours;
          bool joined = false;
          for_cell(i + di, j + dj, [&](const int b) {
            if (std::abs(pos(b).y - pos(a).y) <= tuning_.step &&
                nav_walkable(stage_, pos(a), pos(b), tuning_.step, 0.0F)) {
              const float cost = flat_distance(pos(a), pos(b)) + std::abs(pos(b).y - pos(a).y);
              out[static_cast<std::size_t>(a)].push_back(
                  {static_cast<std::uint32_t>(b), cost, NavLink::walk});
              joined = true;
            }
          });
          connected += joined;
        }
      }
      graph_.nodes_[static_cast<std::size_t>(a)].ledge = connected < neighbours;
    }
    for (int a = 0; a < count; ++a) {
      if (graph_.nodes_[static_cast<std::size_t>(a)].ledge) {
        link_air(a, out[static_cast<std::size_t>(a)]);
      }
    }
    for (int a = 0; a < count; ++a) {
      auto &node = graph_.nodes_[static_cast<std::size_t>(a)];
      node.first = static_cast<std::uint32_t>(graph_.edges_.size());
      node.count = static_cast<std::uint32_t>(out[static_cast<std::size_t>(a)].size());
      graph_.edges_.insert(graph_.edges_.end(), out[static_cast<std::size_t>(a)].begin(),
                           out[static_cast<std::size_t>(a)].end());
    }
  }

  // Jumps up to ledges and across gaps, and drops, from a ledge node.
  void link_air(const int a, std::vector<NavEdge> &edges) {
    const auto [i, j] = cell_xy(a);
    const Vec3 from = pos(a);
    const float room = rise_room(from);
    const int reach = tuning_.jump_cells;
    for (int dj = -reach; dj <= reach; ++dj) {
      for (int di = -reach; di <= reach; ++di) {
        const int cells = std::max(std::abs(di), std::abs(dj));
        if (cells == 0) {
          continue;
        }
        for_cell(i + di, j + dj, [&](const int b) {
          const Vec3 to = pos(b);
          const float rise = to.y - from.y;
          const bool walked = std::ranges::any_of(
              edges, [&](const NavEdge &e) { return e.to == static_cast<std::uint32_t>(b); });
          if (walked) {
            return;
          }
          const float distance = flat_distance(from, to);
          const bool ledge = graph_.nodes_[static_cast<std::size_t>(b)].ledge;
          if (ledge && rise > tuning_.step && rise <= tuning_.jump_rise) {
            if (rise + lip_clearance <= room && jump_clear(from, to)) {
              edges.push_back({static_cast<std::uint32_t>(b),
                               distance + rise * climb_cost_share + tuning_.jump_cost,
                               NavLink::jump});
            }
          } else if (ledge && std::abs(rise) <= tuning_.step && cells >= 2) {
            if (std::max(rise, 0.0F) + lip_clearance <= room && gap_between(from, to) &&
                jump_clear(from, to)) {
              edges.push_back(
                  {static_cast<std::uint32_t>(b), distance + tuning_.jump_cost, NavLink::jump});
            }
          } else if (rise < -tuning_.step && cells <= drop_cells && drop_clear(from, to)) {
            edges.push_back(
                {static_cast<std::uint32_t>(b), distance + tuning_.drop_cost, NavLink::drop});
          }
        });
      }
    }
  }

  // How high a robot's feet rise straight up from `feet` before its body
  // (a sphere of body_radius, as the mover pushes it) meets an overhang.
  float rise_room(const Vec3 feet) const {
    const float r = tuning_.body_radius;
    const float limit = tuning_.jump_rise + lip_clearance;
    for (float rise = sweep_start; rise <= limit; rise += sweep_step) {
      Vec3 centre = feet + up(r + rise);
      const float level = centre.y;
      stage_.push_sphere(centre, r);
      if (centre.y < level - sweep_pushed) {
        return rise;
      }
    }
    return limit;
  }

  // Straight up from the take-off, then across above the higher end.
  bool jump_clear(const Vec3 from, const Vec3 to) const {
    const float top = std::max(from.y, to.y) + lip_clearance;
    const Vec3 rise_to{from.x, top + chest_height, from.z};
    if (stage_.segment_hit(from + up(knee_height), rise_to)) {
      return false;
    }
    const Vec3 over{to.x, top, to.z};
    return !stage_.segment_hit({from.x, top, from.z}, over) &&
           !stage_.segment_hit(rise_to, over + up(chest_height));
  }

  // Off the lip, then straight down. The whole robot walks off: a beam over
  // the lip above its chest stops it too (Shibuya's pit rim at (-69.6, -48,
  // -32.5) hangs 2.5-4 units over the edge, and robots walked into it).
  bool drop_clear(const Vec3 from, const Vec3 to) const {
    const Vec3 out{to.x, from.y + knee_height, to.z};
    const float head = tuning_.headroom - headroom_start;
    return !stage_.segment_hit(from + up(knee_height), out) &&
           !stage_.segment_hit(from + up(chest_height), out + up(chest_height - knee_height)) &&
           !stage_.segment_hit(from + up(head), out + up(head - knee_height)) &&
           !stage_.segment_hit(out, to + up(headroom_start)) && body_clears(from, to);
  }

  // The body (a sphere of body_radius) walked level off the lip toward the
  // landing: a wall that turns it back keeps it on the ledge. The line tests
  // above are thin; a wall beside the landing stopped robots at Shibuya
  // (-35, 0.1, -15), 0.6 short of a drop.
  bool body_clears(const Vec3 from, const Vec3 to) const {
    const float r = tuning_.body_radius;
    const Vec3 across{to.x - from.x, 0.0F, to.z - from.z};
    const float span = std::hypot(across.x, across.z);
    for (float along = std::min(body_step, span); along <= span; along += body_step) {
      const Vec3 start = from + across * (along / span) + up(r + sweep_start);
      Vec3 centre = start;
      stage_.push_sphere(centre, r);
      const Vec3 back = centre - start;
      if (back.x * across.x + back.z * across.z < -body_turned * span) {
        return false;
      }
    }
    return true;
  }

  // No ground within a step of the line between two level ledges.
  bool gap_between(const Vec3 from, const Vec3 to) const {
    const Vec3 middle = (from + to) * 0.5F;
    const float ground = stage_.ground_height(middle, tuning_.step);
    return ground == no_ground || ground < std::min(from.y, to.y) - tuning_.step;
  }

  template <typename Visit> void for_cell(const int i, const int j, Visit &&visit) const {
    if (i < 0 || j < 0 || i >= graph_.columns_ || j >= graph_.rows_) {
      return;
    }
    const auto c = static_cast<std::size_t>(j * graph_.columns_ + i);
    for (std::uint32_t n = graph_.cell_first_[c]; n < graph_.cell_first_[c + 1]; ++n) {
      visit(static_cast<int>(n));
    }
  }

  std::pair<int, int> cell_xy(const int node) const {
    const int c = node_cell_[static_cast<std::size_t>(node)];
    return {c % graph_.columns_, c / graph_.columns_};
  }

  Vec3 pos(const int node) const {
    return graph_.nodes_[static_cast<std::size_t>(node)].position;
  }
  float centre_x(const int i) const {
    return graph_.origin_x_ + (i + 0.5F) * tuning_.cell;
  }
  float centre_z(const int j) const {
    return graph_.origin_z_ + (j + 0.5F) * tuning_.cell;
  }

  const StageCollision &stage_;
  const NavTuning &tuning_;
  NavGraph graph_;
  std::vector<std::vector<float>> heights_;
  std::vector<int> node_cell_; // each node's cell (a nudged node may sit over a neighbour)
};

NavGraph NavGraph::build(const StageCollision &stage, const NavTuning &tuning) {
  return NavGraphBuilder(stage, tuning).run();
}

} // namespace phantomcrash
