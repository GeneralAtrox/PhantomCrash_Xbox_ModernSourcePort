#include "test_support.hpp"
#include "rumbling_fixture.hpp"
#include "../src/desktop_coop_matches.hpp"
#include <algorithm>

using namespace phantomcrash;
using namespace phantomcrash::rumbling_fixture;

namespace {
struct MatchFixture {
  Fixture data;
  TestDriveScript drive;
  std::map<std::string, WeaponTuning> templates;
  CoopMatch match;
  MatchFixture() {
    data.rules.roster.live_size = 4;
    const RumblingTables tables{data.parts, data.garage, data.tables, data.rules, drive, templates};
    match.world = std::make_shared<RumblingWorld>(data.check(), tables, BattleSettings{},
                                                  std::array<RumblingStage, roster_areas>{},
                                                  RumblingLive{}, 7);
    match.battle = match.world->lend(0, match.simulation_save);
    REQUIRE(match.battle);
    match.battle->set_cooperative(true);
    match.players[0].actor = 0;
    match.players[1].actor = 1;
    match.battle->robot(0).piloted = true;
    match.battle->robot(1).piloted = true;
    match.simulation_save.rosters[0].members[1] = {0, RosterSource::champion, 0, 0};
  }
  void death(int owner, bool assisted = false) {
    auto &victim = match.battle->robot(2);
    victim.piloted = false;
    victim.shot_by = owner;
    victim.damage_from.assign(4, 0);
    victim.damage_from[owner] = 100;
    if (assisted) {
      victim.damage_from[1] = 10;
    }
    // Model the combat layer's existing per-contributor credit, then deliver its event.
    for (int i = 0; i < 4; ++i) {
      if (victim.damage_from[i] > 0) {
        ++match.battle->robot(i).kills;
      }
    }
    events() = {{2, BattleEventKind::destroyed}};
    match.collect_rewards();
  }
  std::vector<BattleEvent> &events() {
    // Controlled combat event stream; no changes to the production Battle API.
    return const_cast<std::vector<BattleEvent> &>(match.battle->events());
  }
};
} // namespace

TEST_CASE("co-op preferences round trip without changing player saves") {
  Settings settings;
  settings.coop = {-30, 250, true, true, false};
  settings.coop_profiles[0] = QuestSave{};
  settings.coop_profiles[0]->money = 12345;
  const auto restored = settings_from_json(settings_to_json(settings));
  CHECK(restored.coop == settings.coop);
  CHECK(restored.coop_profiles[0]->money == 12345);
  const auto defaults = settings_from_json(json::parse("{}"));
  CHECK(defaults.experimental_coop);
  CHECK(defaults.coop.enemy_level_offset == 0);
  CHECK(defaults.coop.kill_ny_percent == 100);
  CHECK(defaults.coop.share_shootdowns);
  CHECK(defaults.coop.share_cash);
  CHECK(defaults.coop.share_boss_clears);
  const auto bounded = settings_from_json(
      json::parse(R"({"coop":{"enemy_level_offset":200,"kill_ny_percent":-20}})"));
  CHECK(bounded.coop.enemy_level_offset == 99);
  CHECK(bounded.coop.kill_ny_percent == 0);
}

TEST_CASE("online profile round trips independently of solo and local co-op") {
  Settings settings;
  settings.quest.money = 111;
  settings.coop_profiles[0] = QuestSave{};
  settings.coop_profiles[0]->money = 222;
  settings.online_profile = QuestSave{};
  settings.online_profile->money = 333;
  settings.online_profile->player_name = "Online";
  const auto loaded = settings_from_json(settings_to_json(settings));
  CHECK(loaded.quest.money == 111);
  CHECK(loaded.coop_profiles[0]->money == 222);
  CHECK(loaded.online_profile->money == 333);
  CHECK(loaded.online_profile->player_name == "Online");
  CHECK(!settings_from_json(json::parse("{}")).online_profile);
}

TEST_CASE("shared boss awards pay both players in full and settle only once") {
  MatchFixture f;
  f.match.options = {0, 100, true, true, true};
  const auto bounty = f.match.world->coop_bounty(f.match.simulation_save, 0);
  REQUIRE(bounty > 0);
  f.death(0);
  for (int i : {0, 1}) {
    CHECK(f.match.players[i].bounty == bounty);
    CHECK(f.match.battle->robot(i).kills == 1);
    CHECK(f.match.players[i].won);
    CHECK(f.match.players[i].beaten.size() == 1);
  }
  CHECK(f.match.players[2].bounty == 0);
  CHECK(!f.match.players[2].won);
  QuestSave p1, p2;
  CHECK(f.match.leave(0, p1) == bounty);
  CHECK(f.match.leave(0, p1) == 0);
  CHECK(f.match.leave(1, p2) == bounty);
  CHECK(p1.beaten == p2.beaten);
  CHECK(p1.beaten.size() == 1);
}

TEST_CASE("shared shootdowns never double count contributors") {
  MatchFixture f;
  f.match.options.share_shootdowns = true;
  f.death(0, true);
  CHECK(f.match.battle->robot(0).kills == 1);
  CHECK(f.match.battle->robot(1).kills == 1);
}

TEST_CASE("player two kill pays the same full multiplied reward to both participants") {
  MatchFixture f;
  f.match.options = {0, 250, true, true, true};
  const auto bounty = f.match.world->coop_bounty(f.match.simulation_save, 0);
  f.death(1);
  CHECK(f.match.players[0].bounty == bounty * 250 / 100);
  CHECK(f.match.players[1].bounty == f.match.players[0].bounty);
  CHECK(f.match.battle->robot(0).kills == 1);
  CHECK(f.match.battle->robot(1).kills == 1);
  CHECK(f.match.players[0].won);
  CHECK(f.match.players[1].won);
  // A later tick with no combat events cannot pay the previous reward again.
  f.events().clear();
  f.match.collect_rewards();
  CHECK(f.match.players[0].bounty == bounty * 250 / 100);
}

