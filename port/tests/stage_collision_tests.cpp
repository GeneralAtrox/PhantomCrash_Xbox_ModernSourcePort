// Stage collision: the converted walls and floor, and robots and shots
// meeting them.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/battle.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/stage_collision.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

// A floor from -20 to 20, a wall across z at x = 5 facing -x, and a
// platform top 3 high over x -10..-6.
StageCollision sample_stage() {
  return parse_stage_collision(json::parse(R"({
    "walls": [
      {"points": [[5, 0, -20], [5, 0, 20], [5, 10, -20]], "surface": 9},
      {"points": [[5, 10, -20], [5, 0, 20], [5, 10, 20]], "surface": 8},
      {"points": [[-10, 3, -4], [-10, 3, 4], [-6, 3, -4]], "surface": 9},
      {"points": [[-6, 3, -4], [-10, 3, 4], [-6, 3, 4]], "surface": 9}
    ],
    "floor": [
      {"points": [[-20, 0, -20], [-20, 0, 20], [20, 0, -20]]},
      {"points": [[20, 0, -20], [-20, 0, 20], [20, 0, 20]]}
    ]
  })"));
}

} // namespace

TEST_CASE("collision triangles take their normal from the winding") {
  const StageCollision stage = sample_stage();
  CHECK(std::fabs(stage.walls()[0].normal.x + 1.0F) < 1e-5F);
  CHECK(stage.floor()[0].normal.y > 0.99F);
  CHECK(surface_flags(8) == stage_contact::fence);
  CHECK(surface_flags(1009) == surface_flags(9));
}

TEST_CASE("the ground is the floor, or a top within a step of the feet") {
  const StageCollision stage = sample_stage();
  CHECK(std::fabs(stage.ground_height({0.0F, 0.0F, 0.0F}, 0.5F)) < 1e-5F);
  CHECK(std::fabs(stage.ground_height({-8.0F, 3.2F, 0.0F}, 0.5F) - 3.0F) < 1e-5F);
  CHECK(std::fabs(stage.ground_height({-8.0F, 1.0F, 0.0F}, 0.5F)) < 1e-5F); // under the top
  CHECK(stage.ground_height({30.0F, 0.0F, 0.0F}, 0.5F) == no_ground);
}

TEST_CASE("spheres are pushed out of the front of walls only") {
  const StageCollision stage = sample_stage();
  Vec3 front{4.5F, 2.0F, 0.0F};
  const unsigned contact = stage.push_sphere(front, 1.0F);
  CHECK(std::fabs(front.x - 4.0F) < 1e-4F);
  CHECK((contact & stage_contact::wall) != 0);
  Vec3 behind{5.5F, 2.0F, 0.0F};
  stage.push_sphere(behind, 1.0F);
  CHECK(std::fabs(behind.x - 5.5F) < 1e-4F);
  // Resting on the platform counts as ground.
  Vec3 on_top{-8.0F, 3.5F, 0.0F};
  CHECK((stage.push_sphere(on_top, 1.0F) & stage_contact::ground) != 0);
  CHECK(std::fabs(on_top.y - 4.0F) < 1e-4F);
}

TEST_CASE("segments stop at the first wall or floor they cross") {
  const StageCollision stage = sample_stage();
  const auto hit = stage.segment_hit({0.0F, 2.0F, 0.0F}, {10.0F, 2.0F, 0.0F});
  CHECK(hit.has_value());
  CHECK(std::fabs(hit->x - 5.0F) < 1e-4F);
  CHECK(!stage.segment_hit({0.0F, 2.0F, 0.0F}, {4.0F, 2.0F, 0.0F}).has_value());
  const auto ground = stage.segment_hit({0.0F, 2.0F, 0.0F}, {0.0F, -2.0F, 0.0F});
  CHECK(ground.has_value() && std::fabs(ground->y) < 1e-4F);
}

TEST_CASE("segments beside a small collision's bounds miss and one ending on its face hits") {
  // A placed object's one wall: x = 1, y 0..2, z -1..1, in a single grid cell.
  const StageCollision crate = parse_stage_collision(json::parse(R"({
    "walls": [
      {"points": [[1, 0, -1], [1, 0, 1], [1, 2, -1]]},
      {"points": [[1, 2, -1], [1, 0, 1], [1, 2, 1]]}
    ],
    "floor": []
  })"));
  CHECK(!crate.segment_hit({-40.0F, 1.0F, 30.0F}, {40.0F, 1.0F, 30.0F}).has_value());
  CHECK(!crate.segment_hit({0.0F, 5.0F, 0.0F}, {3.0F, 5.0F, 0.0F}).has_value());
  const auto face = crate.segment_hit({-3.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F});
  CHECK(face.has_value() && std::fabs(face->x - 1.0F) < 1e-4F);
  CHECK(!StageCollision{}.segment_hit({-1.0F, 1.0F, 0.0F}, {3.0F, 1.0F, 0.0F}).has_value());
}

TEST_CASE("robots walking into a stage wall stop at their radius") {
  Battle battle(parse_battle_settings(json::parse("{}")), EnemyPath{});
  battle.set_stage(sample_stage());
  // Heading -pi/2 faces +x (see heading_forward).
  const int player = battle.add_robot("player", 0, {0.0F, 0.0F, 0.0F}, -pi * 0.5F, {}, 100.0F);
  battle.set_radius(player, 1.5F);
  RobotControl control;
  control.move = {0.0F, 1.0F};
  for (int i = 0; i < 300; ++i) {
    battle.tick(control, {}, tick);
  }
  const Vec3 at = battle.player().mover.position();
  CHECK(at.x > 3.0F);
  CHECK(at.x <= 3.5F + 1e-3F);
  CHECK((battle.player().stage_contact & stage_contact::wall) != 0);
  CHECK(battle.player().mover.grounded());
}

