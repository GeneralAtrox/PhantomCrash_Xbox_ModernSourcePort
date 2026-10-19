#include "test_support.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/battle_chatter.hpp"

#include <algorithm>
#include <cmath>

using namespace phantomcrash;

namespace {
StageObjects vehicle() {
  return parse_stage_objects(json::parse(R"([{"stage":0,"objects":[{
    "kind":0,"mesh":14,"wreck_mesh":15,"break_effect":14,
    "break_sound":"sound/sfx/battle/16.wav","wreck_solid":true,
    "position":[5,0,0],"rotation":[0,0,0],
    "collision":[[0,0,-8,0,4,-8,0,4,8],[0,0,-8,0,4,8,0,0,8]]
  },{"kind":6}]}])"),
                             0);
}

std::vector<WeaponHit> shoot(const std::vector<Target> &targets, const Vec3 from,
                             const StageCollision &walls = {}, float blast = 0.0F) {
  WeaponTuning gun;
  gun.speed = 600.0F;
  gun.blast_radius = blast;
  Armory armory;
  armory.fire(gun, 0, 0, from, {1, 0, 0}, -1, targets);
  std::vector<WeaponHit> hits;
  armory.tick(1.0F / 30.0F, targets, walls, hits);
  // The hits' weapon pointers are not used after this fixture returns.
  return hits;
}

Target target(const StageObject &object) {
  return {object.centre, object.radius, -1, object.solid(), &object.collision, false};
}
} // namespace

TEST_CASE("city vehicles switch to the extracted wreck once and retain it") {
  StageObjects objects = vehicle();
  REQUIRE(objects.objects().size() == 1); // gates belong to StageDoors
  CHECK(objects.objects()[0].visible_mesh() == 14);
  const auto broken = objects.hit(0);
  REQUIRE(broken.has_value());
  CHECK(broken->effect == 14);
  CHECK(broken->at.x == 5.0F);
  CHECK(objects.objects()[0].visible_mesh() == 15);
  CHECK(objects.objects()[0].solid());
  REQUIRE(objects.take_sounds().size() == 1);
  objects.tick(20.0F);
  CHECK(!objects.hit(0));
  CHECK(objects.take_sounds().empty());
  CHECK(objects.objects()[0].visible_mesh() == 15);
}

TEST_CASE("type one wrecks cannot break twice in the same volley or burning interval") {
  StageObject object = vehicle().objects()[0];
  object.wreck_break_effect = 15;
  object.wreck_hit_delay = 6.0F;
  StageObjects objects({object});
  REQUIRE(objects.hit(0));
  CHECK(!objects.hit(0));
  objects.tick(5.9F);
  CHECK(!objects.hit(0));
  CHECK(objects.objects()[0].visible_mesh() == 15);
  objects.tick(0.2F);
  const auto second = objects.hit(0);
  REQUIRE(second);
  CHECK(second->effect == 15);
  CHECK(objects.objects()[0].visible_mesh() == -1);
  CHECK(!objects.objects()[0].solid());
}

TEST_CASE("props without wreck meshes vanish and permanent scenery remains intact") {
  StageObject object = vehicle().objects()[0];
  object.wreck_mesh = -1;
  StageObject permanent = object;
  permanent.break_effect = -1;
  StageObjects objects({object, permanent});
  REQUIRE(objects.hit(0));
  CHECK(!objects.objects()[0].solid());
  CHECK(objects.objects()[0].visible_mesh() == -1);
  CHECK(!objects.hit(1));
  CHECK(objects.objects()[1].visible_mesh() == 14);
}

TEST_CASE("projectiles hit the long end of a vehicle without hitting empty space around it") {
  const StageObjects objects = vehicle();
  const Target body = target(objects.objects()[0]);
  const auto end = shoot({body}, {0, 2, 7});
  REQUIRE(end.size() == 1);
  CHECK(std::abs(end[0].position.x - 5.0F) < 0.001F);
  CHECK(shoot({body}, {0, 5, 7}).empty());
  CHECK(shoot({body}, {0, 2, 9}).empty());
  CHECK(std::abs(body.distance_to({5, 2, 9}) - 1.0F) < 0.001F);
}

TEST_CASE("retained wrecks stop walking robots and shots until the wreck is removed") {
  StageObject object = vehicle().objects()[0];
  // Face the approach from x < 5; shot tests are two-sided but movement isn't.
  auto walls = object.collision.walls();
  for (auto &wall : walls) {
    std::swap(wall.points[1], wall.points[2]);
    wall.normal = wall.normal * -1.0F;
  }
  object.collision = StageCollision(std::move(walls), {});
  object.wreck_break_effect = 15;
  object.wreck_hit_delay = 6.0F;
  Battle battle(BattleSettings{}, {});
  battle.add_robot("player", 0, {}, -pi / 2.0F, {}, 100.0F);
  battle.set_objects(StageObjects({object}));
  RobotControl walk;
  walk.move = {0.0F, 0.5F};
  const auto advance = [&] {
    for (int i = 0; i < 180; ++i) {
      battle.tick(walk, {}, 1.0F / 60.0F);
    }
  };
  advance();
  CHECK(battle.player().mover.position().x <= 5.0F - battle.player().radius + 0.001F);
  const auto first_break = battle.objects()->hit(0);
  REQUIRE(first_break);
  advance();
  CHECK(battle.player().mover.position().x <= 5.0F - battle.player().radius + 0.001F);
  CHECK(shoot({target(battle.objects()->objects()[0])}, {0, 2, 0}).size() == 1);
  battle.objects()->tick(6.0F);
  const auto second_break = battle.objects()->hit(0);
  REQUIRE(second_break);
  advance();
  CHECK(battle.player().mover.position().x > 6.0F);
  CHECK(shoot({target(battle.objects()->objects()[0])}, {0, 2, 0}).empty());
}

