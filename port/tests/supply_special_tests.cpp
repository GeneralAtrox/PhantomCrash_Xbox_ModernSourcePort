// The red carrier: the streak of carriers shot down empty that brings it,
// its look, its load and odds, and its pickups landing on their spots.

#include "test_support.hpp"

#include <algorithm>
#include <cmath>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/supply_drop.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) < 0.01F;
}

// A straight route of four segments along +x at height 30, dropping on
// segment 2 (x 60..90). Regular pickups are kind 4; the red carrier's are
// kind 9 and fall onto three spots around x 75.
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
    kind.special_chance = 0.0F;
  }
  settings.kinds[4].chance = 1.0F;
  settings.kinds[9].special_chance = 1.0F;
  settings.special.streak = 3;
  settings.special.tint = {1.0F, 0.0F, 0.0F};
  settings.special.scale = 1.2F;
  settings.special.shot_bounty = 5000.0F;
  settings.special.spots = {{70.0F, -5.0F}, {75.0F, 0.0F}, {80.0F, 5.0F}};
  return settings;
}

float flat(const Vec3) {
  return 0.0F;
}

constexpr float tick = 1.0F / 30.0F;

// Brings the carrier down before its hatch opens and waits for the next to
// set out.
void shoot_down(SupplyDrop &drop) {
  for (int index = 0; index < 20; ++index) {
    drop.tick(tick, flat);
  }
  CHECK(drop.shoot_carrier());
  for (int index = 0; index < 200 && (drop.carrier().wrecked || drop.carrier().parked); ++index) {
    drop.tick(tick, flat);
  }
}

} // namespace

TEST_CASE("three carriers shot down before dropping anything bring the red carrier") {
  SupplyDrop drop(test_settings(), 7);
  CHECK(!drop.special());
  CHECK(drop.carrier().scale == 1.0F && !drop.carrier().tint);
  shoot_down(drop);
  shoot_down(drop);
  CHECK(drop.streak() == 2);
  CHECK(!drop.special());
  CHECK(!drop.take_special_arrival());
  shoot_down(drop);
  CHECK(drop.special());
  CHECK(drop.streak() == 0);
  CHECK(drop.take_special_arrival());
  CHECK(!drop.take_special_arrival()); // reported once
  CHECK(drop.carrier().scale == 1.2F);
  CHECK(drop.carrier().tint.has_value());
  CHECK(near(drop.carrier_radius(), 3.6F));
  CHECK(drop.shot_bounty() == 5000.0F);
  REQUIRE(drop.pickups().size() == 3);
  for (const SupplyPickup &pickup : drop.pickups()) {
    CHECK(pickup.kind == 9);
  }
  // Shot down too, it pays its own bounty and the next trip is a regular one.
  shoot_down(drop);
  CHECK(drop.streak() == 0);
  CHECK(!drop.special());
  CHECK(drop.pickups()[0].kind == 4);
  CHECK(drop.shot_bounty() == 1000.0F);
}

TEST_CASE("a carrier that drops anything, or flies its route, breaks the streak") {
  SupplyDrop drop(test_settings(), 7);
  shoot_down(drop);
  shoot_down(drop);
  CHECK(drop.streak() == 2);
  for (int index = 0; index < 400 && drop.pickups()[0].state == SupplyState::carried; ++index) {
    drop.tick(tick, flat);
  }
  CHECK(drop.shoot_carrier());
  CHECK(drop.streak() == 0);

  SupplyDrop flown(test_settings(), 7);
  shoot_down(flown);
  CHECK(flown.streak() == 1);
  for (int index = 0; index < 1000 && !flown.carrier().parked; ++index) {
    flown.tick(tick, flat);
  }
  CHECK(flown.carrier().parked);
  CHECK(flown.streak() == 0);
}

TEST_CASE("the red carrier's pickups land on their spots") {
  SupplyDrop drop(test_settings(), 7);
  for (int trip = 0; trip < 3; ++trip) {
    shoot_down(drop);
  }
  REQUIRE(drop.special());
  const auto all_landed = [&] {
    return std::ranges::all_of(drop.pickups(), [](const SupplyPickup &pickup) {
      return pickup.state == SupplyState::landed;
    });
  };
  for (int index = 0; index < 600 && !all_landed(); ++index) {
    drop.tick(tick, flat);
  }
  CHECK(all_landed());
  const auto &spots = drop.settings().special.spots;
  for (std::size_t index = 0; index < drop.pickups().size(); ++index) {
    const SupplyPickup &pickup = drop.pickups()[index];
    CHECK(pickup.spot == static_cast<int>(index));
    CHECK(near(pickup.position.x, spots[index][0]));
    CHECK(near(pickup.position.z, spots[index][1]));
    CHECK(near(pickup.position.y, 1.0F));
  }
}

TEST_CASE("the red carrier parses, and its odds fall back to the regular ones") {
  const auto settings = parse_supply_settings(json::parse(R"({
    "kinds": [{"effect": "money", "amount": 100, "chance": 0.5},
              {"effect": "money", "amount": 500, "chance": 0.5, "special_chance": 1}],
    "special": {"streak": 2, "tint": "#FF0000", "scale": 1.5, "drop_ticks": 6,
                "shot_bounty": 2000, "arrive_sound": "warning",
                "spots": [[1, 2], [3, 4]]}
  })"));
  CHECK(!settings.kinds[0].special_chance.has_value());
  CHECK(settings.kinds[1].special_chance == 1.0F);
  const SupplySpecial &special = settings.special;
  CHECK(special.streak == 2);
  CHECK((special.tint == SupplyColour{1.0F, 0.0F, 0.0F}));
  CHECK(special.scale == 1.5F && special.drop_ticks == 6.0F && special.shot_bounty == 2000.0F);
  CHECK(special.arrive_sound == "warning");
  REQUIRE(special.spots.size() == 2);
  CHECK(special.spots[1][0] == 3.0F && special.spots[1][1] == 4.0F);
  CHECK_THROWS(parse_supply_settings(
      json::parse(R"({"kinds": [{"effect": "money", "chance": 1, "special_chance": -1}]})")));
  // Without a special block it never comes.
  CHECK(SupplySettings{}.special.streak == 0);
}

TEST_CASE("the shipped red carrier drops nine pickups on the H with better odds") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto training = roots.document("world/training.json");
  const auto settings = parse_supply_settings(*training.find("battle")->find("supply"));
  const SupplySpecial &special = settings.special;
  CHECK(special.streak == 3);
  CHECK(special.scale == 1.2F);
  CHECK(special.spots.size() == 9);
  CHECK(special.arrive_sound == "warning");
  float total = 0.0F;
  for (const SupplyKind &kind : settings.kinds) {
    total += kind.special_chance.value_or(kind.chance);
  }
  CHECK(near(total, 1.0F));
  CHECK(settings.kinds[0].special_chance == 0.0F); // no 1,000 or 5,000 NY boxes
  CHECK(settings.kinds[1].special_chance == 0.0F);
  CHECK(near(*settings.kinds[12].special_chance, 0.025F)); // ten times the jackpot's odds
}