TEST_CASE("robots land on a platform they jump onto and walk off its edge") {
  Battle battle(parse_battle_settings(json::parse("{}")), EnemyPath{});
  battle.set_stage(sample_stage());
  battle.add_robot("player", 0, {-8.0F, 5.0F, 0.0F}, 0.0F, {}, 100.0F);
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(std::fabs(battle.player().mover.position().y - 3.0F) < 1e-3F);
  CHECK(battle.player().mover.grounded());
  RobotControl control;
  control.move = {0.0F, 1.0F}; // heading 0 walks toward -z, off the top
  for (int i = 0; i < 60; ++i) {
    battle.tick(control, {}, tick);
  }
  for (int i = 0; i < 90; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(std::fabs(battle.player().mover.position().y) < 1e-3F);
  CHECK(battle.player().mover.grounded());
}

TEST_CASE("shots stop at walls") {
  Battle battle(parse_battle_settings(json::parse(R"({
    "weapons": {"handgun": {"kind": "gun", "damage": 40, "cooldown": 0.1, "speed": 300}}
  })")),
                EnemyPath{});
  battle.set_stage(sample_stage());
  battle.add_robot("player", 0, {0.0F, 0.0F, 0.0F}, -pi * 0.5F, {"handgun", "", "", ""}, 100.0F);
  const int target = battle.add_robot("target", 1, {12.0F, 0.0F, 0.0F}, 0.0F, {}, 50.0F);
  FireControl fire;
  fire.fire[0] = true;
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, fire, tick);
  }
  CHECK(battle.robots()[static_cast<std::size_t>(target)].hp == 50.0F);
}

TEST_CASE("a sphere past a ledge's lip stands on the lip while its push still counts as ground") {
  const StageCollision stage = sample_stage();
  const float radius = 2.0F;
  // The platform's top ends at x = -6; the centre is 0.6 past it.
  const float lip = 3.0F + std::sqrt(radius * radius - 0.36F) - radius;
  CHECK(std::fabs(stage.ground_height({-5.4F, lip, 0.0F}, 0.5F, radius) - lip) < 1e-3F);
  // Without a radius there is nothing under the centre but the floor.
  CHECK(std::fabs(stage.ground_height({-5.4F, lip, 0.0F}, 0.5F)) < 1e-5F);
  // Further out the push turns sideways and the robot falls.
  CHECK(std::fabs(stage.ground_height({-4.5F, 2.0F, 0.0F}, 0.5F, radius)) < 1e-5F);
}

TEST_CASE("robots resting on a ledge's lip are not held in a jump and can walk away") {
  Battle battle(parse_battle_settings(json::parse("{}")), EnemyPath{});
  battle.set_stage(sample_stage());
  const int player = battle.add_robot("player", 0, {-5.4F, 2.92F, 0.0F}, -pi * 0.5F, {}, 100.0F);
  battle.set_radius(player, 2.0F);
  for (int i = 0; i < 60; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(battle.player().mover.grounded());
  CHECK(battle.player().mover.mode() != MoveMode::jump);
  RobotControl control;
  control.move = {0.0F, 1.0F};
  for (int i = 0; i < 90; ++i) {
    battle.tick(control, {}, tick);
  }
  CHECK(battle.player().mover.position().x > -1.0F);
  CHECK(std::fabs(battle.player().mover.position().y) < 1e-3F);
}

TEST_CASE("robots walking into a step in the floor triangles stand on it") {
  // Shibuya's station: the floor rises 2 onto a stair landing, the rise and
  // its face both floor triangles, which never push.
  Battle battle(parse_battle_settings(json::parse("{}")), EnemyPath{});
  battle.set_stage(parse_stage_collision(json::parse(R"({
    "floor": [
      {"points": [[-20, 0, -20], [-20, 0, 20], [0, 0, -20]]},
      {"points": [[0, 0, -20], [-20, 0, 20], [0, 0, 20]]},
      {"points": [[0, 0, -20], [0, 2, 20], [0, 0, 20]]},
      {"points": [[0, 2, -20], [0, 2, 20], [20, 2, -20]]},
      {"points": [[20, 2, -20], [0, 2, 20], [20, 2, 20]]}
    ]
  })")));
  CHECK(std::fabs(battle.stage().floor_under(5.0F, 0.0F) - 2.0F) < 1e-5F);
  CHECK(battle.stage().floor_under(30.0F, 0.0F) == no_ground);
  const int player = battle.add_robot("player", 0, {-5.0F, 0.0F, 0.0F}, -pi * 0.5F, {}, 100.0F);
  battle.set_radius(player, 1.5F);
  RobotControl control;
  control.move = {0.0F, 1.0F};
  for (int i = 0; i < 60; ++i) { // onto the landing, short of its far end
    battle.tick(control, {}, tick);
  }
  CHECK(battle.player().mover.position().x > 1.0F);
  CHECK(std::fabs(battle.player().mover.position().y - 2.0F) < 1e-3F);
  CHECK(battle.player().mover.grounded());
}
