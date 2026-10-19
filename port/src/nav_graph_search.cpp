// Route search over the navigation grid, and the per-stage graph cache.
#include "phantomcrash/nav_graph.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <queue>

namespace phantomcrash {
namespace {

constexpr float above_weight = 4.0F; // nodes above the feet are unlikely to be stood on
constexpr float below_weight = 0.5F; // nodes below the feet are where a fall lands
constexpr float above_slack = 1.0F;  // feet this far under a node still stand on it

// Per-thread search state, reused between searches: a stamp marks the
// entries the current search has written.
struct SearchScratch {
  std::vector<float> cost;
  std::vector<int> parent;
  std::vector<NavLink> link;
  std::vector<std::uint32_t> stamp;
  std::uint32_t generation{};

  void begin(const std::size_t nodes) {
    if (cost.size() != nodes) {
      cost.assign(nodes, 0.0F);
      parent.assign(nodes, -1);
      link.assign(nodes, NavLink::walk);
      stamp.assign(nodes, 0);
      generation = 0;
    }
    ++generation;
  }
  [[nodiscard]] float at(const int node) const {
    const auto n = static_cast<std::size_t>(node);
    return stamp[n] == generation ? cost[n] : std::numeric_limits<float>::max();
  }
  void set(const int node, const float value, const int from, const NavLink via) {
    const auto n = static_cast<std::size_t>(node);
    stamp[n] = generation;
    cost[n] = value;
    parent[n] = from;
    link[n] = via;
  }
};

thread_local SearchScratch scratch;

using Open = std::pair<float, int>;
using OpenQueue = std::priority_queue<Open, std::vector<Open>, std::greater<>>;

float flat_distance(const Vec3 a, const Vec3 b) {
  return std::hypot(b.x - a.x, b.z - a.z);
}

float avoid_extra(const std::span<const NavAvoid> avoided, const int from, const int to,
                  const float cost) {
  for (const NavAvoid &a : avoided) {
    if ((a.from < 0 || a.from == from) && a.to == to) {
      return cost;
    }
  }
  return 0.0F;
}

} // namespace

int NavGraph::cell_of(const float x, const float z) const {
  const int i = static_cast<int>(std::floor((x - origin_x_) / cell_));
  const int j = static_cast<int>(std::floor((z - origin_z_) / cell_));
  if (i < 0 || j < 0 || i >= columns_ || j >= rows_) {
    return -1;
  }
  return j * columns_ + i;
}

std::vector<int> NavGraph::nodes_near(const float x, const float z, const float radius) const {
  std::vector<int> found;
  if (columns_ == 0) {
    return found;
  }
  const int i0 = std::max(0, static_cast<int>(std::floor((x - radius - origin_x_) / cell_)));
  const int i1 =
      std::min(columns_ - 1, static_cast<int>(std::floor((x + radius - origin_x_) / cell_)));
  const int j0 = std::max(0, static_cast<int>(std::floor((z - radius - origin_z_) / cell_)));
  const int j1 =
      std::min(rows_ - 1, static_cast<int>(std::floor((z + radius - origin_z_) / cell_)));
  for (int j = j0; j <= j1; ++j) {
    for (int i = i0; i <= i1; ++i) {
      const auto c = static_cast<std::size_t>(j * columns_ + i);
      for (std::uint32_t n = cell_first_[c]; n < cell_first_[c + 1]; ++n) {
        const Vec3 p = nodes_[n].position;
        if (std::hypot(p.x - x, p.z - z) <= radius) {
          found.push_back(static_cast<int>(n));
        }
      }
    }
  }
  return found;
}

int NavGraph::nearest(const Vec3 at) const {
  for (const float radius : {cell_ * 1.5F, cell_ * 3.0F}) {
    int best = -1;
    float best_score = std::numeric_limits<float>::max();
    for (const int n : nodes_near(at.x, at.z, radius)) {
      const Vec3 p = nodes_[static_cast<std::size_t>(n)].position;
      const float rise = p.y - at.y;
      const float score =
          flat_distance(at, p) +
          (rise > above_slack ? rise * above_weight : std::abs(rise) * below_weight);
      if (score < best_score) {
        best_score = score;
        best = n;
      }
    }
    // A node overhead (a slab the robot stands under) is a last resort: a
    // node on its own level a little farther out is looked for first.
    if (best >= 0 && (radius > cell_ * 1.5F ||
                      nodes_[static_cast<std::size_t>(best)].position.y - at.y <= above_slack)) {
      return best;
    }
  }
  return -1;
}

std::vector<NavWaypoint> NavGraph::path(const int from, const int to,
                                        const std::span<const NavAvoid> avoided,
                                        const float avoid_cost, const float jump_rise) const {
  std::vector<NavWaypoint> route;
  if (from < 0 || to < 0 || from == to) {
    return route;
  }
  scratch.begin(nodes_.size());
  const Vec3 goal = nodes_[static_cast<std::size_t>(to)].position;
  OpenQueue open;
  scratch.set(from, 0.0F, -1, NavLink::walk);
  open.push({flat_distance(nodes_[static_cast<std::size_t>(from)].position, goal), from});
  bool found = false;
  while (!open.empty()) {
    const auto [estimate, node] = open.top();
    open.pop();
    if (node == to) {
      found = true;
      break;
    }
    const float here = scratch.at(node);
    if (estimate - flat_distance(nodes_[static_cast<std::size_t>(node)].position, goal) >
        here + 1e-3F) {
      continue; // a stale entry
    }
    for (const NavEdge &e : edges_of(node)) {
      if (too_high(node, e, jump_rise)) {
        continue;
      }
      const int next = static_cast<int>(e.to);
      const float cost = here + e.cost + avoid_extra(avoided, node, next, avoid_cost);
      if (cost < scratch.at(next)) {
        scratch.set(next, cost, node, e.link);
        open.push({cost + flat_distance(nodes_[e.to].position, goal), next});
      }
    }
  }
  if (!found) {
    return route;
  }
  for (int node = to; node != from; node = scratch.parent[static_cast<std::size_t>(node)]) {
    route.push_back({nodes_[static_cast<std::size_t>(node)].position,
                     scratch.link[static_cast<std::size_t>(node)], node});
  }
  std::ranges::reverse(route);
  return route;
}

std::vector<std::pair<int, float>> NavGraph::costs_from(const int from, const float limit,
                                                        const float jump_rise) const {
  std::vector<std::pair<int, float>> reached;
  if (from < 0) {
    return reached;
  }
  scratch.begin(nodes_.size());
  OpenQueue open;
  scratch.set(from, 0.0F, -1, NavLink::walk);
  open.push({0.0F, from});
  while (!open.empty()) {
    const auto [cost, node] = open.top();
    open.pop();
    if (cost > scratch.at(node)) {
      continue;
    }
    reached.emplace_back(node, cost);
    for (const NavEdge &e : edges_of(node)) {
      if (too_high(node, e, jump_rise)) {
        continue;
      }
      const float next = cost + e.cost;
      if (next <= limit && next < scratch.at(static_cast<int>(e.to))) {
        scratch.set(static_cast<int>(e.to), next, node, e.link);
        open.push({next, static_cast<int>(e.to)});
      }
    }
  }
  return reached;
}

std::vector<bool> NavGraph::reachable_from(const int from, const float jump_rise) const {
  std::vector<bool> reached;
  if (from < 0 || from >= static_cast<int>(nodes_.size())) {
    return reached;
  }
  reached.assign(nodes_.size(), false);
  std::vector<int> open{from};
  reached[static_cast<std::size_t>(from)] = true;
  while (!open.empty()) {
    const int node = open.back();
    open.pop_back();
    for (const NavEdge &e : edges_of(node)) {
      if (!reached[e.to] && !too_high(node, e, jump_rise)) {
        reached[e.to] = true;
        open.push_back(static_cast<int>(e.to));
      }
    }
  }
  return reached;
}

namespace {

std::uint64_t mix(std::uint64_t hash, const float value) {
  hash ^= std::bit_cast<std::uint32_t>(value);
  return hash * 1099511628211ULL; // FNV-1a prime
}

std::uint64_t stage_key(const StageCollision &stage, const NavTuning &t) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto *triangles : {&stage.walls(), &stage.floor()}) {
    hash = mix(hash, static_cast<float>(triangles->size()));
    for (const auto &triangle : *triangles) {
      for (const Vec3 &p : triangle.points) {
        hash = mix(mix(mix(hash, p.x), p.y), p.z);
      }
    }
  }
  for (const float value :
       {t.cell, t.step, t.jump_rise, static_cast<float>(t.jump_cells), t.headroom, t.wall_clearance,
        t.jump_cost, t.drop_cost, t.body_radius}) {
    hash = mix(hash, value);
  }
  return hash;
}

} // namespace

NavGraphFuture shared_nav_graph(const StageCollision &stage, const NavTuning &tuning) {
  static std::mutex lock;
  static std::map<std::uint64_t, NavGraphFuture> graphs;
  const std::uint64_t key = stage_key(stage, tuning);
  const std::scoped_lock guard(lock);
  if (const auto found = graphs.find(key); found != graphs.end()) {
    return found->second;
  }
  NavGraphFuture future;
  if (tuning.background) {
    future = std::async(std::launch::async, [stage, tuning] {
               return std::make_shared<const NavGraph>(NavGraph::build(stage, tuning));
             }).share();
  } else {
    std::promise<std::shared_ptr<const NavGraph>> built;
    built.set_value(std::make_shared<const NavGraph>(NavGraph::build(stage, tuning)));
    future = built.get_future().share();
  }
  graphs.emplace(key, future);
  return future;
}

} // namespace phantomcrash
