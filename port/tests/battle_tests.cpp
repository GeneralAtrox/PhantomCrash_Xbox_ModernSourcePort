// The battle: robot movement, the enemy path, weapons and the drones, and
// the tutorial battle's data.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/battle.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/match_result.hpp"
#include "phantomcrash/training_battle.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

EnemyPath square_path() {
  return parse_enemy_path(json::parse(R"({"segments": [
    {"kind": 0, "points": [[-80, 0, -80], [-40, 0, -40]], "next": [2]},
    {"kind": 0, "points": [[80, 0, 80], [40, 0, 40]], "next": [2]},
    {"kind": 1, "points": [[-40, 0, -40], [40, 0, -40], [40, 0, 40]], "next": [3]},
    {"kind": 1, "points": [[40, 0, 40], [-40, 0, 40], [-40, 0, -40]], "next": [2]}
  ]})"));
}

BattleSettings test_settings() {
  return parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100, "blocks": [[90, 90, 110, 110]]},
    "weapons": {
      "handgun": {"kind": "gun", "damage": 40, "cooldown": 0.1, "speed": 300},
      "drill": {"kind": "melee", "damage": 60, "range": 6},
      "rifle": {"kind": "gun", "damage": 1, "cooldown": 0.1, "speed": 300, "ammo": 2},
      "missile": {"kind": "missile", "damage": 10, "cooldown": 0.1, "speed": 60,
                  "turn_degrees_per_second": 360, "lock_range": 120}
    },
    "drone_respawn_seconds": 1,
    "ammo_refill_seconds": 1
  })"));
}

} // namespace

TEST_CASE("robots walk forward along -z and turn right with a positive heading") {
  RobotMover mover{MovementTuning{}};
  mover.place({}, 0.0F);
  RobotControl control;
  control.move = {0.0F, 0.5F};
  for (int i = 0; i < 60; ++i) {
    mover.tick(control, tick);
  }
  CHECK(mover.mode() == MoveMode::walk);
  CHECK(mover.position().z < -1.0F);
  CHECK(std::fabs(mover.position().x) < 1e-3F);

  control.move = {1.0F, 0.0F};
  const float before = mover.heading();
  for (int i = 0; i < 30; ++i) {
    mover.tick(control, tick);
  }
  CHECK(mover.heading() > before);
}

TEST_CASE("robots jump, leave the ground and land again") {
  RobotMover mover{MovementTuning{}};
  mover.place({}, 0.0F);
  RobotControl control;
  control.jump = true;
  mover.tick(control, tick);
  control.jump = false;
  float peak = 0.0F;
  for (int i = 0; i < 600 && (peak == 0.0F || !mover.grounded()); ++i) {
    mover.tick(control, tick);
    peak = std::max(peak, mover.position().y);
  }
  CHECK(peak > 2.0F);
  CHECK(mover.grounded());
  CHECK(std::fabs(mover.position().y) < 1e-3F);
}

TEST_CASE("enemy paths list their entries and reject broken links") {
  const EnemyPath path = square_path();
  CHECK(path.entries().size() == 2);
  CHECK_THROWS(parse_enemy_path(
      json::parse(R"({"segments": [{"kind": 0, "points": [[0, 0, 0]], "next": [5]}]})")));

  PathFollower follower;
  follower.start(path, 0);
  std::minstd_rand random(1);
  const Vec3 target = follower.update(path, {-80.0F, 0.0F, -80.0F}, 3.0F, random);
  CHECK(std::fabs(target.x + 40.0F) < 1e-3F);
}

TEST_CASE("drones spawn at separate entries and walk while active") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "drill", "", ""}, 100.0F);
  battle.add_drone("a", 50.0F);
  battle.add_drone("b", 50.0F);
  const Vec3 a = battle.robots()[1].mover.position();
  const Vec3 b = battle.robots()[2].mover.position();
  CHECK(length(a - b) > 10.0F);

  battle.set_drones_active(false);
  for (int i = 0; i < 60; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(length(battle.robots()[1].mover.position() - a) < 1e-3F);
  battle.set_drones_active(true);
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(length(battle.robots()[1].mover.position() - a) > 1.0F);
}

TEST_CASE("no hit hurts a shielded robot") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 100.0F);
  const int drone = battle.add_drone("a", 50.0F);
  battle.set_drones_active(false);
  battle.place_facing_player(drone, 30.0F);
  battle.raise_shield(drone, 60.0F);
  FireControl fire;
  fire.fire[0] = true;
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, fire, tick);
  }
  CHECK(battle.robots()[1].hp == 50.0F);
  CHECK(battle.player().kills == 0);
}

TEST_CASE("weapons outside the allowed mask stay silent") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 100.0F);
  const int drone = battle.add_drone("a", 50.0F);
  battle.set_drones_active(false);
  battle.place_facing_player(drone, 30.0F);
  battle.set_allowed_weapons(0);
  FireControl fire;
  fire.fire[0] = true;
  for (int i = 0; i < 120; ++i) {
    battle.tick({}, fire, tick);
  }
  CHECK(battle.player().kills == 0);
  CHECK(battle.armory().projectiles().empty());
}

