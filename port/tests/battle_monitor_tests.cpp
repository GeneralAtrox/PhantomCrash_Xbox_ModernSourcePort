#include "test_support.hpp"
#include "phantomcrash/battle_monitor.hpp"
#include "phantomcrash/training_ground.hpp"

#include <limits>
#include <set>

using namespace phantomcrash;

TEST_CASE("battle monitors cycle living humans and AI without immediate repeats") {
  Battle battle({}, {});
  for (int i = 0; i < 4; ++i) {
    battle.add_robot("fighter", i, {static_cast<float>(i * 10), 0, 0}, 0, {}, 100);
  }
  battle.robot(3).alive = false;
  BattleMonitorSettings settings;
  settings.switch_seconds = 2;
  BattleMonitor monitor(settings, 37);
  monitor.tick(battle, 0);
  std::set<int> seen;
  for (int i = 0; i < 50; ++i) {
    const int prior = monitor.subject();
    CHECK(prior >= 0 && prior < 3);
    seen.insert(prior);
    monitor.tick(battle, 1);
    CHECK(monitor.subject() == prior);
    monitor.tick(battle, 1);
    CHECK(monitor.subject() != prior);
  }
  CHECK(seen.size() == 3);
}

TEST_CASE("battle monitors cut immediately on death and recover when fighters return") {
  Battle battle({}, {});
  battle.add_robot("player", 0, {}, 0, {}, 100);
  battle.add_robot("AI", 1, {20, 0, 0}, 0, {}, 100);
  BattleMonitor monitor({}, 5);
  monitor.tick(battle, 0);
  const int dead = monitor.subject();
  battle.robot(dead).alive = false;
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == 1 - dead);
  monitor.tick(battle, 20);
  CHECK(monitor.subject() == 1 - dead);
  battle.robot(1 - dead).alive = false;
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == -1);
  battle.robot(dead).alive = true;
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == dead);
}

TEST_CASE("battle monitor timing uses simulation seconds independently of draws") {
  Battle battle({}, {});
  battle.add_robot("player", 0, {}, 0, {}, 100);
  battle.add_robot("AI", 1, {20, 0, 0}, 0, {}, 100);
  BattleMonitor a({}, 17), b({}, 17);
  a.tick(battle, 0);
  b.tick(battle, 0);
  for (int i = 0; i < 80; ++i) {
    a.tick(battle, 0.125F);
    b.tick(battle, 0.0625F);
    b.tick(battle, 0.0625F);
    CHECK(a.subject() == b.subject());
  }
}

TEST_CASE("stage monitor settings bind named surfaces independently of sky") {
  const auto stages = parse_stages(json::parse(R"({
    "monitor": {"switch_seconds": 5, "aspect": 1.5, "camera": {"behind": 20}},
    "stages": {"arena": {"sky_nodes": ["sky"], "monitor_nodes": ["gate", "ship"]}}
  })"));
  const auto &stage = stages.at("arena");
  CHECK(stage.monitor_nodes.size() == 2);
  CHECK(stage.sky_nodes.size() == 1);
  CHECK(stage.monitor.switch_seconds == 5);
  CHECK(stage.monitor.aspect == 1.5F);
  CHECK(stage.monitor.camera.behind == 20);
  CHECK_THROWS(parse_battle_monitor(json::parse(R"({"switch_seconds": 0})")));
}

TEST_CASE("monitors exclude waiting robots and switch immediately when a subject exits") {
  Battle battle({}, {});
  battle.add_robot("fighter", 0, {}, 0, {}, 100);
  battle.add_robot("waiting in elevator", 1, {20, 0, 0}, 0, {}, 100);
  battle.add_robot("outside", 2, {120, 0, 0}, 0, {}, 100);
  battle.script(1, {});
  BattleMonitor monitor({}, 3);
  for (int i = 0; i < 10; ++i) {
    monitor.tick(battle, 10);
    CHECK(monitor.subject() == 0);
  }
  battle.release(1);
  battle.raise_shield(1); // playable shielded robots remain eligible
  battle.script(0, {});   // selected robot starts leaving
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == 1);
  battle.place(1, {120, 0, 0}, 0);
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == -1); // black feed, never the previous outside subject
  battle.place(2, {0, 12, 0}, 0); // an airborne fighter back over the arena
  monitor.tick(battle, 0);
  CHECK(monitor.subject() == 2);
}