TEST_CASE("co-op reward switches are independent and multiplier applies once") {
  MatchFixture f;
  f.match.options = {0, 200, false, false, false};
  const auto bounty = f.match.world->coop_bounty(f.match.simulation_save, 0);
  f.death(0);
  CHECK(f.match.players[0].bounty == bounty * 2);
  CHECK(f.match.players[0].won);
  CHECK(f.match.players[1].bounty == 0);
  CHECK(!f.match.players[1].won);
  CHECK(f.match.players[1].beaten.empty());
  CHECK(f.match.battle->robot(1).kills == 0);
}

TEST_CASE("cash from wrecks and pickups shares without the robot kill multiplier") {
  MatchFixture f;
  f.match.options = {0, 200, false, true, false};
  BattleEvent wreck{0, BattleEventKind::wreck_destroyed};
  wreck.amount = 5000;
  BattleEvent money{1, BattleEventKind::supply};
  money.amount = 60;
  BattleEvent repair{1, BattleEventKind::supply};
  f.events() = {wreck, money, repair};
  f.match.collect_rewards();
  CHECK(f.match.players[0].bounty == 5060);
  CHECK(f.match.players[1].bounty == 5060);
  f.match.players[1].actor = -1;
  f.events() = {wreck};
  f.match.collect_rewards();
  CHECK(f.match.players[0].bounty == 10060);
  CHECK(f.match.players[1].bounty == 5060);
}

TEST_CASE("NPC kills do not pay humans or clear bosses for them") {
  MatchFixture f;
  f.match.options = {0, 100, true, true, true};
  f.death(3);
  CHECK(f.match.players[0].bounty == 0);
  CHECK(f.match.players[1].bounty == 0);
  CHECK(!f.match.players[0].won);
  CHECK(!f.match.players[1].won);
  CHECK(f.match.battle->robot(0).kills == 0);
}

TEST_CASE("enemy offset adjusts loadouts and preserves humans and damaged state") {
  MatchFixture f;
  auto &enemy = f.match.battle->robot(2);
  enemy.hp = enemy.max_hp * 0.5F;
  enemy.kills = 3;
  enemy.lost[0] = true;
  const auto player_hp = f.match.battle->robot(1).max_hp;
  const auto player_level = f.match.world->robot(0, 1)->loadout.body_level;
  const auto enemy_level = f.match.world->robot(0, 2)->loadout.body_level;
  f.match.world->set_coop_level_offset(0, 10, f.match.simulation_save);
  CHECK(f.match.world->robot(0, 2)->loadout.body_level == enemy_level + 10);
  CHECK(f.match.world->robot(0, 1)->loadout.body_level == player_level);
  CHECK(f.match.battle->robot(1).max_hp == player_hp);
  CHECK(enemy.hp == enemy.max_hp * 0.5F);
  CHECK(enemy.kills == 3);
  CHECK(enemy.lost[0]);
  enemy.alive = false;
  enemy.dying = false;
  enemy.shot_by = -1;
  const auto changes = f.match.world->changes(0);
  for (int i = 0; i < 1200 && f.match.world->changes(0) == changes; ++i) {
    f.match.world->update(f.match.simulation_save, 1.0 / 60.0);
  }
  CHECK(f.match.world->changes(0) > changes);
  CHECK(f.match.world->robot(0, 2)->loadout.body_level == enemy_level + 10);

  f.match.world->set_coop_level_offset(0, -200, f.match.simulation_save);
  CHECK(f.match.world->robot(0, 2)->loadout.body_level == -99);
}

TEST_CASE("co-op players join in addition to the area's opponents") {
  Fixture data;
  data.rules.roster.live_size = 4; // the player's place and three opponents
  TestDriveScript drive;
  std::map<std::string, WeaponTuning> templates;
  const RumblingTables tables{data.parts, data.garage, data.tables, data.rules, drive, templates};
  CoopMatch match;
  match.world =
      std::make_shared<RumblingWorld>(data.check(), tables, BattleSettings{},
                                      std::array<RumblingStage, roster_areas>{}, RumblingLive{}, 7);
  match.battle = match.world->lend(0, match.simulation_save);
  REQUIRE(match.battle);
  match.battle->set_cooperative(true);
  const auto opponents = match.simulation_save.rosters[0].members.size();
  REQUIRE(opponents == 3);
  REQUIRE(match.battle->robots().size() == opponents + 1);

  CHECK(match.join(0, {}) == 0); // the player's own place
  CHECK(match.join(1, {}) == 4); // a robot of its own past the roster's
  CHECK(match.join(2, {}) == 5);
  CHECK(match.battle->robots().size() == opponents + 3);
  for (int index = 1; index <= static_cast<int>(opponents); ++index) {
    CHECK(!match.battle->robots()[static_cast<std::size_t>(index)].piloted);
  }

  // A place a player leaves is the next joiner's.
  QuestSave save;
  static_cast<void>(match.leave(1, save));
  CHECK(match.join(3, {}) == 4);
  CHECK(match.battle->robots().size() == opponents + 3);

  // Once the match is over, the area keeps its roster's robots alone.
  match.world->give_back(match.simulation_save);
  const auto area = match.world->lend(0, match.simulation_save);
  REQUIRE(area);
  CHECK(area->robots().size() == opponents + 1);
}
