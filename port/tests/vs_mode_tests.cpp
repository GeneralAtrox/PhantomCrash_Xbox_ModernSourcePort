// VS MODE: the entry on the controller ports, the Scoobee select, the area
// select and the battle it leads to.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/vs_battle.hpp"
#include "phantomcrash/vs_mode.hpp"

#include <string>
#include <vector>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

VsModeScript shipped_script() {
  return parse_vs_mode(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/vs_mode.json"));
}

// The three presets' robots, 1081 to 1083.
GarageTables garage_tables() {
  GarageTables tables;
  tables.robots.resize(1084);
  for (int robot = 1081; robot <= 1083; ++robot) {
    tables.robots[static_cast<std::size_t>(robot)].name = "PLAIN-" + std::to_string(robot);
  }
  return tables;
}

VsScoobee scoobee(const std::string &name, const int body = 0) {
  VsScoobee kept;
  kept.name = name;
  kept.robot.name = name;
  kept.robot.parts[static_cast<std::size_t>(PartSlot::body)] = RobotPart{body, 0};
  return kept;
}

// `connected` ports with a controller; `port` pressing `buttons`.
InputFrame frame(const int connected, const int port = -1, const Buttons buttons = {}) {
  InputFrame input;
  input.controllers = 1;
  for (int at = 0; at < connected; ++at) {
    input.ports[static_cast<std::size_t>(at)].connected = true;
  }
  if (port >= 0) {
    input.ports[static_cast<std::size_t>(port)].held = buttons;
    input.ports[static_cast<std::size_t>(port)].pressed = buttons;
    input.held = buttons;
    input.pressed = buttons;
  }
  return input;
}

struct Fixture {
  PartTables parts;
  GarageTables garage = garage_tables();
  std::vector<VsScoobee> kept;
  VsSession session;
  int ports{2};

  Fixture() {
    parts.bodies.assign(2, ArmourRecord{});
    parts.bodies[1].maker = 3;
  }

  VsMode open(const VsModeEntry entry = {}) {
    VsMode logic(shipped_script(), parts, garage, kept, session, entry);
    settle(logic, 2.5);
    return logic;
  }

  void settle(VsMode &logic, const double seconds = 0.3) const {
    for (double time = 0.0; time < seconds; time += tick) {
      logic.update(tick, frame(ports));
    }
  }

  void tap(VsMode &logic, const int port, const Buttons buttons) const {
    logic.update(tick, frame(ports, port, buttons));
    settle(logic);
  }

  // Both players enter and answer YES.
  VsMode to_select() {
    VsMode logic = open();
    const VsModeScript &script = logic.script();
    tap(logic, 0, script.confirm_buttons);
    tap(logic, 1, script.confirm_buttons);
    tap(logic, 0, script.confirm_buttons);
    tap(logic, 0, script.confirm_buttons);
    settle(logic, 3.0);
    return logic;
  }
};

bool played(VsMode &logic, const std::string &sound) {
  for (const std::string &name : logic.take_sounds()) {
    if (name == sound) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST_CASE("vs_mode.json reads the players, the cards and the areas") {
  const VsModeScript script = shipped_script();
  CHECK(script.players[1].grow_from == ScaleAnchor::bottom_left);
  CHECK(script.players[3].hint_from_bottom == false);
  REQUIRE(script.cards.presets.size() == 3);
  CHECK(script.cards.presets[0].robot == 1082);
  CHECK(script.cards.makers.size() == 6);
  REQUIRE(script.area.items.size() == 6);
  CHECK(script.area.items[1].stages[1] == 3);
  CHECK(script.area.items.back().kind == VsAreaKind::exit);
  CHECK(script.area.crowd.first == 3);
  CHECK(script.helps.contains("area"));
  CHECK(script.battle_route == "vs_battle");
}

TEST_CASE("the entry needs two players; NO lets everyone go") {
  Fixture fixture;
  VsMode logic = fixture.open();
  const VsModeScript &script = logic.script();
  REQUIRE(logic.step() == VsStep::input);
  CHECK(logic.player(2).overlay.shown()); // No Controller
  CHECK(logic.player(0).content.shown());
  fixture.tap(logic, 0, script.confirm_buttons);
  CHECK(logic.player(0).joined);
  static_cast<void>(logic.take_sounds());
  fixture.tap(logic, 0, script.confirm_buttons);
  CHECK(logic.step() == VsStep::input);
  CHECK(played(logic, script.no_sound));
  fixture.tap(logic, 1, script.confirm_buttons);
  CHECK(logic.player(1).extra.shown()); // the next-step hint
  fixture.tap(logic, 1, script.confirm_buttons);
  REQUIRE(logic.step() == VsStep::question);
  fixture.tap(logic, 0, script.right);
  CHECK(logic.choice() == 1);
  fixture.tap(logic, 0, script.confirm_buttons);
  CHECK(logic.step() == VsStep::input);
  CHECK(logic.joined_count() == 0);
}

TEST_CASE("YES turns to the Scoobee select, where each player picks along a strip") {
  Fixture fixture;
  fixture.kept = {scoobee("ALPHA"), scoobee("BROKEN", 9)};
  VsMode logic = fixture.to_select();
  const VsModeScript &script = logic.script();
  REQUIRE(logic.phase() == VsPhase::select);
  REQUIRE(logic.step() == VsStep::input);
  CHECK(logic.count() == 5);
  CHECK(logic.name(0) == "PLAIN-1082");
  CHECK(logic.usable(3));
  CHECK(!logic.usable(4));
  fixture.tap(logic, 0, script.right);
  fixture.tap(logic, 0, script.right);
  CHECK(logic.player(0).row == 2);
  logic.update(tick, frame(2, 0, script.right));
  CHECK(logic.player(0).cards.scrolling());
  fixture.settle(logic);
  fixture.tap(logic, 0, script.right);
  CHECK(logic.player(0).chosen() == 4);
  static_cast<void>(logic.take_sounds());
  fixture.tap(logic, 0, script.confirm_buttons);
  CHECK(!logic.player(0).decided);
  CHECK(script.unusable_sound == "confirm");
  CHECK(played(logic, script.unusable_sound));
  fixture.tap(logic, 0, script.left);
  fixture.tap(logic, 0, script.confirm_buttons);
  CHECK(logic.player(0).decided);
  fixture.tap(logic, 0, script.cancel_buttons);
  CHECK(!logic.player(0).decided);
  fixture.tap(logic, 0, script.confirm_buttons);
  fixture.tap(logic, 1, script.confirm_buttons);
  REQUIRE(logic.step() == VsStep::question);
  fixture.settle(logic);
  fixture.tap(logic, 1, script.confirm_buttons);
  fixture.settle(logic, 1.0);
  CHECK(logic.phase() == VsPhase::area);
  CHECK(logic.items().size() == 6);
  CHECK(fixture.session.chosen[0] == 3);
}

TEST_CASE("cancel while choosing offers to give up the play data") {
  Fixture fixture;
  VsMode logic = fixture.to_select();
  const VsModeScript &script = logic.script();
  fixture.tap(logic, 1, script.cancel_buttons);
  CHECK(logic.message() == "abandon");
  CHECK(logic.choice() == 1);
  fixture.tap(logic, 1, script.confirm_buttons);
  CHECK(logic.step() == VsStep::input);
  fixture.tap(logic, 1, script.cancel_buttons);
  fixture.tap(logic, 1, script.left);
  fixture.tap(logic, 1, script.confirm_buttons);
  fixture.settle(logic, 1.0);
  CHECK(logic.phase() == VsPhase::entry);
  CHECK(logic.joined_count() == 0);
}

TEST_CASE("an area's time of day starts the battle there with the players' Scoobees") {
  Fixture fixture;
  VsMode logic = fixture.to_select();
  const VsModeScript &script = logic.script();
  fixture.tap(logic, 0, script.confirm_buttons);
  fixture.tap(logic, 1, script.confirm_buttons);
  fixture.settle(logic);
  fixture.tap(logic, 0, script.confirm_buttons);
  fixture.settle(logic, 1.5);
  REQUIRE(logic.phase() == VsPhase::area);
  fixture.tap(logic, 0, script.right);
  CHECK(logic.area_cursor() == 1);
  fixture.settle(logic, 0.5);
  fixture.tap(logic, 1, script.confirm_buttons);
  REQUIRE(logic.step() == VsStep::zone);
  fixture.tap(logic, 1, script.cancel_buttons);
  CHECK(logic.step() == VsStep::input);
  fixture.tap(logic, 1, script.confirm_buttons);
  fixture.tap(logic, 1, script.right);
  fixture.tap(logic, 1, script.confirm_buttons);
  fixture.settle(logic, 1.5);
  CHECK(logic.next() == "vs_battle");
  CHECK(fixture.session.stage == 3);
  REQUIRE(fixture.session.robots[0].has_value());
  CHECK(fixture.session.robots[0]->name == "PLAIN-1082");
  CHECK(!fixture.session.robots[2].has_value());
}

TEST_CASE("back from the battle a crowd of three chooses among the arena's items") {
  Fixture fixture;
  fixture.ports = 3;
  fixture.session.joined = {true, true, true, false};
  fixture.session.chosen = {0, 1, 2, 0};
  VsMode logic = fixture.open({true});
  CHECK(logic.phase() == VsPhase::area);
  REQUIRE(logic.items().size() == 3);
  CHECK(logic.items()[static_cast<std::size_t>(logic.area_cursor())] == 3);
  CHECK(logic.player(2).chosen() == 2);
  fixture.tap(logic, 2, logic.script().cancel_buttons);
  CHECK(logic.area_cursor() == 2);
}

TEST_CASE("cancel from a port that has not entered leaves the VS mode") {
  Fixture fixture;
  VsMode logic = fixture.open();
  fixture.tap(logic, 1, logic.script().cancel_buttons);
  CHECK(logic.step() == VsStep::exiting);
  fixture.settle(logic, 2.5);
  CHECK(logic.next() == "main_menu");
  CHECK(vs_lesson(VsBattle{}, 3).screens == 3);
}

TEST_CASE("the battle's SELECT SV opens the Scoobee select with the same players") {
  Fixture fixture;
  fixture.ports = 2;
  fixture.session.joined = {true, true, false, false};
  fixture.session.chosen = {0, 1, 0, 0};
  fixture.session.wins = {1, 0, 0, 0};
  fixture.session.resume = VsPhase::select;
  VsMode logic = fixture.open({true});
  CHECK(logic.phase() == VsPhase::select);
  CHECK(logic.player(1).joined);
  CHECK(fixture.session.resume == VsPhase::area);
  CHECK(fixture.session.wins[0] == 1); // the tallies stay
}
