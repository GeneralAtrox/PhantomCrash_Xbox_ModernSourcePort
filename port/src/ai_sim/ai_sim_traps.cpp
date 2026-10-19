#include "ai_sim_traps.hpp"

#include "phantomcrash/nav_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace phantomcrash::ai_sim {
namespace {

constexpr const char *link_names[] = {"walk", "jump", "drop"};
constexpr std::size_t listed_entries = 3; // entries printed for each dead end
constexpr float test_tick = 1.0F / 60.0F; // the battle screen's logic rate
constexpr float settle_seconds = 1.0F;    // the arena fills before the test robot is put in
constexpr float escape_seconds = 30.0F;   // how long the test robot has to get out
constexpr float out_of_harm = 1e9F;       // the test robot takes no hits
constexpr float on_node = 1.0F;           // this near a node it stands on it
constexpr int probe_ways = 16;            // directions a held robot's way in is looked for in
constexpr float probe_reach = 6.0F;       // how far each look goes
constexpr float probe_height = 1.5F;      // and how high above the feet
constexpr float two_pi = 6.28318531F;

struct Links {
  std::vector<std::vector<std::uint32_t>> out, in;
};

// The grid's links a robot of `jump_rise` can take, both ways round.
Links links_of(const NavGraph &graph, const float jump_rise) {
  const std::size_t count = graph.nodes().size();
  Links links{std::vector<std::vector<std::uint32_t>>(count),
              std::vector<std::vector<std::uint32_t>>(count)};
  for (std::size_t from = 0; from < count; ++from) {
    const Vec3 at = graph.nodes()[from].position;
    for (const NavEdge &edge : graph.edges_of(static_cast<int>(from))) {
      if (edge.link == NavLink::jump && graph.nodes()[edge.to].position.y - at.y > jump_rise) {
        continue;
      }
      links.out[from].push_back(edge.to);
      links.in[edge.to].push_back(static_cast<std::uint32_t>(from));
    }
  }
  return links;
}

// Kosaraju without recursion: the nodes of the largest group in which every
// node reaches every other.
std::vector<bool> main_group(const Links &links) {
  const std::size_t count = links.out.size();
  std::vector<std::uint32_t> finished;
  std::vector<bool> seen(count);
  for (std::size_t root = 0; root < count; ++root) {
    if (seen[root]) {
      continue;
    }
    std::vector<std::pair<std::uint32_t, std::size_t>> stack{{static_cast<std::uint32_t>(root), 0}};
    seen[root] = true;
    while (!stack.empty()) {
      auto &[node, next] = stack.back();
      if (next < links.out[node].size()) {
        const std::uint32_t to = links.out[node][next++];
        if (!seen[to]) {
          seen[to] = true;
          stack.emplace_back(to, 0);
        }
        continue;
      }
      finished.push_back(node);
      stack.pop_back();
    }
  }
  std::vector<int> group(count, -1);
  std::vector<std::size_t> sizes;
  for (auto order = finished.rbegin(); order != finished.rend(); ++order) {
    if (group[*order] >= 0) {
      continue;
    }
    const int id = static_cast<int>(sizes.size());
    sizes.push_back(0);
    std::vector<std::uint32_t> stack{*order};
    group[*order] = id;
    while (!stack.empty()) {
      const std::uint32_t node = stack.back();
      stack.pop_back();
      ++sizes.back();
      for (const std::uint32_t from : links.in[node]) {
        if (group[from] < 0) {
          group[from] = id;
          stack.push_back(from);
        }
      }
    }
  }
  const int largest = static_cast<int>(std::ranges::max_element(sizes) - sizes.begin());
  std::vector<bool> main(count);
  for (std::size_t node = 0; node < count; ++node) {
    main[node] = group[node] == largest;
  }
  return main;
}

// Every node reached from `start` along `next`.
std::vector<bool> reached(const std::vector<bool> &start,
                          const std::vector<std::vector<std::uint32_t>> &next) {
  std::vector<bool> seen = start;
  std::vector<std::uint32_t> stack;
  for (std::size_t node = 0; node < start.size(); ++node) {
    if (start[node]) {
      stack.push_back(static_cast<std::uint32_t>(node));
    }
  }
  while (!stack.empty()) {
    const std::uint32_t node = stack.back();
    stack.pop_back();
    for (const std::uint32_t to : next[node]) {
      if (!seen[to]) {
        seen[to] = true;
        stack.push_back(to);
      }
    }
  }
  return seen;
}

struct Trap {
  bool dead_end{};
  std::vector<std::uint32_t> nodes;
};

// The marked nodes in groups linked either way.
std::vector<Trap> traps_of(const Links &links, const std::vector<bool> &marked,
                           const std::vector<bool> &from_main) {
  std::vector<bool> taken(marked.size());
  std::vector<Trap> traps;
  for (std::size_t root = 0; root < marked.size(); ++root) {
    if (!marked[root] || taken[root]) {
      continue;
    }
    Trap trap;
    std::vector<std::uint32_t> stack{static_cast<std::uint32_t>(root)};
    taken[root] = true;
    while (!stack.empty()) {
      const std::uint32_t node = stack.back();
      stack.pop_back();
      trap.nodes.push_back(node);
      trap.dead_end = trap.dead_end || from_main[node];
      for (const auto *next : {&links.out[node], &links.in[node]}) {
        for (const std::uint32_t to : *next) {
          if (marked[to] && !taken[to]) {
            taken[to] = true;
            stack.push_back(to);
          }
        }
      }
    }
    traps.push_back(std::move(trap));
  }
  std::ranges::sort(traps, [](const Trap &a, const Trap &b) {
    return a.dead_end != b.dead_end ? a.dead_end : a.nodes.size() > b.nodes.size();
  });
  return traps;
}

float rounded(const float value) {
  return std::round(value * 10.0F) / 10.0F;
}

void print_trap(const NavGraph &graph, const Trap &trap, const std::vector<bool> &main,
                std::ostream &out) {
  Vec3 low{1e9F, 1e9F, 1e9F}, high{-1e9F, -1e9F, -1e9F}, sum{};
  for (const std::uint32_t node : trap.nodes) {
    const Vec3 p = graph.nodes()[node].position;
    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    sum = sum + p;
  }
  const Vec3 mid = sum * (1.0F / static_cast<float>(trap.nodes.size()));
  out << (trap.dead_end ? "dead end" : "island") << ' ' << trap.nodes.size() << " nodes at ("
      << rounded(mid.x) << ", " << rounded(mid.y) << ", " << rounded(mid.z) << ") x "
      << rounded(low.x) << ".." << rounded(high.x) << " y " << rounded(low.y) << ".."
      << rounded(high.y) << " z " << rounded(low.z) << ".." << rounded(high.z) << '\n';
  if (!trap.dead_end) {
    return;
  }
  // How routes from the main grid get in.
  std::vector<bool> inside(graph.nodes().size());
  for (const std::uint32_t node : trap.nodes) {
    inside[node] = true;
  }
  std::size_t listed = 0, entries = 0;
  for (std::size_t from = 0; from < graph.nodes().size(); ++from) {
    if (!main[from]) {
      continue;
    }
    for (const NavEdge &edge : graph.edges_of(static_cast<int>(from))) {
      if (!inside[edge.to]) {
        continue;
      }
      ++entries;
      if (listed++ < listed_entries) {
        const Vec3 p = graph.nodes()[from].position;
        const Vec3 q = graph.nodes()[edge.to].position;
        out << "  " << link_names[static_cast<int>(edge.link)] << ' ' << from << " ("
            << rounded(p.x) << ", " << rounded(p.y) << ", " << rounded(p.z) << ") -> " << edge.to
            << " (" << rounded(q.x) << ", " << rounded(q.y) << ", " << rounded(q.z) << ")\n";
      }
    }
  }
  out << "  " << entries << " links in from the main grid\n";
}

// A robot put down in the trap, left to the AI: how soon it stands at a node
// of the main grid again (a trap with a way out the grid does not link).
void test_escape(SimData &data, const ArenaOptions &options, const int area, const NavGraph &graph,
                 const std::vector<bool> &main, const Trap &trap, std::ostream &out) {
  SimArena arena(data, options, area, 1);
  for (float t = 0.0F; t < settle_seconds; t += test_tick) {
    arena.tick(test_tick);
  }
  Battle &battle = arena.battle();
  int tested = -1;
  for (std::size_t index = 1; index < battle.robots().size() && tested < 0; ++index) {
    tested = battle.robots()[index].alive ? static_cast<int>(index) : -1;
  }
  if (tested < 0) {
    out << "  no robot to test with\n";
    return;
  }
  battle.place(tested, graph.nodes()[trap.nodes.front()].position, 0.0F);
  for (float t = 0.0F; t < escape_seconds; t += test_tick) {
    battle.robot(tested).invulnerable = out_of_harm;
    arena.tick(test_tick);
    const Combatant &robot = battle.robots()[static_cast<std::size_t>(tested)];
    const Vec3 at = robot.mover.position();
    const int node = graph.nearest(at);
    if (node < 0 || !main[static_cast<std::size_t>(node)]) {
      continue;
    }
    // Standing on it, not beside it beyond a wall.
    const Vec3 q = graph.nodes()[static_cast<std::size_t>(node)].position;
    if (std::hypot(q.x - at.x, q.z - at.z) < on_node && std::abs(q.y - at.y) < on_node) {
      out << "  a robot put here got out in " << rounded(t) << " s, at (" << rounded(at.x) << ", "
          << rounded(at.y) << ", " << rounded(at.z) << ")\n";
      return;
    }
  }
  const Vec3 p = battle.robots()[static_cast<std::size_t>(tested)].mover.position();
  out << "  TRAPPED: a robot put here was still in after " << escape_seconds << " s, at ("
      << rounded(p.x) << ", " << rounded(p.y) << ", " << rounded(p.z) << ")\n";
  // Which ways a robot could walk in: a body-height line out from the trap
  // that no wall stops, ending over floor.
  const Vec3 from = graph.nodes()[trap.nodes.front()].position;
  out << "  open ways in (of " << probe_ways << "):";
  for (int way = 0; way < probe_ways; ++way) {
    const float heading = two_pi * static_cast<float>(way) / static_cast<float>(probe_ways);
    const Vec3 step{std::sin(heading) * probe_reach, 0.0F, std::cos(heading) * probe_reach};
    const Vec3 waist = from + Vec3{0.0F, probe_height, 0.0F};
    const float floor =
        battle.stage().ground_height(waist + step, probe_height + ground_snap, 0.0F);
    if (!battle.stage().segment_hit(waist, waist + step) && floor != no_ground) {
      out << ' ' << way;
    }
  }
  out << '\n';
}

} // namespace

