// A beam's charge (0x09ba08, 0x097785, 0x09772e): held, it drains a round a
// frame and does not recharge; let go, it recharges once faded out, and a
// new beam starts only on a full charge.

#include "test_support.hpp"

#include "phantomcrash/battle.hpp"
#include "phantomcrash/json.hpp"

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

// The L50/Javelin: 150 rounds, recharged over 150 of the original's ticks.
BattleSettings beam_settings() {
  return parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 200},
    "weapons": {
      "javelin": {"kind": "gun", "hud_kind": 5, "damage": 1, "cooldown": 0.0166666675,
                  "speed": 210, "range": 60, "ammo": 150, "recharge_seconds": 5}
    }
  })"));
}

struct Range {
  Battle battle{beam_settings(), EnemyPath{}};

  Range() {
    battle.add_robot("player", 0, {}, 0.0F, {"javelin", "", "", ""}, 1000.0F);
  }
  [[nodiscard]] const Magazine &magazine() const {
    return battle.player().magazines[0];
  }
  void run(const bool held, const int ticks) {
    FireControl fire;
    fire.fire[0] = held;
    for (int i = 0; i < ticks; ++i) {
      battle.tick({}, fire, tick);
    }
  }
  [[nodiscard]] bool beam_on() const {
    for (const Beam &beam : battle.armory().beams()) {
      if (beam.owner == 0 && beam.tip) {
        return true;
      }
    }
    return false;
  }
};

} // namespace

TEST_CASE("a held beam drains its charge without recharging and stops when empty") {
  Range range;
  range.run(true, 60);
  CHECK(range.magazine().ammo == 90); // a round a frame
  range.run(true, 92);
  CHECK(range.magazine().ammo == 0);
  CHECK(!range.beam_on());
  range.run(true, 4); // fading out: still no recharge
  CHECK(range.magazine().ammo == 0);
}

TEST_CASE("a released beam recharges and fires again only on a full charge") {
  Range range;
  range.run(true, 30);
  range.run(false, 60);
  CHECK(range.magazine().ammo > 120);
  CHECK(range.magazine().ammo < 150);
  range.run(true, 1);
  CHECK(!range.beam_on()); // not full yet
  range.run(false, 300);
  CHECK(range.magazine().ammo == 150);
  range.run(true, 2);
  CHECK(range.beam_on());
}

TEST_CASE("an emptied beam takes its recharge time to fill") {
  Range range;
  range.run(true, 160);
  CHECK(range.magazine().empty());
  range.run(false, 60 * 4);
  CHECK(range.magazine().ammo < 150);
  range.run(false, 60 * 2);
  CHECK(range.magazine().ammo == 150);
}

TEST_CASE("a beam's tip steps at the battle tick rate, so a close robot is hit at most 30 times a "
          "second") {
  Range range;
  // Facing the player, so each hit does the round's 1 point.
  range.battle.add_robot("target", 1, Vec3{0.0F, 0.0F, -4.0F}, pi, {"", "", "", ""}, 1000.0F);
  range.run(true, 60);
  const float lost = 1000.0F - range.battle.robots()[1].hp;
  CHECK(lost >= 29.0F);
  CHECK(lost <= 31.0F); // not 60: once a battle tick, not every frame
}
