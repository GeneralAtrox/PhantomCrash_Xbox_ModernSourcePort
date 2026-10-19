#include "rumbling_fixture.hpp"
#include "test_support.hpp"
#include "phantomcrash/rumbling_world.hpp"
#include "phantomcrash/robot_wrecks.hpp"

#include <algorithm>
#include <cmath>

using namespace phantomcrash;
using namespace phantomcrash::rumbling_fixture;

TEST_CASE("a finite checklist unlocks its boss and keeps skulls across respawns and saves") {
  Fixture fixture;
  fixture.rules.roster.live_size = 4;
  fixture.rules.roster.champion_classes = {0};
  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  RumblingLive live;
  live.arrival_seconds = 0;
  RumblingWorld world(fixture.check(), tables, {}, {}, live, 7);
  world.set_background(false);
  QuestSave quest;
  const auto battle = world.lend(0, quest);
  REQUIRE(battle != nullptr);
  const auto roster = contest_opponents(fixture.check(), fixture.tables, 0);
  std::vector<int> killed;
  bool boss_seen = false;
  for (int turn = 0; turn < 30 && !boss_seen; ++turn) {
    for (int actor = 1; actor < static_cast<int>(battle->robots().size()); ++actor) {
      auto &robot = battle->robot(actor);
      if (!robot.alive) {
        continue;
      }
      const auto &member = quest.rosters[0].members[actor - 1];
      if (robot.boss) {
        CHECK(killed.size() == roster.size());
        CHECK(member.source == RosterSource::champion);
        boss_seen = true;
      } else {
        if (std::ranges::find(killed, member.entrant) == killed.end()) {
          killed.push_back(member.entrant);
        }
      }
      robot.alive = false;
      robot.hp = 0;
      robot.shot_by = 0;
    }
    world.update(quest, 1.0 / 30.0);
  }
  REQUIRE(boss_seen);
  CHECK(world.player_won());
  const auto status = world.roster_status(0);
  CHECK(status.size() == roster.size() + 1);
  CHECK(status.back().source == RosterSource::champion);
  CHECK(std::ranges::all_of(status, &RosterStatus::eliminated));
  const QuestSave saved = quest_from_json(quest_to_json(quest));
  CHECK(saved.rosters == quest.rosters);
  for (int frame = 0; frame < 120; ++frame) {
    world.update(quest, 1.0 / 30.0);
  }
  CHECK(std::ranges::any_of(battle->robots(), [](const auto &robot) { return robot.alive; }));
  RumblingWorld loaded(fixture.check(), tables, {}, {}, live, 11);
  QuestSave restored = saved;
  const auto restored_battle = loaded.lend(0, restored);
  REQUIRE(restored_battle != nullptr);
  loaded.update(restored, 1.0 / 30.0);
  CHECK(std::ranges::all_of(loaded.roster_status(0), &RosterStatus::eliminated));
  CHECK(std::ranges::any_of(restored_battle->robots(),
                            [](const auto &robot) { return robot.alive; }));
}

TEST_CASE("leaders wait for eligible teammates and NPC kills do not mark the player checklist") {
  Fixture fixture;
  fixture.rules.roster.live_size = 4;
  fixture.rules.roster.champion_classes = {0};
  AreaRoster roster = draw_roster(fixture.check(), fixture.tables, {}, 0, constant(0));
  const auto opponents = contest_opponents(fixture.check(), fixture.tables, 0);
  for (const auto &opponent : opponents) {
    if (opponent.entrant != 0 && opponent.entrant != 7) {
      roster.eliminated.push_back(opponent.entrant);
    }
  }
  roster.members = {{0, RosterSource::member, 0, 1}};
  const auto repeat =
      draw_replacement(fixture.check(), fixture.tables, {}, roster, -1, constant(0));
  REQUIRE(repeat.has_value());
  CHECK(repeat->entrant != 7);
  roster.eliminated.push_back(0);
  roster.members[0].live.down = true;
  const auto leader = draw_replacement(fixture.check(), fixture.tables, {}, roster, 0, constant(0));
  REQUIRE(leader.has_value());
  CHECK(leader->entrant == 7);

  const TestDriveScript drive;
  const std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{fixture.parts, fixture.garage, fixture.tables,
                              fixture.rules, drive,          templates};
  RumblingLive live;
  live.arrival_seconds = 0;
  RumblingWorld world(fixture.check(), tables, {}, {}, live, 7);
  QuestSave quest;
  const auto battle = world.lend(0, quest);
  const int victim = quest.rosters[0].members[0].entrant;
  battle->robot(1).alive = false;
  battle->robot(1).hp = 0;
  battle->robot(1).shot_by = 2;
  world.update(quest, 1.0 / 30.0);
  CHECK(std::ranges::find(quest.rosters[0].eliminated, victim) ==
        quest.rosters[0].eliminated.end());
  CHECK(world.bounty_won() == 0);
  CHECK(world.give_back(quest) == 0);
}

TEST_CASE("wreck broad phase preserves detailed hits, pushes and ledge support") {
  const StageCollision geometry = parse_stage_collision(json::parse(R"({"walls": [
    {"points": [[-2, 1, -2], [-2, 1, 2], [2, 1, -2]]},
    {"points": [[2, 1, -2], [-2, 1, 2], [2, 1, 2]]}
  ], "floor": []})"));
  RobotWrecks wrecks;
  wrecks.add({});
  wrecks.set_collision(0, geometry);
  for (const Vec3 at : {Vec3{0, 2, 0}, Vec3{2.2F, 2, 0}, Vec3{0, 100, 0}, Vec3{100, 2, 0}}) {
    CHECK(wrecks.ground_height(at, 1, 1) == geometry.ground_height(at, 1, 1));
    Vec3 pushed = at, expected = at;
    CHECK(wrecks.push_sphere(pushed, 1) == geometry.push_sphere(expected, 1));
    CHECK(length(pushed - expected) < 0.0001F);
    const Vec3 to{at.x, -2, at.z};
    const auto hit = wrecks.segment_hit(at, to), reference = geometry.segment_hit(at, to);
    CHECK(hit.has_value() == reference.has_value());
    if (hit && reference) {
      CHECK(length(*hit - *reference) < 0.0001F);
    }
  }
}

TEST_CASE("the champion enters after first defeats even while defeated opponents are fighting") {
  Fixture fixture;
  fixture.rules.roster.champion_classes = {0};
  AreaRoster roster;
  roster.drawn_for = 0;
  roster.members = {{0, RosterSource::member, 0, 1}, {1, RosterSource::member, 0, 2}};
  for (const auto &member : contest_opponents(fixture.check(), fixture.tables, 0)) {
    roster.eliminated.push_back(member.entrant);
  }
  const auto boss = draw_replacement(fixture.check(), fixture.tables, {}, roster, 0, constant(0));
  REQUIRE(boss.has_value());
  CHECK(boss->source == RosterSource::champion);
  CHECK(boss->entrant == 8);
  roster.members[0] = *boss;
  const auto respawn =
      draw_replacement(fixture.check(), fixture.tables, {}, roster, 1, constant(0));
  REQUIRE(respawn.has_value());
  CHECK(respawn->source != RosterSource::champion);
  CHECK(std::ranges::find(roster.eliminated, respawn->entrant) != roster.eliminated.end());
}
