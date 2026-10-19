// The first-person cockpit: its style, the gauges' ease, the cells a gauge
// picks, the groups' placement and slide, and the visor tint.

#include "test_support.hpp"

#include <cmath>

#include "phantomcrash/cockpit.hpp"
#include "phantomcrash/json.hpp"

using namespace phantomcrash;

namespace {

constexpr float frame = 1.0F / 60.0F;

CockpitStyle test_style() {
  return parse_cockpit_style(json::parse(R"({
    "groups": {"top": {"slide": 3, "bob": -0.5}, "lower": {"slide": -3, "bob": 0.2}},
    "cells": {"bar": {"offsets": [[0, 0], [36, 0], [72, 0], [108, 0]]},
              "scanner": {"offsets": [[0, 0], [100, 0]], "per_second": 30}},
    "pieces": [{"mesh": 20, "group": "top", "cells": "scanner"},
               {"mesh": 3, "group": "lower", "scale": "armour"},
               {"mesh": 19, "group": "lower", "cells": "bar", "cell_source": "speed"}]})"));
}

bool near(const float a, const float b) {
  return std::fabs(a - b) < 0.001F;
}

} // namespace

TEST_CASE("the cockpit's gauges ease 0.05 a frame and pick their cells") {
  Cockpit cockpit(test_style());
  CockpitInput input;
  input.speed_ratio = 0.6F;
  input.gravity = 24.5F;
  input.vertical_speed = -24.5F * 6.0F / 30.0F; // full scale falling
  cockpit.tick(input, frame);
  CHECK(near(cockpit.speed(), 0.05F));
  CHECK(near(cockpit.climb(), 0.05F));
  for (int index = 0; index < 30; ++index) {
    cockpit.tick(input, frame);
  }
  CHECK(near(cockpit.speed(), 0.6F));
  CHECK(near(cockpit.climb(), 1.0F));
  input.armour = 0.2F;
  const std::vector<CockpitDraw> draws = cockpit.draws(input, 0.0);
  CHECK(draws.size() == 3); // no visor out of camouflage
  CHECK(near(draws[1].scale_x, 0.2F));
  CHECK(draws[1].colour == 0xc04040); // below a quarter
  CHECK(draws[0].colour == 0x64ff6e);
  CHECK(near(draws[2].uv_offset[0], 72.0F / 512.0F)); // 0.6 of 4 cells
}

TEST_CASE("the cockpit's groups stand ahead of the eye and slide out of view") {
  Cockpit cockpit(test_style());
  CockpitInput input;
  input.slide = 0.5F;
  const std::vector<CockpitDraw> draws = cockpit.draws(input, 0.0);
  const Vec3 top = transform_point({}, draws[0].place);
  const Vec3 lower = transform_point({}, draws[1].place);
  CHECK(near(top.z, 8.0F) && near(top.y, -0.1F + 1.5F));
  CHECK(near(lower.y, -0.1F - 1.5F));
  input.slide = 0.0F;
  input.sway = 0.5F; // turns about its own origin
  const Vec3 side = transform_point({1.0F, 0.0F, 0.0F}, cockpit.draws(input, 0.0)[0].place);
  CHECK(near(side.x, std::cos(0.5F)) && near(side.z, 8.0F - std::sin(0.5F)));
}

TEST_CASE("camouflage fades the visor tint in over 60 ticks") {
  Cockpit cockpit(test_style());
  CockpitInput input;
  input.camouflaged = true;
  cockpit.tick(input, 1.0F);
  std::vector<CockpitDraw> draws = cockpit.draws(input, 0.0);
  CHECK(draws.size() == 4);
  CHECK(draws[0].additive && near(draws[0].alpha, 0.5F));
  CHECK(draws[0].colour == 0x1e0f64);
  cockpit.tick(input, 1.0F);
  CHECK(near(cockpit.draws(input, 0.0)[0].alpha, 1.0F));
}
