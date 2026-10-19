// The Armory: rounds and spread, projectile lifetimes, record blasts and
// lobbed rounds.

#include "test_support.hpp"

#include <array>
#include <cmath>

#include "phantomcrash/battle_weapons.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 30.0F;

bool near(const float a, const float b, const float tolerance = 1e-3F) {
  return std::fabs(a - b) < tolerance;
}

// Runs the armory for `seconds`, collecting every hit.
std::vector<WeaponHit> run(Armory &armory, const std::vector<Target> &targets,
                           const float seconds) {
  const StageCollision open;
  std::vector<WeaponHit> hits;
  for (float t = 0.0F; t < seconds; t += tick) {
    armory.tick(tick, targets, open, hits);
  }
  return hits;
}

} // namespace

TEST_CASE("a pull fires the weapon's rounds, scattered within its spread") {
  WeaponTuning shotgun;
  shotgun.speed = 60.0F;
  shotgun.spread_degrees = 5.0F;
  Armory armory;
  armory.fire(shotgun, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, {}, 6);
  CHECK(armory.projectiles().size() == 6);
  const Vec3 first = normalize(armory.projectiles().front().velocity);
  CHECK(near(first.z, -1.0F)); // the first goes straight
  for (const Projectile &shot : armory.projectiles()) {
    const float off = std::acos(std::clamp(-normalize(shot.velocity).z, -1.0F, 1.0F));
    CHECK(off <= radians(5.0F) + 1e-4F);
    CHECK(near(length(shot.velocity), 60.0F));
  }
}

TEST_CASE("a projectile with a lifetime expires after it, however far it went") {
  WeaponTuning pistol;
  pistol.speed = 222.0F;
  pistol.range = 1000.0F;
  pistol.life_seconds = 0.5F;
  Armory armory;
  armory.fire(pistol, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, {});
  run(armory, {}, 0.4F);
  CHECK(armory.projectiles().size() == 1);
  run(armory, {}, 0.2F);
  CHECK(armory.projectiles().empty());
}

TEST_CASE("a record blast hits directly, then hurts every body in reach each tick") {
  WeaponTuning sling;
  sling.kind = WeaponKind::gun; // straight, to aim the direct hit
  sling.speed = 60.0F;
  sling.damage = 400.0F;
  sling.blast_radius = 20.0F;
  sling.blast_start_radius = 1.0F;
  sling.blast_seconds = 0.25F;
  sling.blast_damage = 120.0F;
  const std::vector<Target> targets{
      {Vec3{0.0F, 0.0F, -10.0F}, 2.0F, 1, true},  // in the line of fire
      {Vec3{12.0F, 0.0F, -10.0F}, 2.0F, 1, true}, // reached as the blast grows
      {Vec3{40.0F, 0.0F, -10.0F}, 2.0F, 1, true}, // out of reach
      {Vec3{0.0F, 0.0F, -14.0F}, 2.0F, 0, true},  // a team mate
  };
  Armory armory;
  armory.fire(sling, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, targets);
  const std::vector<WeaponHit> hits = run(armory, targets, 1.0F);
  std::array<float, 4> damage{};
  for (const WeaponHit &hit : hits) {
    damage[static_cast<std::size_t>(hit.victim)] += hit.damage;
  }
  CHECK(!hits.empty() && hits.front().directional && near(hits.front().damage, 400.0F));
  // The blast lasts 0.25 s: 8 ticks of 120 for the body it hit, fewer for
  // the one it reaches as it grows, none for the rest.
  CHECK(near(damage[0], 400.0F + 8.0F * 120.0F));
  CHECK(damage[1] >= 3.0F * 120.0F && damage[1] <= 5.0F * 120.0F);
  CHECK(damage[2] == 0.0F);
  CHECK(damage[3] == 0.0F);
  for (const WeaponHit &hit : hits) {
    if (hit.victim == 1) {
      CHECK(!hit.directional && hit.direction.x > 0.9F); // pushed away from the centre
    }
  }
  CHECK(armory.projectiles().empty());
}

