// A record weapon's reach and locks (0x09d42f and the lock checks).

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/battle_weapons.hpp"
#include "phantomcrash/robot_parts.hpp"
#include "phantomcrash/weapon_reach.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) < 1e-2F;
}

WeaponStats stats(const int kind, const int flight) {
  WeaponStats out;
  out.hud_kind = kind;
  out.flight = flight;
  return out;
}

} // namespace

TEST_CASE("a straight round reaches as far as it flies in its life") {
  CHECK(near(straight_reach(3.7F, 3.7F, 0.0F, 30.0F), 111.0F)); // the pistol
  // A bazooka at its top speed after 16 of its 45 frames.
  CHECK(near(straight_reach(0.05F, 1.25F, 0.075F, 45.0F), 46.65F));
  // Still speeding up when its life ends.
  CHECK(near(straight_reach(0.0F, 10.0F, 0.1F, 20.0F), 20.0F));
  // Without acceleration it keeps its start speed, below its top.
  CHECK(near(straight_reach(1.0F, 1.75F, 0.0F, 100.0F), 100.0F));
}

TEST_CASE("a lobbed round's reach is the original's estimate about its blast") {
  CHECK(near(lobbed_reach(0.75F, 30.0F), 17.90F));
  CHECK(near(lobbed_reach(0.75F, -10.0F), lobbed_reach_fallback));
  WeaponStats sling = stats(hud_kind::grenade, weapon_flight_lobbed);
  sling.start_speed = sling.top_speed = 0.75F;
  sling.launch_degrees = 30.0F;
  sling.blast_radius = 20.0F;
  const WeaponReach reach = weapon_reach(sling, true);
  CHECK(near(reach.range, 17.90F + 20.0F + lobbed_reach_margin));
  CHECK(reach.min_range == 0.0F);
  sling.blast_radius = 5.0F;
  CHECK(near(weapon_reach(sling, true).min_range, 17.90F - 5.0F - lobbed_reach_margin));
}

TEST_CASE("a weapon locks by its mount and a melee weapon from nearer") {
  WeaponStats gun = stats(hud_kind::cannon, 0);
  gun.start_speed = gun.top_speed = 3.7F;
  gun.life_frames = 30.0F;
  const WeaponReach arm = weapon_reach(gun, false);
  CHECK(arm.lock_range == arm_lock_range);
  CHECK(arm.hold_degrees == 0.0F);
  CHECK(near(arm.range, 111.0F));
  const WeaponReach shoulder = weapon_reach(gun, true);
  CHECK(shoulder.lock_range == shoulder_lock_range);
  CHECK(shoulder.hold_degrees == lock_hold_degrees);

  const WeaponReach melee = weapon_reach(stats(hud_kind::melee, 0), false);
  CHECK(melee.range == melee_reach);
  CHECK(melee.lock_range == melee_lock_range);
  CHECK(melee.hold_degrees == lock_hold_degrees);
}

TEST_CASE("a beam runs out to its length and a stun makes a flinch") {
  WeaponStats laser = stats(hud_kind::beam, 0);
  laser.start_speed = 2.0F;
  laser.top_speed = 60.0F;
  laser.acceleration = 2.0F;
  laser.stun = 1.0F;
  laser.recharge_frames = 150.0F;
  WeaponTuning weapon;
  weapon.part = PartRef{WeaponTable::arm, 29, 0};
  apply_weapon_stats(weapon, laser);
  CHECK(near(weapon.range, 60.0F));
  CHECK(near(weapon.speed, 120.0F)); // 2 units a frame
  CHECK(weapon.acceleration == 0.0F);
  CHECK(weapon.lock_range == arm_lock_range);
  CHECK(near(weapon.stagger_seconds, 1.0F / 60.0F));
  CHECK(near(weapon.recharge_seconds, 5.0F)); // 150 battle ticks, as the shop shows
}
