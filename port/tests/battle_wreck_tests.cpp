#include "test_support.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/money.hpp"

using namespace phantomcrash;

namespace {
constexpr float tick = 1.0F / 60.0F;

StageCollision panel(float x, float low_z, float high_z) {
  CollisionTriangle first, second;
  first.points = {Vec3{x, 0, low_z}, Vec3{x, 4, low_z}, Vec3{x, 4, high_z}};
  second.points = {Vec3{x, 0, low_z}, Vec3{x, 4, high_z}, Vec3{x, 0, high_z}};
  first.normal = second.normal = {-1, 0, 0};
  return StageCollision({first, second}, {});
}

void add_wreck(Battle &battle, float x, float low_z, float high_z) {
  const auto index = battle.wrecks().items().size();
  battle.wrecks().add({});
  battle.wrecks().set_collision(index, panel(x, low_z, high_z));
}

BattleSettings settings(float blast_radius = 0) {
  BattleSettings result;
  auto &gun = result.weapons["gun"];
  gun.speed = 600;
  gun.cooldown = 0.05F;
  gun.blast_radius = blast_radius;
  return result;
}

int rewards(const Battle &battle) {
  int money = 0;
  for (const auto &event : battle.events()) {
    if (event.kind == BattleEventKind::wreck_destroyed) {
      CHECK(event.robot == 0);
      money += static_cast<int>(event.amount) * ny_per_stored_unit;
    }
  }
  return money;
}
} // namespace

TEST_CASE("a bullet destroys a corpse and pays ten thousand NY only once") {
  Battle battle(settings(), {});
  battle.add_robot("player", 0, {}, -pi / 2, {"gun", "", "", ""}, 1000);
  add_wreck(battle, 5, -4, 4);
  FireControl fire;
  fire.fire[0] = true;
  int money = 0;
  for (int frame = 0; frame < 30; ++frame) {
    battle.tick({}, fire, tick);
    money += rewards(battle);
  }
  CHECK(battle.wrecks().items()[0].removed);
  CHECK(money == 10000);
  CHECK(battle.player().kills == 0);
}

TEST_CASE("an explosion next to a corpse leaves it solid and pays nothing") {
  Battle battle(settings(12), {});
  battle.add_robot("player", 0, {}, -pi / 2, {"gun", "", "", ""}, 1000);
  battle.set_stage(panel(10, -20, 20));
  add_wreck(battle, 7, 5, 7); // off the projectile's path, inside its blast
  FireControl fire;
  fire.fire[0] = true;
  bool exploded = false;
  for (int frame = 0; frame < 12; ++frame) {
    battle.tick({}, frame == 0 ? fire : FireControl{}, tick);
    exploded = exploded || !battle.armory().explosions().empty();
    CHECK(rewards(battle) == 0);
  }
  CHECK(exploded);
  CHECK(battle.wrecks().items()[0].solid());
  CHECK(battle.player().kills == 0);
}

TEST_CASE("an explosive projectile pays for the corpse it strikes but not nearby corpses") {
  Battle battle(settings(12), {});
  battle.add_robot("player", 0, {}, -pi / 2, {"gun", "", "", ""}, 1000);
  add_wreck(battle, 5, -4, 4);
  add_wreck(battle, 7, 5, 7);
  FireControl fire;
  fire.fire[0] = true;
  int money = 0;
  for (int frame = 0; frame < 12; ++frame) {
    battle.tick({}, frame == 0 ? fire : FireControl{}, tick);
    money += rewards(battle);
  }
  CHECK(battle.wrecks().items()[0].removed);
  CHECK(battle.wrecks().items()[1].solid());
  CHECK(money == 10000);
  CHECK(battle.player().kills == 0);
}

TEST_CASE("the shipped corpse bounty is ten thousand NY") {
  const auto data = DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/training.json");
  const auto battle = parse_battle_settings(*data.find("battle"));
  CHECK(battle.robot_wrecks.reward_ny == 10000);
}