TEST_CASE("a record's lobbed round leaves at its launch angle and falls") {
  WeaponTuning sling;
  sling.kind = WeaponKind::grenade;
  sling.speed = 45.0F;
  sling.launch_degrees = 30.0F;
  sling.gravity = 49.0F;
  sling.life_seconds = 1.25F;
  Armory armory;
  armory.fire(sling, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, {});
  const Vec3 velocity = armory.projectiles().front().velocity;
  CHECK(near(velocity.y, 45.0F * 0.5F));
  CHECK(near(velocity.z, -45.0F * std::cos(radians(30.0F))));
  run(armory, {}, 0.5F);
  CHECK(armory.projectiles().front().velocity.y < 0.0F); // past the top
}

TEST_CASE("a missile speeds up from its start speed by its acceleration") {
  WeaponTuning aspide;
  aspide.kind = WeaponKind::missile;
  aspide.speed = 69.0F;
  aspide.start_speed = 21.0F;
  aspide.acceleration = 54.0F;
  aspide.life_seconds = 2.0F;
  Armory armory;
  armory.fire(aspide, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, {});
  CHECK(near(length(armory.projectiles().front().velocity), 21.0F));
  run(armory, {}, 0.5F);
  CHECK(near(length(armory.projectiles().front().velocity), 21.0F + 54.0F * 0.5F, 2.0F));
  run(armory, {}, 1.0F);
  CHECK(near(length(armory.projectiles().front().velocity), 69.0F));
}

TEST_CASE("a landing says whether it struck a robot, which picks the impact case") {
  WeaponTuning gun;
  gun.speed = 120.0F;
  gun.surface_hit = shot_hit::burst;
  gun.robot_hit = shot_hit::removed;
  Armory armory;
  const std::vector<Target> targets{{Vec3{0.0F, 0.0F, -10.0F}, 1.0F, 1, true}};
  armory.fire(gun, 0, 0, Vec3{}, Vec3{0.0F, 0.0F, -1.0F}, -1, targets);
  run(armory, targets, 0.3F);
  const auto landings = armory.take_landings();
  REQUIRE(landings.size() == 1);
  CHECK(landings[0].on_robot);
  CHECK(landings[0].target == 0);
  CHECK(!starts_impact(gun, true)); // 0x098eda case 0: gone without one
  CHECK(starts_impact(gun, false)); // case 1 on a wall
  gun.robot_hit = shot_hit::explode_quiet;
  CHECK(!starts_impact(gun, true));
  gun.robot_hit = shot_hit::pierce;
  CHECK(starts_impact(gun, true));
}

TEST_CASE("attached impact ownership identifies the struck body for strikes and beams") {
  const std::vector<Target> targets{{{0, 0, 0}, 1, 0, true}, {{0, 0, -4}, 1, 1, true}};
  WeaponTuning blade;
  blade.kind = WeaponKind::melee;
  Armory strike;
  std::vector<WeaponHit> hits;
  CHECK(strike.strike(blade, 0, 0, {0, 0, -4}, targets, hits));
  auto landings = strike.take_landings();
  REQUIRE(landings.size() == 1);
  CHECK(landings[0].target == 1);
  WeaponTuning ray;
  ray.kind = WeaponKind::gun;
  ray.speed = 120;
  ray.range = 100;
  Armory beam;
  beam.hold_beam(ray, 0, 0, 0, {}, {0, 0, -1});
  run(beam, targets, .1F);
  landings = beam.take_landings();
  REQUIRE(!landings.empty());
  CHECK(landings[0].on_robot && landings[0].target == 1);
  Target scenery = targets[1];
  scenery.on_robot = false;
  Armory wall;
  wall.fire(ray, 0, 0, {}, {0, 0, -1}, -1, {scenery});
  run(wall, {scenery}, .1F);
  landings = wall.take_landings();
  REQUIRE(!landings.empty());
  CHECK(!landings[0].on_robot && landings[0].target == -1);
}