TEST_CASE("robots can stand on wrecks and fall when the wreck is removed") {
  StageObject object = vehicle().objects()[0];
  object.collision = StageCollision({{{Vec3{-4, 4, -4}, Vec3{4, 4, 4}, Vec3{4, 4, -4}}, {0, 1, 0}},
                                     {{Vec3{-4, 4, -4}, Vec3{-4, 4, 4}, Vec3{4, 4, 4}}, {0, 1, 0}}},
                                    {});
  object.wreck_break_effect = 15;
  Battle battle(BattleSettings{}, {});
  battle.add_robot("player", 0, {0, 4, 0}, 0.0F, {}, 100.0F);
  battle.set_objects(StageObjects({object}));
  const auto first_break = battle.objects()->hit(0);
  REQUIRE(first_break);
  CHECK(battle.objects()->ground_height({0, 4, 0}, 0.1F) == 4.0F);
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, {}, 1.0F / 60.0F);
  }
  CHECK(std::abs(battle.player().mover.position().y - 4.0F) < 0.001F);
  CHECK(battle.player().mover.grounded());
  const auto second_break = battle.objects()->hit(0);
  REQUIRE(second_break);
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, {}, 1.0F / 60.0F);
  }
  CHECK(std::abs(battle.player().mover.position().y) < 0.001F);
}

TEST_CASE("scenery intercepts shots before robots and walls block shots before scenery") {
  const StageObjects objects = vehicle();
  const Target body = target(objects.objects()[0]);
  const std::vector<Target> targets{{{10, 2, 0}, 1.0F, 1, true}, body};
  const auto hits = shoot(targets, {0, 2, 0});
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].victim == 1);
  CHECK(shoot(targets, {0, 2, 0}, StageCollision::box({3.0F, {}}, 10.0F)).empty());
}

TEST_CASE("blast reach uses vehicle surfaces rather than their oversized bounding sphere") {
  const StageObjects objects = vehicle();
  const Target body = target(objects.objects()[0]);
  // A robot impact one metre beyond the vehicle's end reaches the vehicle.
  const auto hits = shoot({body, {{6, 2, 9}, 1.0F, 1, true}}, {0, 2, 9}, {}, 2.0F);
  CHECK(std::ranges::any_of(hits, [](const auto &hit) { return hit.victim == 0; }));
  // Six metres above the roof is still inside its enclosing sphere but outside the blast.
  const auto clear = shoot({body, {{6, 10, 0}, 1.0F, 1, true}}, {0, 10, 0}, {}, 2.0F);
  CHECK(std::ranges::none_of(clear, [](const auto &hit) { return hit.victim == 0; }));
}

TEST_CASE("battle shots produce scenery explosions and wrecks without robot kill credit") {
  BattleSettings settings;
  settings.weapons["gun"].speed = 300.0F;
  Battle battle(settings, {});
  battle.add_robot("player", 0, {}, -pi / 2.0F, {"gun", "", "", ""}, 100.0F);
  battle.set_objects(vehicle());
  FireControl fire;
  fire.fire[0] = true;
  int explosions = 0;
  int remarks = 0;
  for (int frame = 0; frame < 60; ++frame) {
    battle.tick({}, fire, 1.0F / 60.0F);
    for (const auto &event : battle.events()) {
      if (event.kind == BattleEventKind::effect && event.slot == 14) {
        ++explosions;
      }
      if (event.kind == BattleEventKind::chatter && event.chatter == chatter_event::object_broken) {
        ++remarks; // 0x047ed9: the shooter remarks on the break
      }
      CHECK(event.kind != BattleEventKind::hit);
      CHECK(event.kind != BattleEventKind::shot_down);
    }
  }
  CHECK(explosions == 1);
  CHECK(remarks == 1);
  CHECK(battle.objects()->objects()[0].visible_mesh() == 15);
  CHECK(battle.player().kills == 0);
}

TEST_CASE("destroyed object slots do not shift supply target indices") {
  BattleSettings settings;
  settings.weapons["gun"].speed = 300.0F;
  settings.weapons["gun"].cooldown = 0.1F;
  Battle battle(settings, {});
  battle.add_robot("player", 0, {}, -pi / 2.0F, {"gun", "", "", ""}, 100.0F);
  StageObject object = vehicle().objects()[0];
  object.wreck_solid = false;
  battle.set_objects(StageObjects({object}));
  SupplySettings supply;
  SupplyRoute route;
  route.segments.resize(4, {Vec3{12, 2, 0}, Vec3{12, 2, 0}, Vec3{12, 2, 0}, Vec3{12, 2, 0}});
  route.closing = 1;
  route.drop = 2;
  route.leave = 3;
  supply.routes.push_back(route);
  supply.speed = 0.0F;
  battle.set_supply(supply);
  FireControl fire;
  fire.fire[0] = true;
  bool carrier = false;
  for (int frame = 0; frame < 60 && !carrier; ++frame) {
    battle.tick({}, fire, 1.0F / 60.0F);
    for (const auto &event : battle.events()) {
      if (event.kind == BattleEventKind::carrier_down) {
        carrier = true;
      }
    }
  }
  CHECK(battle.objects()->objects()[0].destroyed);
  CHECK(carrier);
}
