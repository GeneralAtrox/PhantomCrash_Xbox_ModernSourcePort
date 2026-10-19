// The live RUMBLING: the opponents' fight among themselves, the larger
// rosters and the next opponent drawn, and the world that runs the areas'
// battles and lends one to the player.

#include "rumbling_fixture.hpp"
#include "test_support.hpp"

#include "phantomcrash/battle.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/rumbling_map.hpp"
#include "phantomcrash/training_battle.hpp"
#include "phantomcrash/battle_hud_style.hpp"

#include <algorithm>
#include <cmath>

using namespace phantomcrash;
using namespace phantomcrash::rumbling_fixture;

namespace {

EnemyPath square_path() {
  return parse_enemy_path(json::parse(R"({"segments": [
    {"kind": 0, "points": [[-80, 0, -80], [-40, 0, -40]], "next": [2]},
    {"kind": 0, "points": [[80, 0, 80], [40, 0, 40]], "next": [2]},
    {"kind": 1, "points": [[-40, 0, -40], [40, 0, -40], [40, 0, 40]], "next": [3]},
    {"kind": 1, "points": [[40, 0, 40], [-40, 0, 40], [-40, 0, -40]], "next": [2]}
  ]})"));
}

BattleSettings fighting_settings() {
  return parse_battle_settings(json::parse(R"({
    "arena": {"half_size": 100},
    "weapons": {"handgun": {"kind": "gun", "damage": 10, "cooldown": 0.1, "speed": 300}},
    "ai": {"fight": true, "engage_range": 150}
  })"));
}

bool near(const float a, const float b) {
  return std::abs(a - b) < 0.01F;
}

} // namespace

TEST_CASE("the live roster has live_size places and the next one is not already out") {
  Fixture fixture;
  fixture.rules.roster.live_size = 5;
  QuestSave quest;
  const AreaRoster roster = draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.0F));
  REQUIRE(roster.members.size() == 4); // five places, the player's among them
  const auto next =
      draw_replacement(fixture.check(), fixture.tables, quest, roster, 0, constant(0.0F));
  REQUIRE(next.has_value());
  for (std::size_t place = 1; place < roster.members.size(); ++place) {
    CHECK(!next->same(roster.members[place]));
  }
}

TEST_CASE("every opponent a replacement can bring in is known ahead, to be loaded") {
  Fixture fixture;
  fixture.rules.roster.live_size = 5;
  QuestSave quest;
  const AreaRoster roster = draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.0F));
  const std::vector<int> ahead =
      replacement_entrants(fixture.check(), fixture.tables, quest, roster);
  for (const RosterMember &member : roster.members) {
    CHECK(std::ranges::find(ahead, member.entrant) != ahead.end());
  }
  for (const float share : {0.0F, 0.3F, 0.6F, 0.99F}) {
    for (int leaving = 0; leaving < static_cast<int>(roster.members.size()); ++leaving) {
      const auto next = draw_replacement(fixture.check(), fixture.tables, quest, roster, leaving,
                                         constant(share));
      REQUIRE(next.has_value());
      CHECK(std::ranges::find(ahead, next->entrant) != ahead.end());
    }
  }
}

TEST_CASE("the live world runs the areas, lends one and pays for the player's kills") {
  Fixture fixture;
  fixture.rules.roster.live_size = 4;
  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  std::array<RumblingStage, roster_areas> stages{};
  stages[0].path = square_path();
  RumblingLive live;
  live.arrival_seconds = 1.0F;
  RumblingWorld world(fixture.check(), tables, fighting_settings(), stages, live, 7);
  QuestSave quest;
  world.update(quest, 0.5);
  CHECK(quest.live_epoch != 0);
  REQUIRE(quest.rosters[0].members.size() == 3);
  for (const RosterMember &member : quest.rosters[0].members) {
    CHECK(member.live.placed);
    CHECK(!member.live.down);
  }
  REQUIRE(world.robot(0, 1) != nullptr);
  CHECK(world.robot(0, 1)->entrant == quest.rosters[0].members[0].entrant);

  // A loaded game's opponents are taken up where they stood.
  QuestSave loaded = quest_from_json(quest_to_json(quest));
  CHECK(loaded.live_epoch == 0);
  RumblingWorld other(fixture.check(), tables, fighting_settings(), stages, live, 9);
  const std::shared_ptr<Battle> battle = other.lend(0, loaded);
  REQUIRE(battle != nullptr);
  CHECK(other.lent() == 0);
  const LiveRobot &stood = quest.rosters[0].members[0].live;
  const Vec3 at = battle->robots()[1].mover.position();
  CHECK(near(at.x, stood.position[0]));
  CHECK(near(at.z, stood.position[2]));

  // One the player shoots down is beaten once the player leaves.
  const RosterMember victim = loaded.rosters[0].members[0];
  battle->robot(1).alive = false;
  battle->robot(1).shot_by = 0;
  const int changes = other.changes(0);
  other.update(loaded, 0.2);
  CHECK(loaded.rosters[0].members[0].live.down);
  other.give_back(loaded);
  CHECK(other.lent() < 0);
  CHECK(std::ranges::find(loaded.beaten, beaten_key(victim.team, victim.place)) !=
        loaded.beaten.end());
  // Its place is filled once the arrival time is up.
  for (int frame = 0; frame < 90; ++frame) {
    other.update(loaded, tick);
  }
  CHECK(!loaded.rosters[0].members[0].live.down);
  CHECK(other.changes(0) > changes);
  CHECK(battle->robots()[1].alive);
  CHECK(battle->robots()[1].marker_colour == static_cast<int>(loaded.rosters[0].members[0].source));
}

