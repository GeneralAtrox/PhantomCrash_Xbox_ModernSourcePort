// Controller vibration: the damage pattern (0x07675e) and the effect player.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/rumble.hpp"

#include <cmath>

using namespace phantomcrash;

namespace {

bool near(const float a, const float b) {
  return std::fabs(a - b) <= 1e-4F;
}

RumbleTuning shipped() {
  return parse_rumble(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumble.json"));
}

} // namespace

TEST_CASE("rumble.json reads the original's three-step damage pattern") {
  const RumbleTuning tuning = shipped();
  REQUIRE(tuning.damage_steps.size() == 3);
  CHECK(near(tuning.damage_steps[0].seconds, 20.0F / 60.0F));
  CHECK(near(tuning.damage_steps[0].low, 65000.0F / 65535.0F));
  CHECK(near(tuning.damage_steps[2].high, 25000.0F / 65535.0F));
  CHECK(near(tuning.damage_floor, 300.0F) && near(tuning.damage_ceiling, 1000.0F));
  CHECK_THROWS(parse_rumble(json::parse(R"({"frame_rate": 60, "damage": {"steps": []}})")));
}

TEST_CASE("damage is clamped, weak hits are short and strength follows the damage") {
  const RumbleTuning tuning = shipped();
  const auto weak = damage_rumble(tuning, 20.0F); // clamped up to 300, under 500
  REQUIRE(weak.size() == 3);
  CHECK(near(weak[0].seconds, 10.0F / 60.0F));
  CHECK(near(weak[0].low, 0.3F * 65000.0F / 65535.0F));
  const auto strong = damage_rumble(tuning, 5000.0F); // clamped down to 1000
  CHECK(near(strong[1].seconds, 10.0F / 60.0F));
  CHECK(near(strong[1].high, 35000.0F / 65535.0F));
  CHECK(damage_rumble(tuning, 0.0F).empty());
}

TEST_CASE("the effect player walks each step, then falls still; stop silences it") {
  RumblePlayer player;
  player.start(1, {{0.1F, 1.0F, 0.5F}, {0.2F, 0.25F, 0.0F}});
  CHECK((player.motors(1) == RumbleMotors{1.0F, 0.5F}));
  CHECK((player.motors(0) == RumbleMotors{}));
  player.update(0.15F);
  CHECK((player.motors(1) == RumbleMotors{0.25F, 0.0F}));
  player.update(0.2F);
  CHECK((player.motors(1) == RumbleMotors{}));
  player.start(2, {{1.0F, 0.5F, 0.5F}});
  player.stop();
  CHECK((player.all_motors()[2] == RumbleMotors{}));
  player.start(9, {{1.0F, 0.5F, 0.5F}}); // no such port
}
