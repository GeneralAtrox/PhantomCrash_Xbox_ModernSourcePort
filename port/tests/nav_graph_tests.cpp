// Port navigation for computer robots: the grid over a stage's ground, its
// routes, and robots following them up onto a roof (nav_graph.hpp).

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/nav_graph.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;

// A floor from -30 to 30 and a block 6 high over x 4..14, z -5..5: its roof
// is out of a step's reach but within a held jump's.
StageCollision roof_stage() {
  return parse_stage_collision(json::parse(R"({
    "walls": [
      {"points": [[4,6,-5],[14,6,5],[14,6,-5]], "surface": 9},
      {"points": [[4,6,-5],[4,6,5],[14,6,5]], "surface": 9},
      {"points": [[4,0,-5],[4,0,5],[4,6,5]], "surface": 9},
      {"points": [[4,0,-5],[4,6,5],[4,6,-5]], "surface": 9},
      {"points": [[14,0,-5],[14,6,5],[14,0,5]], "surface": 9},
      {"points": [[14,0,-5],[14,6,-5],[14,6,5]], "surface": 9},
      {"points": [[4,0,-5],[14,6,-5],[14,0,-5]], "surface": 9},
      {"points": [[4,0,-5],[4,6,-5],[14,6,-5]], "surface": 9},
      {"points": [[4,0,5],[14,0,5],[14,6,5]], "surface": 9},
      {"points": [[4,0,5],[14,6,5],[4,6,5]], "surface": 9}
    ],
    "floor": [
      {"points": [[-30,0,-30],[30,0,30],[30,0,-30]], "surface": 9},
      {"points": [[-30,0,-30],[-30,0,30],[30,0,30]], "surface": 9}
    ]
  })"));
}

NavTuning tuning() {
  NavTuning t;
  t.enabled = true;
  t.background = false;
  return t;
}

bool has_link(const std::vector<NavWaypoint> &route, const NavLink link) {
  return std::ranges::any_of(route, [&](const NavWaypoint &w) { return w.link == link; });
}

// One drone walking a single ENPT segment from the street to the roof.
float climb(const bool navigation) {
  BattleSettings settings =
      parse_battle_settings(json::parse(R"({"arena": {"half_size": 100, "blocks": []}})"));
  settings.ai.fight = true;
  settings.ai.original = parse_original_ai(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/robot_ai.json"));
  settings.ai.navigation = tuning();
  settings.ai.navigation.enabled = navigation;
  const EnemyPath path = parse_enemy_path(json::parse(R"({"segments": [
    {"kind": 3, "points": [[-10, 0, 0], [9, 6, 0]], "next": []}
  ]})"));
  Battle battle(settings, path, 7);
  battle.set_stage(roof_stage());
  battle.add_robot("player", 0, {-25.0F, 0.0F, -25.0F}, 0.0F, {}, 100.0F);
  battle.take_out(0);
  const int drone = battle.add_drone("drone", 100.0F);
  battle.robot(drone).ai.ability = 99;
  battle.place(drone, {-10.0F, 0.0F, 0.0F}, -pi * 0.5F);
  battle.robot(drone).path.start(path, 0);
  float highest = 0.0F;
  for (int i = 0; i < 20 * 30; ++i) {
    battle.tick({}, {}, tick);
    const Vec3 at = battle.robot(drone).mover.position();
    if (battle.robot(drone).mover.grounded()) {
      highest = std::max(highest, at.y);
    }
  }
  return highest;
}

} // namespace

TEST_CASE("the navigation grid reaches a roof by a held jump and leaves it by a drop") {
  const NavGraph graph = NavGraph::build(roof_stage(), tuning());
  const int street = graph.nearest({-10.0F, 0.0F, 0.0F});
  const int roof = graph.nearest({9.0F, 6.0F, 0.0F});
  REQUIRE(street >= 0);
  REQUIRE(roof >= 0);
  CHECK(std::fabs(graph.nodes()[static_cast<std::size_t>(street)].position.y) < 0.01F);
  CHECK(std::fabs(graph.nodes()[static_cast<std::size_t>(roof)].position.y - 6.0F) < 0.01F);
  const auto up = graph.path(street, roof, {}, 0.0F);
  REQUIRE(!up.empty());
  CHECK(has_link(up, NavLink::jump));
  CHECK(!has_link(up, NavLink::drop));
  const auto down = graph.path(roof, street, {}, 0.0F);
  REQUIRE(!down.empty());
  CHECK(has_link(down, NavLink::drop));
  CHECK(!has_link(down, NavLink::jump));
  // No node stands inside the block's walls.
  for (const NavNode &node : graph.nodes()) {
    const Vec3 p = node.position;
    const bool inside = p.x > 4.0F && p.x < 14.0F && p.z > -5.0F && p.z < 5.0F;
    CHECK(!inside || std::fabs(p.y - 6.0F) < 0.01F);
  }
}

TEST_CASE("the reach flood finds exactly the nodes a costed search reaches") {
  const NavGraph graph = NavGraph::build(roof_stage(), tuning());
  CHECK(graph.reachable_from(-1).empty());
  const int street = graph.nearest({-10.0F, 0.0F, 0.0F});
  REQUIRE(street >= 0);
  const std::vector<bool> reach = graph.reachable_from(street);
  REQUIRE(reach.size() == graph.nodes().size());
  std::vector<bool> costed(graph.nodes().size(), false);
  for (const auto &[node, cost] : graph.costs_from(street, std::numeric_limits<float>::max())) {
    costed[static_cast<std::size_t>(node)] = true;
  }
  CHECK(reach == costed);
}

TEST_CASE("an avoided waypoint sends the route another way") {
  const NavGraph graph = NavGraph::build(roof_stage(), tuning());
  const int from = graph.nearest({-10.0F, 0.0F, 0.0F});
  const int to = graph.nearest({24.0F, 0.0F, 0.0F});
  const auto direct = graph.path(from, to, {}, 0.0F);
  REQUIRE(direct.size() > 2);
  const int blocked = direct[direct.size() / 2].node;
  const NavAvoid avoid{-1, blocked, 1.0F};
  const auto around = graph.path(from, to, {&avoid, 1}, 1000.0F);
  REQUIRE(!around.empty());
  CHECK(std::ranges::none_of(around, [&](const NavWaypoint &w) { return w.node == blocked; }));
}

TEST_CASE("a computer robot with navigation climbs to a route point on a roof") {
  // The original's straight line and tapped recovery jump stay on the street.
  CHECK(climb(false) < 1.0F);
  CHECK(climb(true) > 5.5F);
}
