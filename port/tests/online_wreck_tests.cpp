#include "test_support.hpp"
#include "phantomcrash/online_input.hpp"

using namespace phantomcrash;

namespace {
StageCollision wall() {
  CollisionTriangle triangle;
  triangle.points = {Vec3{0, -10, -10}, Vec3{0, 10, -10}, Vec3{0, 0, 10}};
  triangle.normal = {1, 0, 0};
  return StageCollision{{triangle}, {}};
}

bool blocks(const Battle &battle) {
  return battle.wrecks().segment_hit({5, 0, 0}, {-5, 0, 0}).has_value();
}
} // namespace

TEST_CASE("online wreck collision prepares once before the burn clock reaches solid") {
  Battle battle(BattleSettings{}, EnemyPath{});
  RobotWreck wreck;
  wreck.burn_left = 2 * online::tick_seconds;
  battle.wrecks().add(wreck);
  int prepared = 0;
  battle.wrecks().set_collision_builder([&](const RobotWreck &captured) {
    CHECK(captured.burn_left == 2 * online::tick_seconds);
    ++prepared;
    return wall();
  });
  CHECK(!blocks(battle));
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 1);
  CHECK(battle.wrecks().items()[0].collision_prepared);
  CHECK(!blocks(battle));
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 1);
  CHECK(blocks(battle));
  Vec3 position{0.2F, 0, 0};
  CHECK(battle.wrecks().push_sphere(position, 1) != 0);
  CHECK(position.x > 0.2F);
}

TEST_CASE("rollback reconstructs unprepared wrecks and restores destroyed collision") {
  Battle battle(BattleSettings{}, EnemyPath{});
  int prepared = 0;
  battle.wrecks().set_collision_builder([&](const RobotWreck &) {
    ++prepared;
    return wall();
  });
  const auto empty = battle.checkpoint();
  battle.wrecks().add(RobotWreck{});
  const auto unprepared = battle.checkpoint();
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 1);
  CHECK(blocks(battle));
  const auto solid = battle.checkpoint();
  CHECK(battle.wrecks().destroy(0));
  CHECK(!blocks(battle));
  const auto generation = battle.restore_generation();
  battle.restore_checkpoint(*solid);
  CHECK(battle.restore_generation() == generation + 1);
  CHECK(blocks(battle));
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 1);
  battle.restore_checkpoint(*unprepared);
  CHECK(battle.restore_generation() == generation + 2);
  CHECK(!blocks(battle));
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 2);
  CHECK(blocks(battle));
  battle.restore_checkpoint(*empty);
  CHECK(battle.wrecks().items().empty());
  battle.wrecks().add(RobotWreck{});
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 3);
  CHECK(blocks(battle));
}

TEST_CASE("empty wreck geometry is prepared once and offline collision stays explicit") {
  Battle battle(BattleSettings{}, EnemyPath{});
  battle.wrecks().add(RobotWreck{});
  battle.tick({}, {}, online::tick_seconds);
  CHECK(!battle.wrecks().items()[0].collision_prepared);
  CHECK(!battle.wrecks().simulation_collision());
  int prepared = 0;
  battle.wrecks().set_collision_builder([&](const RobotWreck &) {
    ++prepared;
    return StageCollision{};
  });
  battle.tick({}, {}, online::tick_seconds);
  battle.tick({}, {}, online::tick_seconds);
  CHECK(prepared == 1);
  CHECK(!blocks(battle));
  CHECK(!battle.wrecks().destroy(0));
}

TEST_CASE("checkpoints share the stage and wrecks until a wreck changes") {
  // Online rollback copies the battle several times a tick; copying every
  // wreck's collision made each kill slow the match further.
  Battle battle(BattleSettings{}, EnemyPath{});
  battle.set_stage(wall());
  RobotWreck wreck;
  wreck.burn_left = online::tick_seconds;
  battle.wrecks().add(wreck);
  battle.wrecks().set_collision(0, wall());
  const auto burning = battle.checkpoint();
  CHECK(&burning->stage().walls() == &battle.stage().walls());
  CHECK(&burning->wrecks().items()[0] == &battle.wrecks().items()[0]);

  battle.tick({}, {}, online::tick_seconds);
  const auto &now = battle.wrecks().items()[0];
  CHECK(&burning->wrecks().items()[0] != &now);
  CHECK(burning->wrecks().items()[0].burn_left == online::tick_seconds);
  CHECK(now.burn_left == 0.0F);
  CHECK(&burning->wrecks().items()[0].collision->walls() == &now.collision->walls());

  const auto burnt = battle.checkpoint();
  battle.tick({}, {}, online::tick_seconds);
  CHECK(&burnt->wrecks().items()[0] == &battle.wrecks().items()[0]);
  battle.restore_checkpoint(*burning);
  CHECK(battle.wrecks().items()[0].burn_left == online::tick_seconds);
  CHECK(battle.wrecks().destroy(0) == false);
}