bool scan_traps(SimData &data, const ArenaOptions &options, const int area, const float jump_rise,
                std::ostream &out) {
  SimArena arena(data, options, area, 1);
  if (!arena.ready()) {
    return false;
  }
  NavTuning tuning = arena.battle().settings().ai.navigation;
  tuning.background = false;
  const NavGraph graph = NavGraph::build(arena.battle().stage(), tuning);
  const float rise = jump_rise > 0.0F ? jump_rise : NavGraph::any_rise;
  const Links links = links_of(graph, rise);
  const std::vector<bool> main = main_group(links);
  const std::vector<bool> from_main = reached(main, links.out);
  const std::vector<bool> to_main = reached(main, links.in);
  std::vector<bool> marked(main.size());
  std::size_t main_nodes = 0;
  for (std::size_t node = 0; node < main.size(); ++node) {
    main_nodes += main[node] ? 1 : 0;
    marked[node] = !to_main[node];
  }
  const std::vector<Trap> traps = traps_of(links, marked, from_main);
  out << "stage " << arena.stage_name() << ": " << graph.nodes().size() << " nodes, " << main_nodes
      << " in the main grid, " << traps.size() << " places with no way back\n";
  for (const Trap &trap : traps) {
    print_trap(graph, trap, main, out);
    test_escape(data, options, area, graph, main, trap, out);
  }
  return true;
}

} // namespace phantomcrash::ai_sim
