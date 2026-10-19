// Port navigation getting unstuck: jumps that need room for the body, climbs
// the grid cannot follow, robots placed under the street and dead-end route
// points (nav_graph.hpp, battle_ai_steering.cpp).

#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/nav_graph.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;

// A floor from -30 to 30, a block 6 high over x 4..14, z -5..5, and a canopy
// 5 high over z 8..14 whose underside a jumping robot meets: a take-off at
// z 7 has room above its middle but not above its body.
StageCollision canopy_stage() {
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
      {"points": [[4,0,5],[14,6,5],[4,6,5]], "surface": 9},
      {"points": [[-30,5,8],[30,5,8],[30,5,14]], "surface": 9},
      {"points": [[-30,5,8],[30,5,14],[-30,5,14]], "surface": 9}
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

BattleSettings settings() {
  BattleSettings s =
      parse_battle_settings(json::parse(R"({"arena": {"half_size": 100, "blocks": []}})"));
  s.ai.fight = true;
  s.ai.original = parse_original_ai(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/robot_ai.json"));
  s.ai.navigation = tuning();
  return s;
}

// The farthest a drone gets from `from` in `seconds`, walking `path` from
// its segment 0.
float wander(const EnemyPath &path, const Vec3 from, const float seconds) {
  Battle battle(settings(), path, 11);
  battle.set_stage(canopy_stage());
  battle.add_robot("player", 0, {-25.0F, 0.0F, -25.0F}, 0.0F, {}, 100.0F);
  battle.take_out(0);
  const int drone = battle.add_drone("drone", 100.0F);
  battle.place(drone, from, 0.0F);
  battle.robot(drone).path.start(path, 0);
  float farthest = 0.0F;
  for (int i = 0; i < static_cast<int>(seconds / tick); ++i) {
    battle.tick({}, {}, tick);
    Vec3 offset = battle.robot(drone).mover.position() - from;
    offset.y = 0.0F;
    farthest = std::max(farthest, length(offset));
  }
  return farthest;
}

} // namespace

TEST_CASE("a planned jump needs room for the robot's body above its take-off") {
  const NavTuning t = tuning();
  const NavGraph graph = NavGraph::build(canopy_stage(), t);
  bool climbs = false;
  for (std::size_t a = 0; a < graph.nodes().size(); ++a) {
    const Vec3 from = graph.nodes()[a].position;
    for (const NavEdge &edge : graph.edges_of(static_cast<int>(a))) {
      if (edge.link != NavLink::jump || graph.nodes()[edge.to].position.y < 5.0F) {
        continue;
      }
      climbs = true;
      // No take-off within a body of the canopy's edge.
      CHECK(from.z < 8.0F - t.body_radius);
    }
  }
  CHECK(climbs); // the roof is still climbed, from the other sides
}

TEST_CASE("a path follower takes no blocked next segment") {
  const EnemyPath path = parse_enemy_path(json::parse(R"({"segments": [
    {"kind": 1, "points": [[0, 0, 0]], "next": [1, 2]},
    {"kind": 7, "points": [[0, 9, 0]], "next": []},
    {"kind": 1, "points": [[9, 0, 0]], "next": []}
  ]})"));
  std::minstd_rand random(3);
  const std::vector<bool> blocked{false, true, false};
  for (int i = 0; i < 20; ++i) {
    PathFollower follower;
    follower.start(path, 0);
    static_cast<void>(follower.update(path, {}, 1.0F, random, &blocked));
    CHECK(follower.segment() == 2);
  }
  // With every next segment blocked, the segment is a dead end.
  const std::vector<bool> all{false, true, true};
  PathFollower follower;
  follower.start(path, 0);
  static_cast<void>(follower.update(path, {}, 1.0F, random, &all));
  CHECK(follower.segment() == 0);
}

TEST_CASE("a robot placed deep under the street stands on it") {
  Battle battle(settings(), EnemyPath{}, 5);
  battle.set_stage(canopy_stage());
  const int robot = battle.add_robot("player", 0, {-20.0F, 0.0F, -20.0F}, 0.0F, {}, 100.0F);
  battle.place(robot, {-20.0F, -5.0F, -20.0F}, 0.0F);
  for (int i = 0; i < 30; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(battle.robot(robot).mover.position().y > -0.1F);
}

TEST_CASE("a computer robot at a dead-end route point moves on to another") {
  // Segment 0 is a single point where the drone stands; the only other
  // segment lies across the street.
  const EnemyPath path = parse_enemy_path(json::parse(R"({"segments": [
    {"kind": 1, "points": [[-20, 0, -20]], "next": []},
    {"kind": 1, "points": [[-20, 0, 20], [20, 0, 20]], "next": []}
  ]})"));
  CHECK(wander(path, {-20.0F, 0.0F, -20.0F}, 30.0F) > 20.0F);
}