TEST_CASE("live NPC nameplate colours follow saved roster status, not combat allegiance") {
  Fixture fixture;
  fixture.parts.pilots[0].tag = "Yuki";
  fixture.parts.pilots[1].tag = "Roy";
  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  QuestSave quest;
  refresh_rosters(fixture.check(), fixture.tables, quest, constant(0.0F));
  quest.rosters[0].members = {{8, RosterSource::champion, 0, 0},
                              {0, RosterSource::member, 0, 1},
                              {7, RosterSource::leader, 0, 0},
                              {4, RosterSource::extra, 2, 1},
                              {1, RosterSource::member, 0, 2}};
  quest = quest_from_json(quest_to_json(quest));
  std::array<RumblingStage, roster_areas> stages{};
  stages[0].path = square_path();
  RumblingWorld world(fixture.check(), tables, fighting_settings(), stages, {}, 7);
  const auto battle = world.lend(0, quest);
  REQUIRE(battle != nullptr);
  REQUIRE(battle->robots().size() == 6);
  const auto style = parse_battle_hud_style(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/battle_hud.json"));
  const std::array<std::uint32_t, 5> expected{0xff8080, 0xffff00, 0x8080ff, 0x00e100, 0xffff00};
  for (int index = 1; index <= 5; ++index) {
    const Combatant &robot = battle->robots()[static_cast<std::size_t>(index)];
    CHECK(robot.team == index); // all five still fight independently
    CHECK(style.team_colour(robot.marker_colour) == expected[static_cast<std::size_t>(index - 1)]);
    CHECK(!robot.tag.empty());
  }
  CHECK(battle->robots()[0].marker_colour == -1); // existing player/versus selection
}

TEST_CASE("returning a battle collects its final tick kill and cannot pay it twice") {
  Fixture fixture;
  fixture.rules.roster.live_size = 4;
  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  std::array<RumblingStage, roster_areas> stages{};
  stages[0].path = square_path();
  RumblingWorld world(fixture.check(), tables, fighting_settings(), stages, {}, 7);
  QuestSave quest;
  const auto battle = world.lend(0, quest);
  REQUIRE(battle != nullptr);
  REQUIRE(!quest.rosters[0].members.empty());
  const auto victim = quest.rosters[0].members[0];
  const auto prize =
      roster_card(fixture.check(), fixture.tables, quest, victim.entrant, true).bounty;
  battle->robot(1).alive = false;
  battle->robot(1).shot_by = 0;
  // No 30 Hz world update between the killing hit and leaving the match.
  CHECK(world.give_back(quest) == prize);
  CHECK(quest.money == prize);
  CHECK(world.give_back(quest) == 0);
  CHECK(quest.money == prize);
}

TEST_CASE("the map puts a stage point by its area's scale, offset and turn") {
  RumblingRobotMarkers markers;
  markers.origin = {234.0F, 73.0F};
  RumblingMapProjection flat{{232.0F, 167.0F}, {0.0F, 0.0F}, 0.0F, 2.0F};
  const auto at = rumbling_map_point(markers, flat, 10.0F, 5.0F);
  CHECK(near(at[0], 234.0F + 232.0F + 20.0F));
  CHECK(near(at[1], 73.0F + 167.0F - 10.0F)); // +z runs up the map
  CHECK(near(rumbling_map_degrees(flat, 0.0F), 0.0F));
  // A quarter turn about the pivot: the pivot itself stays put.
  RumblingMapProjection turned{{10.0F, 20.0F}, {10.0F, 20.0F}, pi * 0.5F, 1.0F};
  const auto pivot = rumbling_map_point(markers, turned, 0.0F, 0.0F);
  CHECK(near(pivot[0], 244.0F));
  CHECK(near(pivot[1], 93.0F));
  const auto east = rumbling_map_point(markers, turned, 1.0F, 0.0F);
  CHECK(near(east[0], 244.0F));
  CHECK(near(east[1], 92.0F));
}

TEST_CASE("the areas wait while the world is told not to fight, and go on after") {
  Fixture fixture;
  fixture.rules.roster.live_size = 4;
  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  std::array<RumblingStage, roster_areas> stages{};
  stages[0].path = square_path();
  RumblingWorld world(fixture.check(), tables, fighting_settings(), stages, RumblingLive{}, 7);
  QuestSave quest;
  world.update(quest, 0.5);
  REQUIRE(world.robot(0, 1) != nullptr);
  const auto position = [&] { return quest.rosters[0].members[0].live.position; };
  const auto before = position();
  world.set_fighting(false);
  world.update(quest, 2.0);
  CHECK(position() == before);
  world.set_fighting(true);
  world.update(quest, 2.0);
  CHECK(position() != before);
}

TEST_CASE("the shipped areas fight only on the area choice") {
  const RumblingLive live = parse_rumbling_live(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"));
  REQUIRE(live.fighting_screens.has_value());
  CHECK((*live.fighting_screens == std::vector<std::string>{"rumbling"}));
}
