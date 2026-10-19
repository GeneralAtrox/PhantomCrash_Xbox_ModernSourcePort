// The VS battle's rules (0x046f81): who stands, the round-end menu and the
// tallies; and the battle the VS mode's session leads to.

#include "test_support.hpp"

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/vs_battle.hpp"
#include "phantomcrash/vs_rules.hpp"

#include <vector>

using namespace phantomcrash;

namespace {

constexpr double frame = 1.0 / 60.0;

void run(VsRound &round, const std::vector<bool> &standing, const double seconds) {
  for (double at = 0.0; at < seconds; at += frame) {
    round.update(standing, {}, frame);
  }
}

VsMenuInput key(bool VsMenuInput::*button) {
  VsMenuInput input;
  input.*button = true;
  return input;
}

void press(VsRound &round, const std::vector<bool> &standing, VsMenuInput input) {
  round.update(standing, input, frame);
}

} // namespace

TEST_CASE("a VS robot stands while it has armour, a weapon on it and is not shot down") {
  WeaponTuning gun;
  Combatant robot{MovementTuning{}};
  robot.hp = 10.0F;
  CHECK(!vs_standing(robot)); // no weapon
  robot.weapons[0] = &gun;
  CHECK(vs_standing(robot));
  robot.lost[0] = true;
  CHECK(!vs_standing(robot)); // knocked off
  robot.lost[0] = false;
  robot.hp = 0.5F;
  CHECK(!vs_standing(robot));
  robot.hp = 10.0F;
  robot.alive = false;
  CHECK(!vs_standing(robot));
}

TEST_CASE("a VS round is decided below two standing and its menu opens after 120 frames") {
  VsRound round({}, 3);
  run(round, {true, true, true}, 1.0);
  CHECK(round.stage() == VsRoundStage::fight);
  run(round, {true, false, true}, 0.5);
  CHECK(round.stage() == VsRoundStage::fight);
  run(round, {true, true, false}, frame); // robot 1 stays out
  CHECK(round.stage() == VsRoundStage::settle);
  CHECK(round.outcome(0) == VsOutcome::win);
  CHECK(round.outcome(1) == VsOutcome::lose);
  CHECK(round.outcome(2) == VsOutcome::lose);
  run(round, {true, true, true}, 1.9);
  CHECK(round.stage() == VsRoundStage::settle);
  run(round, {true, true, true}, 0.2);
  CHECK(round.stage() == VsRoundStage::menu);
}

TEST_CASE("the VS menu's choice fades out and tallies, a draw tallying nothing") {
  const std::vector<bool> standing{false, true};
  VsRound round({}, 2);
  run(round, standing, 2.1);
  REQUIRE(round.stage() == VsRoundStage::menu);
  press(round, standing, key(&VsMenuInput::down));
  CHECK(round.cursor() == 1);
  CHECK(round.take_sounds() == std::vector<std::string>{"cursor"});
  press(round, standing, key(&VsMenuInput::confirm));
  CHECK(round.stage() == VsRoundStage::fade);
  CHECK(!round.choice());
  run(round, standing, 0.6);
  REQUIRE(round.choice() == VsChoice::select_area);
  std::array<int, controller_ports> wins{}, losses{};
  round.tally({2, 0}, wins, losses);
  round.tally({2, 0}, wins, losses); // once
  CHECK(wins[0] == 1);
  CHECK(losses[2] == 1);
  CHECK(wins[2] == 0);

  VsRound draw({}, 2);
  run(draw, {false, false}, 2.1);
  CHECK(draw.outcome(0) == VsOutcome::draw);
  press(draw, {false, false}, key(&VsMenuInput::confirm));
  run(draw, {false, false}, 0.6);
  std::array<int, controller_ports> none{}, none_lost{};
  draw.tally({0, 1}, none, none_lost);
  const std::array<int, controller_ports> zero{};
  CHECK(none == zero);
  CHECK(none_lost == zero);
}

TEST_CASE("EXIT asks first: cancel moves to EXIT, NO closes and YES leaves") {
  const std::vector<bool> standing{true, false};
  VsRound round({}, 2);
  run(round, standing, 2.1);
  press(round, standing, key(&VsMenuInput::cancel));
  CHECK(round.cursor() == vs_menu_exit);
  press(round, standing, key(&VsMenuInput::confirm));
  REQUIRE(round.stage() == VsRoundStage::confirm_exit);
  CHECK(round.answer() == vs_answer_no);
  press(round, standing, key(&VsMenuInput::confirm));
  CHECK(round.stage() == VsRoundStage::menu);
  press(round, standing, key(&VsMenuInput::confirm));
  press(round, standing, key(&VsMenuInput::left));
  CHECK(round.answer() == vs_answer_yes);
  press(round, standing, key(&VsMenuInput::cancel));
  CHECK(round.answer() == vs_answer_no);
  press(round, standing, key(&VsMenuInput::right));
  press(round, standing, key(&VsMenuInput::confirm));
  run(round, standing, 0.6);
  CHECK(round.choice() == VsChoice::exit);
}

TEST_CASE("the shipped VS battle names the area stages and the menu's routes") {
  const VsBattle battle = parse_vs_battle(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/vs_battle.json"));
  REQUIRE(battle.stages.size() == 8);
  CHECK(battle.stages[3] == "shibuya_night");
  CHECK(battle.stages[6] == "choco_day");
  CHECK(battle.routes[static_cast<std::size_t>(VsChoice::retry)] == "vs_battle");
  CHECK(battle.routes[static_cast<std::size_t>(VsChoice::exit)] == "main_menu");
  CHECK(battle.round.settle_frames == 120.0);
  CHECK(battle.overlay.cover.rect[2] == 488.0F);
  CHECK(battle.overlay.question.size() == 2);
}
