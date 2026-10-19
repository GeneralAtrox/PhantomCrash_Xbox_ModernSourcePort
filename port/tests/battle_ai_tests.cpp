// The original opponent AI's perception (0x05b9c1 / 0x04f6a0) and its jump
// at a target off the sight (0x05949b).

#include "test_support.hpp"

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json.hpp"

#include <algorithm>

using namespace phantomcrash;

namespace {

constexpr float tick = 1.0F / 60.0F;

OriginalAiData shipped_ai() {
  return *parse_original_ai(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/robot_ai.json"));
}

BattleSettings ai_settings(OriginalAiData data) {
  BattleSettings settings =
      parse_battle_settings(json::parse(R"({"arena": {"half_size": 100, "blocks": []}})"));
  settings.ai.fight = true;
  settings.ai.original = std::make_shared<const OriginalAiData>(std::move(data));
  return settings;
}

// A few of the AI's 30 Hz ticks.
void settle(Battle &battle) {
  for (int i = 0; i < 10; ++i) {
    battle.tick({}, {}, tick);
  }
}

EnemyPath no_path() {
  return parse_enemy_path(json::parse(R"({"segments": []})"));
}

} // namespace

TEST_CASE("a computer opponent takes a computer rival only once it has locked it") {
  Battle battle(ai_settings(shipped_ai()), no_path());
  // The player's robot comes first; straight ahead of the drone, far off.
  const int player = battle.add_robot("player", 0, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  // A computer rival, nearer and in view, but outside the drone's tiny
  // inner box, so never locked (0x04f6a0).
  const int rival = battle.add_robot("rival", 2, {3.5F, 0.0F, -20.0F}, 0.0F, {}, 100.0F);
  battle.robot(drone).sight.inner_half = {1.0F, 1.0F};
  battle.robot(rival).sight.inner_half = {1.0F, 1.0F};
  battle.robot(rival).frozen = true; // stands still, uncloaked
  settle(battle);
  CHECK(battle.robot(drone).ai.target == player);

  // With a wide inner box the nearer computer rival is locked and wins.
  Battle wide(ai_settings(shipped_ai()), no_path());
  wide.add_robot("player", 0, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
  wide.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  wide.add_robot("rival", 2, {3.5F, 0.0F, -20.0F}, 0.0F, {}, 100.0F);
  wide.robot(drone).sight.inner_half = {320.0F, 240.0F};
  wide.robot(rival).frozen = true;
  settle(wide);
  CHECK(wide.robot(drone).ai.target == rival);
}

TEST_CASE("a computer opponent jumps at a target off its sight, then aims and goes back") {
  OriginalAiData data = shipped_ai();
  // Every tick, and on any floor (the test arena's ground is the high ground).
  data.parameters["jump_reaction_chance"] = data.value("probability_scale");
  data.parameters["high_ground_outside_floor"] = 100.0F;
  data.parameters["high_ground_inside_floor"] = 100.0F;
  Battle battle(ai_settings(data), no_path());
  const int player = battle.add_robot("player", 0, {0.0F, 0.0F, -10.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  // The best pilot remembers its target through the jump and the turn.
  battle.robot(drone).ai.ability = 99;
  settle(battle);
  REQUIRE(battle.robot(drone).ai.target == player);
  CHECK(battle.robot(drone).ai.jump_phase == 0); // straight ahead: no jump

  // The target steps beside the drone, 10 units off and 90 degrees round.
  battle.robot(player).mover.set_position({10.0F, 0.0F, 0.0F});
  bool airborne = false;
  bool aimed = false;
  bool charging = false;
  for (int i = 0; i < 60 * 8; ++i) {
    battle.tick({}, {}, tick);
    airborne |= !battle.robot(drone).mover.grounded();
    aimed |= battle.robot(drone).ai.jump_phase == 3;
    // 0x059921 never resets the forward stick: it walks on while it aims.
    charging |=
        battle.robot(drone).ai.jump_phase == 3 && battle.robot(drone).ai.control.move[1] > 0;
    if (aimed && battle.robot(drone).ai.jump_phase == 0) {
      break;
    }
  }
  CHECK(airborne);
  CHECK(aimed);
  CHECK(charging);
  CHECK(battle.robot(drone).ai.jump_phase == 0);
  CHECK(battle.robot(drone).ai.mode == AiMode::route);
}

TEST_CASE("the jump at a target is not made on area 1's high ground unless allowed") {
  for (const bool any_floor : {false, true}) {
    OriginalAiData data = shipped_ai();
    data.parameters["jump_reaction_chance"] = data.value("probability_scale");
    BattleSettings settings = ai_settings(data);
    settings.ai.jump_any_floor = any_floor;
    Battle battle(settings, no_path());
    battle.add_robot("player", 0, {0.0F, 0.0F, -10.0F}, 0.0F, {}, 100.0F);
    const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
    settle(battle);
    battle.robot(0).mover.set_position({10.0F, 0.0F, 0.0F});
    bool jumped = false;
    for (int i = 0; i < 60; ++i) {
      battle.tick({}, {}, tick);
      jumped |= battle.robot(drone).ai.jump_phase != 0;
    }
    CHECK(jumped == any_floor);
  }
}

TEST_CASE("a weak pilot forgets its target in the jump unless jump_keeps_target") {
  for (const bool keep : {false, true}) {
    OriginalAiData data = shipped_ai();
    data.parameters["jump_reaction_chance"] = data.value("probability_scale");
    BattleSettings settings = ai_settings(data);
    settings.ai.jump_any_floor = true;
    settings.ai.jump_keeps_target = keep;
    Battle battle(settings, no_path());
    const int player = battle.add_robot("player", 0, {0.0F, 0.0F, -10.0F}, 0.0F, {}, 100.0F);
    const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F); // ability 0
    settle(battle);
    battle.robot(player).mover.set_position({10.0F, 0.0F, 0.0F});
    bool aimed = false;
    for (int i = 0; i < 60 * 3 && !aimed; ++i) {
      battle.tick({}, {}, tick);
      aimed = battle.robot(drone).ai.jump_phase == 3;
    }
    // 0.68 s of memory runs out in the air and 0x05981c goes back to the route.
    CHECK(aimed == keep);
  }
}

TEST_CASE("a leader holding no target turns on the robot that just hurt it") {
  Battle battle(ai_settings(shipped_ai()), no_path());
  // The player's robot behind the drone, out of its sight.
  const int player = battle.add_robot("player", 0, {0.0F, 0.0F, 30.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  battle.robot(drone).marker_colour = 1; // a leader: class 1 (0x05ec7d) always retaliates
  settle(battle);
  REQUIRE(battle.robot(drone).ai.target == -1);
  battle.robot(drone).damage_from = {10.0F, 0.0F};
  settle(battle);
  CHECK(battle.robot(drone).ai.target == player);
  CHECK(battle.robot(drone).ai.mode == AiMode::engage);
  // 2 s + ability x 0.1 s, counting down: well past a sighting's 0.68 s.
  CHECK(battle.robot(drone).ai.memory > 1.5F);
}

TEST_CASE("holding a target, a robot that locks no one takes the last rival it sees") {
  for (const bool nearest : {false, true}) {
    BattleSettings settings = ai_settings(shipped_ai());
    settings.ai.nearest_fallback_target = nearest;
    Battle battle(settings, no_path());
    battle.add_robot("player", 0, {0.0F, 0.0F, 99.0F}, 0.0F, {}, 100.0F); // behind, unseen
    const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
    const int near = battle.add_robot("near", 2, {0.0F, 0.0F, -20.0F}, 0.0F, {}, 100.0F);
    const int far = battle.add_robot("far", 3, {2.0F, 0.0F, -30.0F}, 0.0F, {}, 100.0F);
    battle.robot(near).frozen = true;
    battle.robot(far).frozen = true;
    // A box too small to lock anything: 0x05b9c1's second pass decides.
    battle.robot(drone).sight.inner_half = {0.0F, 0.0F};
    settle(battle);
    // Taken first with no target held: the nearest; then the last seen.
    CHECK(battle.robot(drone).ai.target == (nearest ? near : far));
  }
}

TEST_CASE("the attract demo's and RESULT's opponents act at the showcase ability") {
  BattleSettings settings = ai_settings(shipped_ai());
  settings.ai.showcase = true;
  Battle battle(settings, no_path());
  battle.add_robot("player", 0, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  settle(battle);
  CHECK(battle.robot(drone).ai.ability == 99);
}

TEST_CASE("with rivals_by_sight a computer rival is taken on sight, as a player's robot is") {
  BattleSettings settings = ai_settings(shipped_ai());
  settings.ai.rivals_by_sight = true;
  Battle battle(settings, no_path());
  battle.add_robot("player", 0, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  const int rival = battle.add_robot("rival", 2, {3.5F, 0.0F, -20.0F}, 0.0F, {}, 100.0F);
  battle.robot(drone).sight.inner_half = {1.0F, 1.0F}; // locks nothing
  battle.robot(rival).frozen = true;
  settle(battle);
  CHECK(battle.robot(drone).ai.target == rival);
}

TEST_CASE("a provoked ranker fights as any robot does for eight seconds, then chooses again") {
  Battle battle(ai_settings(shipped_ai()), no_path());
  const int player = battle.add_robot("player", 0, {0.0F, 0.0F, 30.0F}, 0.0F, {}, 100.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
  battle.robot(player).frozen = true;
  battle.robot(drone).ai.entrant = 0; // a ranker: class 2 always retaliates
  settle(battle);
  REQUIRE(battle.robot(drone).ai.target == -1);
  battle.robot(drone).damage_from = {10.0F, 0.0F};
  settle(battle);
  CHECK(battle.robot(drone).ai.target == player);
  CHECK(battle.robot(drone).ai.ranker_provoked);
  CHECK(battle.robot(drone).ai.ranker_left > 7.0F); // 0x05d2bd: +0x132 = 8 s
  for (int i = 0; i < 60 * 9; ++i) {
    battle.tick({}, {}, tick);
  }
  CHECK(!battle.robot(drone).ai.ranker_provoked);
}

TEST_CASE("cached settings are derived at ability 0 unless assigned_ability_settings is set") {
  for (const bool assigned : {false, true}) {
    BattleSettings settings = ai_settings(shipped_ai());
    settings.ai.assigned_ability_settings = assigned;
    Battle battle(settings, no_path());
    battle.add_robot("player", 0, {0.0F, 0.0F, -40.0F}, 0.0F, {}, 100.0F);
    const int drone = battle.add_robot("drone", 1, {}, 0.0F, {}, 100.0F);
    battle.robot(drone).ai.ability = 60;
    settle(battle);
    CHECK(battle.robot(drone).ai.ability == 60);
    CHECK(battle.robot(drone).ai.setup_ability == (assigned ? 60 : 0));
  }
}

namespace {
BattleSettings armed_settings() {
  BattleSettings settings = parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100, "blocks": []},
    "weapons": {
      "gun": {"kind": "gun", "hud_kind": 2, "aim_mode": 0, "damage": 0, "cooldown": 0.05,
              "speed": 300, "range": 100},
      "rocket": {"kind": "missile", "aim_mode": 2, "damage": 0, "cooldown": 1, "speed": 60,
                 "range": 100}
    }
  })"));
  settings.ai.fight = true;
  settings.ai.original = std::make_shared<const OriginalAiData>(shipped_ai());
  return settings;
}
} // namespace

TEST_CASE("an opponent's pulled trigger stays held, and a gun needs no lock") {
  Battle battle(armed_settings(), no_path());
  battle.add_robot("player", 0, {0.0F, 0.0F, -20.0F}, 0.0F, {}, 1000.0F);
  const int drone = battle.add_robot("drone", 1, {}, 0.0F, {"gun", "rocket", "", ""}, 100.0F);
  battle.robot(0).frozen = true;
  battle.robot(drone).drone = true; // fires (fire_drones)
  battle.robot(drone).sight.inner_half = {0.0F, 0.0F}; // locks nothing
  int held = 0, longest = 0;
  bool rocket = false;
  for (int i = 0; i < 60 * 8; ++i) {
    battle.tick({}, {}, tick);
    const auto &ai = battle.robot(drone).ai;
    held = ai.fire[0] ? held + 1 : 0;
    longest = std::max(longest, held);
    rocket = rocket || ai.fire[1];
    CHECK(battle.robot(drone).locks[0] == -1);
  }
  // The hold is 0.889 s x (1 + 0.2 U) of 30 Hz ticks at ability 0.
  CHECK(longest >= 50);
  CHECK(battle.robot(drone).ai.attacks > 10);
  // A lock-on weapon (aim mode 2) is never pulled unlocked.
  CHECK(!rocket);
}
