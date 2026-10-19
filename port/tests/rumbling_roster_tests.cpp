// RUMBLING's day rosters and WIRE HEADS DATA: who fits a contest, the draw
// from the teams (0x045270, 0x045a68), the card and the window's browsing.

#include "rumbling_fixture.hpp"
#include "test_support.hpp"

#include "phantomcrash/rumbling.hpp"

#include <string>

using namespace phantomcrash;
using namespace phantomcrash::rumbling_fixture;

namespace {

void tap(Rumbling &rumbling, const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  rumbling.update(tick, input);
  rumbling.update(tick, InputFrame{});
}

} // namespace

TEST_CASE("an entrant fits a contest by level, maker, legs, weight and O.C modules") {
  const Fixture fixture;
  const RegulationCheck check = fixture.check();
  CHECK(entrant_fits(check, fixture.tables, 0, 0));
  CHECK(!entrant_fits(check, fixture.tables, 0, 8)); // level 50 is past 40
  CHECK(!entrant_fits(check, fixture.tables, 0, -1));
  CHECK(!entrant_fits(check, fixture.tables, 1, 0)); // maker 0 on legs type 2
  CHECK(entrant_fits(check, fixture.tables, 1, 9));
  CHECK(entrant_fits(check, fixture.tables, 2, 0));
  CHECK(!entrant_fits(check, fixture.tables, 2, 10)); // 25 heavy, and an O.C module
}

TEST_CASE("the roster draws the unbeaten teams' members, a leader only once they are beaten") {
  const Fixture fixture;
  QuestSave quest;
  // Low numbers keep to the unbeaten and take the first one left.
  const AreaRoster roster = draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.0F));
  CHECK(roster.day == 0);
  CHECK(roster.contest == 0);
  REQUIRE(roster.members.size() == 3); // roster_size 3: four places, the player's among them
  CHECK((roster.members[0] == RosterMember{0, RosterSource::member, 0, 1}));
  CHECK(roster.members[1].entrant == 1);
  CHECK(roster.members[2].entrant == 2);
  for (const RosterMember &member : roster.members) {
    CHECK(member.entrant != 7);
  }
  // Team 0's members beaten: its leader joins the unbeaten.
  quest.beaten = {beaten_key(0, 1), beaten_key(0, 2)};
  const AreaRoster later = draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.0F));
  REQUIRE(later.members.size() == 3);
  CHECK((later.members[0] == RosterMember{7, RosterSource::leader, 0, 0}));
  CHECK(later.members[1].entrant == 2);
}

TEST_CASE("past reserve_after a high number draws the extra team, never the last one left") {
  const Fixture fixture;
  const QuestSave quest;
  const AreaRoster roster = draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.99F));
  REQUIRE(roster.members.size() == 3);
  // The unbeaten are 0, 1, 2, 3 and 9; each draw takes (left - 1) x 0.99,
  // rounded down, so the last one left (9) stays.
  CHECK(roster.members[0].entrant == 3);
  CHECK(roster.members[1].entrant == 2);
  CHECK((roster.members[2] == RosterMember{4, RosterSource::extra, 2, 1}));
}

TEST_CASE("the champion joins once every team is beaten, if won or a champion's class") {
  Fixture fixture;
  QuestSave quest;
  const auto drawn = [&fixture](const QuestSave &save) {
    return draw_roster(fixture.check(), fixture.tables, save, 3, constant(0.0F)).members;
  };
  CHECK(drawn(quest).empty());
  quest.won_contests = {3};
  const auto won = drawn(quest);
  REQUIRE(won.size() == 1); // the champion, not held to the contest's levels
  CHECK((won[0] == RosterMember{8, RosterSource::champion, 3, 0}));
  fixture.rules.roster.champion_classes = {0};
  CHECK(drawn(QuestSave{}).size() == 1);
  // Contest 0's teams are not all beaten: no champion, won or not.
  quest.won_contests = {0};
  for (const RosterMember &member :
       draw_roster(fixture.check(), fixture.tables, quest, 0, constant(0.0F)).members) {
    CHECK(member.source != RosterSource::champion);
  }
}

TEST_CASE("rosters are drawn once a day and saved with the game") {
  const Fixture fixture;
  QuestSave quest;
  CHECK(refresh_rosters(fixture.check(), fixture.tables, quest, constant(0.0F)));
  CHECK(!refresh_rosters(fixture.check(), fixture.tables, quest, constant(0.0F)));
  CHECK(quest.rosters[0].members.size() == 3);
  CHECK(quest.rosters[1].contest == no_contest);
  CHECK(quest.rosters[1].members.empty());
  quest.beaten = {beaten_key(1, 2)};
  const QuestSave loaded = quest_from_json(quest_to_json(quest));
  CHECK(loaded.rosters == quest.rosters);
  CHECK(loaded.beaten == quest.beaten);
  quest.day = 2;
  CHECK(refresh_rosters(fixture.check(), fixture.tables, quest, constant(0.0F)));
  CHECK(quest.rosters[0].day == 2);
}

TEST_CASE("a card's bounty grows with the level and a cleared game raises the level") {
  const Fixture fixture;
  ContestTables tables = contests();
  tables.entrants[0].level = 65;
  QuestSave quest;
  quest.cleared = true;
  quest.laps = 40;
  const RosterCard card = roster_card(fixture.check(), tables, quest, 0, true);
  CHECK(card.bounty == 10078); // 5000 x (1 + 65 / 64)
  CHECK(card.level == 99);
  CHECK(roster_card(fixture.check(), tables, quest, 0, false).level == 65);
}

TEST_CASE("CHARACTER INFO steps through the day's opponents and B goes back") {
  const Fixture fixture;
  const RumblingScript script =
      parse_rumbling(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/rumbling.json"));
  QuestSave quest;
  quest.money = 5000;
  Rumbling rumbling(script, fixture.tables, fixture.check(), quest, 0);
  CHECK(rumbling.quest().rosters[0].day == 0);
  CHECK(rumbling.can_choose(RumblingAction::character_info));
  for (int k = 0; k < 60; ++k) {
    rumbling.update(tick, InputFrame{});
  }
  tap(rumbling, script.down); // SELECT SV to CHARACTER INFO
  REQUIRE(rumbling.menu_item() == static_cast<int>(RumblingAction::character_info));
  (void)rumbling.take_sounds();
  tap(rumbling, script.confirm_buttons);
  REQUIRE(rumbling.view() == RumblingView::character_info);
  const auto count = static_cast<int>(rumbling.cards().size());
  CHECK(count == static_cast<int>(rumbling.quest().rosters[0].members.size()));
  tap(rumbling, script.left);
  CHECK(rumbling.card() == count - 1); // wrapping
  tap(rumbling, script.right);
  CHECK(rumbling.card() == 0);
  tap(rumbling, script.cancel_buttons);
  CHECK(rumbling.view() == RumblingView::menu);
}

TEST_CASE("in a ranker's contest WIRE HEADS DATA shows the ranker alone") {
  const Fixture fixture;
  QuestSave quest;
  quest.day = 1;
  refresh_rosters(fixture.check(), fixture.tables, quest, constant(0.0F));
  const auto cards = wire_heads_cards(fixture.check(), fixture.tables, quest, 0, 14);
  REQUIRE(cards.size() == 1);
  CHECK(cards[0].entrant == fixture.rules.area_rankers[0].entrant);
  CHECK(wire_heads_cards(fixture.check(), fixture.tables, quest, 1, no_contest).empty());
}