TEST_CASE("missiles lock after the fire control's lock time; guns at once") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "missile", ""}, 100.0F);
  const int drone = battle.add_drone("a", 1000.0F);
  battle.set_drones_active(false);
  battle.place_facing_player(drone, 30.0F);
  battle.set_lock_seconds(0, 0.5F);
  battle.tick({}, {}, tick);
  CHECK(battle.player_lock(WeaponSlot::right_arm) == drone);
  CHECK(battle.player_lock_candidate(WeaponSlot::right_shoulder) == drone);
  CHECK(battle.player_lock(WeaponSlot::right_shoulder) == -1);
  for (int i = 0; i < 29; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(battle.player_lock(WeaponSlot::right_shoulder) == -1);
  for (int i = 0; i < 2; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(battle.player_lock(WeaponSlot::right_shoulder) == drone);
  CHECK(battle.player_lock_held(WeaponSlot::right_shoulder) >= 0.5F);
}

TEST_CASE("weapons spend their ammunition and refill when empty") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"rifle", "drill", "", ""}, 100.0F);
  CHECK(battle.player().magazines[0].max == 2);
  CHECK(!battle.player().magazines[1].limited()); // melee needs none
  FireControl fire;
  fire.fire[0] = true;
  for (int i = 0; i < 30; ++i) {
    battle.tick({}, fire, tick);
  }
  CHECK(battle.player().magazines[0].empty());
  CHECK(battle.armory().projectiles().size() <= 2);
  for (int i = 0; i < 60; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(battle.player().magazines[0].ammo == 2);
  CHECK(battle.player().magazines[0].refilling());
}

TEST_CASE("hits and shoot-downs become the HUD's events") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 100.0F);
  const int drone = battle.add_drone("a", 50.0F);
  battle.set_drones_active(false);
  battle.place_facing_player(drone, 30.0F);
  FireControl fire;
  fire.fire[0] = true;
  int hits = 0;
  int bingos = 0;
  int shot_down = 0;
  int rumbled = 0;
  for (int i = 0; i < 120 && battle.player().kills == 0; ++i) {
    battle.tick({}, fire, tick);
    for (const BattleEvent &event : battle.events()) {
      hits += event.robot == 0 && event.kind == BattleEventKind::hit ? 1 : 0;
      bingos += event.robot == 0 && event.kind == BattleEventKind::bingo ? 1 : 0;
      shot_down += event.robot == drone && event.kind == BattleEventKind::shot_down ? 1 : 0;
      rumbled += event.kind == BattleEventKind::damaged ? 1 : 0;
    }
  }
  CHECK(rumbled == 0); // only a piloted robot's damage rumbles its pad
  CHECK(hits == 2);    // 40 damage a shot against 50 armour
  CHECK(bingos == 1);
  CHECK(shot_down == 1);
  battle.tick({}, {}, tick);
  CHECK(battle.events().empty());
}

TEST_CASE("a damaging hit pays its attacker hit_reward_ny") {
  for (const int reward_ny : {0, 10}) {
    BattleSettings settings = test_settings();
    settings.hit_reward_ny = reward_ny;
    Battle battle(settings, square_path());
    battle.add_robot("player", 0, {}, 0.0F, {"handgun", "", "", ""}, 100.0F);
    const int drone = battle.add_drone("a", 50.0F);
    battle.set_drones_active(false);
    battle.place_facing_player(drone, 30.0F);
    FireControl fire;
    fire.fire[0] = true;
    std::int64_t paid = 0;
    for (int i = 0; i < 120 && battle.player().kills == 0; ++i) {
      battle.tick({}, fire, tick);
      paid += match_event_prize(battle.events(), 0);
    }
    CHECK(paid == 2 * reward_ny / 10); // two hits, stored in tens of NY
  }
}

TEST_CASE("robots stay inside the arena and out of its blocks") {
  Battle battle(test_settings(), square_path());
  battle.add_robot("player", 0, {95.0F, 0.0F, 80.0F}, pi, {}, 100.0F);
  RobotControl control;
  control.move = {0.0F, 0.6F};
  for (int i = 0; i < 600; ++i) {
    battle.tick(control, {}, tick);
  }
  const Vec3 at = battle.player().mover.position();
  CHECK(at.z <= 90.0F);
  CHECK(at.x <= 100.0F);
}