TEST_CASE("monitor play area uses actual floor and fences rather than the fallback arena box") {
  auto enclosure = StageCollision::box(ArenaBounds{50, {}}, 30);
  auto walls = enclosure.walls();
  for (auto &wall : walls) {
    wall.surface = 8;
  }
  // Ground extends beyond the fence, like entrance scenery. A shifted
  // arena also checks that eligibility does not assume a world-origin box.
  auto floor = StageCollision::box(ArenaBounds{80, {}}, 30).floor();
  const Vec3 shift{250, 0, -200};
  for (auto *triangles : {&walls, &floor}) {
    for (auto &triangle : *triangles) {
      for (auto &point : triangle.points) {
        point = point + shift;
      }
    }
  }
  // Downward-facing force-field ceiling.
  walls.push_back(
      {{{shift + Vec3{-50, 30, -50}, shift + Vec3{50, 30, -50}, shift + Vec3{-50, 30, 50}}},
       {0, -1, 0},
       8});
  walls.push_back(
      {{{shift + Vec3{50, 30, -50}, shift + Vec3{50, 30, 50}, shift + Vec3{-50, 30, 50}}},
       {0, -1, 0},
       8});
  StageCollision stage(std::move(walls), std::move(floor));
  CHECK(stage.in_play_area(shift));
  CHECK(stage.in_play_area(shift + Vec3{0, 15, 0}));
  CHECK(stage.in_play_area(shift + Vec3{49, 0, 49}));
  CHECK(stage.in_play_area(shift + Vec3{50, 0, 50}));
  CHECK(!stage.in_play_area(shift + Vec3{55, 0, 0}));
  CHECK(!stage.in_play_area(shift + Vec3{0, 0, -55}));
  CHECK(!stage.in_play_area(shift + Vec3{-55, 0, -55}));
  CHECK(!stage.in_play_area(shift + Vec3{0, 35, 0}));
  CHECK(!stage.in_play_area(shift + Vec3{0, -10, 0}));
  CHECK(!stage.in_play_area({std::numeric_limits<float>::quiet_NaN(), 0, 0}));
  Battle battle({}, {});
  battle.set_stage(std::move(stage));
  battle.add_robot("inside", 0, shift, 0, {}, 100);
  battle.add_robot("outside fence but over ground", 1, shift + Vec3{55, 0, 0}, 0, {}, 100);
  BattleMonitor monitor({}, 7);
  for (int i = 0; i < 20; ++i) {
    monitor.tick(battle, 10);
    CHECK(monitor.subject() == 0);
  }
}

TEST_CASE("monitor boundary follows a concave elevated fence and merges duplicate panels") {
  const std::vector<Vec3> outline{{-20, 5, -20}, {20, 5, -20}, {20, 5, 0},
                                  {0, 5, 0},     {0, 5, 20},   {-20, 5, 20}};
  std::vector<CollisionTriangle> walls;
  for (std::size_t i = 0; i < outline.size(); ++i) {
    const Vec3 a = outline[i], b = outline[(i + 1) % outline.size()];
    const Vec3 c = b + Vec3{0, 25, 0}, d = a + Vec3{0, 25, 0};
    const Vec3 normal = normalize(cross(b - a, c - a));
    walls.push_back({{{a, b, c}}, normal, 8});
    walls.push_back({{{a, c, d}}, normal, 8});
    walls.push_back({{{a, b, c}}, normal, 8}); // duplicate face must not toggle parity
  }
  StageCollision stage(std::move(walls), StageCollision::box(ArenaBounds{60, {}}, 30).floor());
  CHECK(stage.in_play_area({-10, 0, 10}));
  CHECK(stage.in_play_area({10, 0, -10}));
  CHECK(!stage.in_play_area({10, 0, 10})); // the notch has floor, but is outside
  CHECK(!stage.in_play_area({-25, 0, -25}));
  CHECK(!stage.in_play_area({25, 0, -10}));
  CHECK(!stage.in_play_area({-25, 0, -10}));
}
