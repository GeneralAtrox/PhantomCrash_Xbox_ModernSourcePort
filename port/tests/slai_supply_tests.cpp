#include "test_support.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/supply_drop.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/world_model.hpp"
#include <cmath>
#include <set>

using namespace phantomcrash;

TEST_CASE("all source arenas expose the crate model and all ten kind-indexed image banks") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto stages = parse_stages(roots.document("world/stages.json"));
  const std::array<std::string, supply_kind_count> names{
      "cy_100", "cy_1000", "cy_10000", "cy_100000", "ch_10",
      "ch_30",  "ch_50",   "cm_030",   "cm_050",    "cm_100"};
  int count = 0;
  for (const auto &[name, stage] : stages) {
    if (!name.starts_with("slai_")) {
      CHECK(!stage.supply_pickups.has_value());
      continue;
    }
    ++count;
    REQUIRE(stage.supply_pickups.has_value());
    const auto &look = *stage.supply_pickups;
    CHECK(look.scale == 2.5F);
    CHECK(look.carrier_model == "slai/assets/native_world/jet01.gltf");
    CHECK(roots.find_asset(look.carrier_model).has_value());
    REQUIRE(look.pulse.has_value());
    CHECK(look.pulse->frame_rate == 60);
    CHECK(look.pulse->colour_steps == 96);
    REQUIRE(stage.supply_routes.has_value());
    CHECK(stage.supply_routes->empty() == (name == "slai_motion"));
    CHECK(!look.carrier_light_model.empty() == name.starts_with("slai_night_"));
    if (!look.carrier_light_model.empty()) {
      CHECK(roots.find_asset(look.carrier_light_model).has_value());
    }
    CHECK(look.model == "slai/assets/native_world/JET_CONT.gltf");
    const auto model = load_model(roots.asset(look.model), look.model);
    CHECK(!model.vertices.empty());
    CHECK(!model.draws.empty());
    CHECK(model.textures.size() == 1); // the per-item image override owns texture zero
    for (std::size_t kind = 0; kind < names.size(); ++kind) {
      CHECK(look.images[kind] == "slai/assets/textures/" + names[kind] + ".png");
      CHECK(roots.find_asset(look.images[kind]).has_value());
    }
    CHECK(std::set<std::string>(look.images.begin(), look.images.end()).size() == 10);
  }
  CHECK(count == 15);
}

TEST_CASE(
    "source carrier rotation follows elapsed simulation time rather than presentation frames") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto stages = parse_stages(roots.document("world/stages.json"));
  const auto &stage = stages.at("slai_night_german");
  SupplySettings settings;
  settings.look.pickups = stage.supply_pickups;
  settings.routes = *stage.supply_routes;
  SupplyDrop a(settings, 1), b(settings, 1);
  const auto floor = [](Vec3) { return -1000.F; };
  for (int i = 0; i < 30; ++i) {
    a.tick(1.F / 30, floor);
  }
  for (int i = 0; i < 144; ++i) {
    b.tick(1.F / 144, floor);
  }
  CHECK(std::fabs(a.carrier().spin - 1.8F) < .0001F);
  CHECK(std::fabs(a.carrier().spin - b.carrier().spin) < .0001F);
}

TEST_CASE("pickup asset settings preserve supply quantities and require a complete kind table") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto training = roots.document("world/training.json");
  const auto base = parse_supply_settings(*training.find("battle")->find("supply"));
  CHECK(!base.look.pickups.has_value());
  auto overlaid = base;
  const auto stages = parse_stages(roots.document("world/stages.json"));
  overlaid.look.pickups = stages.at("slai_day_german").supply_pickups;
  REQUIRE(overlaid.look.pickups.has_value());
  for (std::size_t kind = 0; kind < base.kinds.size(); ++kind) {
    CHECK(overlaid.kinds[kind].effect == base.kinds[kind].effect);
    CHECK(overlaid.kinds[kind].amount == base.kinds[kind].amount);
  }
  CHECK(overlaid.speed == base.speed);
  CHECK(overlaid.drop_ticks == base.drop_ticks);
  CHECK_THROWS(
      parse_supply_pickup_look(json::parse(R"({"model":"crate.gltf","images":["one.png"]})")));
}