TEST_CASE("a robot placed a little under the ground stands on it") {
  Battle battle(test_settings(), square_path());
  battle.set_stage(StageCollision::box(ArenaBounds{}, 30.0F));
  battle.add_robot("player", 0, {}, 0.0F, {}, 100.0F);
  // Enemy path points lie up to about 1.5 under the street.
  battle.place(0, {10.0F, -1.5F, 10.0F}, 0.0F);
  for (int i = 0; i < 30; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(std::abs(battle.player().mover.position().y) < 0.01F);
  CHECK(battle.player().mover.grounded());
}

TEST_CASE("the shown armour drains after a hit and flashes as it fills") {
  ShownArmour shown;
  shown.tick(1000.0F, 1000.0F, tick);
  CHECK(shown.value() == 1000.0F);
  // A fifth lost: 0.2 x 90 frames, 18 frames at 60 a second (0x076368).
  shown.tick(800.0F, 1000.0F, 0.0F);
  CHECK(shown.value() == 1000.0F);
  shown.tick(800.0F, 1000.0F, 9.0F / 60.0F);
  CHECK(std::abs(shown.value() - 900.0F) < 0.01F);
  CHECK(!shown.filling());
  shown.tick(800.0F, 1000.0F, 9.0F / 60.0F);
  CHECK(shown.value() == 800.0F);
  // A twentieth repaired: 0.05 x 300 frames, filling and flashing.
  shown.tick(850.0F, 1000.0F, 5.0F / 60.0F);
  CHECK(shown.filling());
  shown.tick(850.0F, 1000.0F, 10.0F / 60.0F);
  CHECK(!shown.filling());
  CHECK(shown.value() == 850.0F);
}

TEST_CASE("the tutorial battle's data names its weapons and buttons") {
  const TrainingBattle setup = parse_training_battle(json::parse(R"({
    "weapons": {"handgun": {"kind": "gun"}},
    "buttons": {"fire": {"right_arm": "a"}, "exit": ["start"]},
    "player": {"weapons": {"right_arm": "handgun"}, "hp": 250, "start": [0, 0, 10]},
    "drone": {"count": 2}
  })"));
  CHECK(setup.buttons.fire[0] == button::a);
  CHECK(setup.buttons.fire[1] == button::left_trigger);
  CHECK(setup.buttons.exit == button::start);
  CHECK(setup.player.weapons[0] == "handgun");
  CHECK(setup.player.hp == 250.0F);
  CHECK(setup.drones == 2);
  CHECK_THROWS(
      parse_training_battle(json::parse(R"({"player": {"weapons": {"right_arm": "laser"}}})")));

  InputFrame input;
  input.left_stick = {-1.0F, 1.0F};
  input.pressed = button::white;
  LessonStep step;
  step.walk_limit = 0.7F;
  step.turn_only = 1;
  const RobotControl control = battle_control(input, BattleButtons{}, &step);
  CHECK(std::fabs(control.move[1] - 0.7F) < 1e-6F);
  CHECK(control.move[0] == 0.0F);
  CHECK(control.dodge == -1);
}

TEST_CASE("a robot pushed off the stage's ground goes back to its last footing") {
  const auto floor = [](const Vec3 a, const Vec3 b, const Vec3 c) {
    return CollisionTriangle{{a, b, c}, {0, 1, 0}, 0};
  };
  Battle battle(BattleSettings{}, {});
  battle.set_stage(StageCollision({}, {floor({-20, 0, -20}, {20, 0, -20}, {20, 0, 20}),
                                       floor({-20, 0, -20}, {20, 0, 20}, {-20, 0, 20})}));
  battle.add_robot("player", 0, {0, 0, 0}, 0, {}, 100);
  for (int i = 0; i < 5; ++i) {
    battle.tick({}, {}, tick);
  }
  REQUIRE(battle.player().mover.grounded());
  // As if a push carried it past a wall into a building's empty footprint.
  battle.robot(0).mover.set_position({30, 0, 0});
  battle.tick({}, {}, tick);
  const Vec3 at = battle.player().mover.position();
  CHECK(std::abs(at.x) < 1.0F);
  CHECK(std::abs(at.y) < 0.01F);
  CHECK(battle.player().mover.grounded());
}

TEST_CASE("a dash on eligible legs leans into a turn without restarting its stride") {
  for (const int legs : {2, 0}) {
    RobotMover mover{MovementTuning{}};
    mover.set_timings(nullptr, legs);
    mover.place({}, 0.0F);
    RobotControl control;
    control.move = {0.0F, 1.0F};
    control.dash = true;
    mover.tick(control, tick);
    control.dash = false;
    REQUIRE(mover.mode() == MoveMode::dash);
    REQUIRE(mover.motion() == robot_motion::dash);
    control.move = {1.0F, 1.0F};
    for (int i = 0; i < 30; ++i) {
      mover.tick(control, tick);
    }
    CHECK(mover.mode() == MoveMode::dash);
    // Leg type 0's movement-sound family has no lean (0x0823cb, 0x08f5d8).
    CHECK(mover.motion() == (legs == 2 ? robot_motion::dash + 1 : robot_motion::dash));
    control.move = {-1.0F, 1.0F};
    for (int i = 0; i < 60; ++i) {
      mover.tick(control, tick);
    }
    CHECK(mover.motion() == (legs == 2 ? robot_motion::dash + 2 : robot_motion::dash));
  }
}
