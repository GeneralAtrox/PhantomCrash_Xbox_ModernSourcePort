// The rescue: a player's robot low on armour is brought a strong repair box
// by the next carrier, gliding down to land at its feet.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/supply_drop.hpp"

using namespace phantomcrash;

namespace {

// A straight route of four segments along +x at height 30, dropping on
// segment 2 (x 60..90). Regular pickups are ammo (kind 9).
SupplySettings test_settings() {
  SupplySettings settings;
  SupplyRoute route;
  for (int index = 0; index < 4; ++index) {
    const auto x = static_cast<float>(index * 30);
    route.segments.push_back({Vec3{x, 30.0F, 0.0F}, Vec3{x + 10.0F, 30.0F, 0.0F},
                              Vec3{x + 20.0F, 30.0F, 0.0F}, Vec3{x + 30.0F, 30.0F, 0.0F}});
  }
  route.closing = 1;
  route.drop = 2;
  route.leave = 3;
  settings.routes.push_back(route);
  for (SupplyKind &kind : settings.kinds) {
    kind.chance = 0.0F;
  }
  settings.kinds[9].chance = 1.0F;
  settings.rescue.armour_share = 0.25F;
  return settings;
}

float flat(const Vec3) {
  return 0.0F;
}

constexpr float tick = 1.0F / 30.0F;

float across(const Vec3 a, const Vec3 b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

// Brings the first carrier down and waits for the next to set out.
void next_carrier(SupplyDrop &drop) {
  for (int index = 0; index < 20; ++index) {
    drop.tick(tick, flat);
  }
  CHECK(drop.shoot_carrier());
  for (int index = 0; index < 200 && (drop.carrier().wrecked || drop.carrier().parked); ++index) {
    drop.tick(tick, flat);
  }
}

// Ticks until the first pickup has landed (or been taken).
void land_first(SupplyDrop &drop) {
  for (int index = 0; index < 1000 && drop.pickups()[0].state != SupplyState::landed; ++index) {
    drop.tick(tick, flat);
  }
  CHECK(drop.pickups()[0].state == SupplyState::landed);
}

} // namespace

TEST_CASE("a rescue box is the weakest repair giving at least half the armour") {
  SupplyDrop drop(test_settings(), 3);
  CHECK(drop.rescue_kind() == 6); // the 50% box
  SupplySettings strong = test_settings();
  strong.rescue.min_repair = 0.9F;
  CHECK(SupplyDrop(strong, 3).rescue_kind() == 6); // none that strong: the strongest
  SupplySettings none = test_settings();
  for (SupplyKind &kind : none.kinds) {
    kind.effect = SupplyEffect::money;
  }
  CHECK(SupplyDrop(none, 3).rescue_kind() == -1);
}

TEST_CASE("the next carrier brings a robot calling for rescue a repair box at its feet") {
  SupplyDrop drop(test_settings(), 5);
  const Vec3 robot{40.0F, 0.0F, 25.0F};
  drop.watch({Vec3{}, robot}, {1});
  next_carrier(drop);
  CHECK(drop.pickups()[0].kind == 6);
  CHECK(drop.pickups()[0].target == 1);
  CHECK(drop.pickups()[1].kind == 9); // the rest roll as usual
  drop.watch({Vec3{}, robot}, {});    // healed meanwhile: the box still comes
  land_first(drop);
  CHECK(across(drop.pickups()[0].position, robot) < 0.05F);
  // A robot that touches it takes the repair.
  const auto taken = drop.take(robot + Vec3{0.0F, 1.0F, 0.0F}, 2.0F);
  REQUIRE(taken);
  CHECK(taken->kind.effect == SupplyEffect::repair);
  CHECK(taken->kind.amount == 0.5F);
}

TEST_CASE("a rescue box stops as near as it can reach to a robot too far away") {
  SupplySettings settings = test_settings();
  settings.rescue.glide = 0.1F;
  SupplyDrop drop(settings, 5);
  const Vec3 far{75.0F, 0.0F, 200.0F};
  drop.watch({far}, {0});
  next_carrier(drop);
  for (int index = 0; index < 1000 && drop.pickups()[0].state == SupplyState::carried; ++index) {
    drop.tick(tick, flat);
  }
  const Vec3 released = drop.pickups()[0].position;
  land_first(drop);
  const Vec3 landed = drop.pickups()[0].position;
  // About 60 ticks of falling at 0.1 across a tick.
  CHECK(across(landed, released) > 5.0F);
  CHECK(across(landed, released) < 6.5F);
  CHECK(across(landed, far) < across(released, far) - 5.0F);
}

TEST_CASE("no rescue when the armour share is 0 or nobody calls") {
  SupplySettings off = test_settings();
  off.rescue.armour_share = 0.0F;
  SupplyDrop drop(off, 5);
  drop.watch({Vec3{40.0F, 0.0F, 25.0F}}, {0});
  next_carrier(drop);
  CHECK(drop.pickups()[0].kind == 9);
  CHECK(drop.pickups()[0].target == -1);

  SupplyDrop quiet(test_settings(), 5);
  quiet.watch({Vec3{40.0F, 0.0F, 25.0F}}, {});
  next_carrier(quiet);
  CHECK(quiet.pickups()[0].kind == 9);
}

TEST_CASE("the rescue parses, and the shipped one answers a quarter of the armour") {
  const auto settings = parse_supply_settings(
      json::parse(R"({"rescue": {"armour_share": 0.3, "min_repair": 0.25, "glide": 2}})"));
  CHECK(settings.rescue.armour_share == 0.3F);
  CHECK(settings.rescue.min_repair == 0.25F);
  CHECK(settings.rescue.glide == 2.0F);
  CHECK(SupplySettings{}.rescue.armour_share == 0.0F);

  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto training = roots.document("world/training.json");
  const auto shipped = parse_supply_settings(*training.find("battle")->find("supply"));
  CHECK(shipped.rescue.armour_share == 0.25F);
  CHECK(SupplyDrop(shipped, 1).rescue_kind() == 6);
}
