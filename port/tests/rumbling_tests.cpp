// RUMBLING: the area select, the regulation check, the sub-menu, the gates
// and entering.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/rumbling.hpp"
#include "phantomcrash/rumbling_battle.hpp"

#include <string>

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

RumblingScript shipped_script() {
  return parse_rumbling(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/rumbling.json"));
}

RumblingRules shipped_rules() {
  return parse_rumbling_rules(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"));
}

// Day 0: Shinjuku holds an open contest (class 0, fee 1000), Shibuya a
// rentals-only one (class 1) and Aqualine the maker-1 class; day 1 holds
// Shinjuku's Area Ranker contest (14).
ContestTables contests() {
  std::string list;
  for (int index = 0; index < 15; ++index) {
    const int contest_class = index == 1 ? 1 : (index == 2 ? 2 : 0);
    list += std::string(index == 0 ? "" : ",") + "{\"name\": \"C" + std::to_string(index) +
            "\", \"class\": " + std::to_string(contest_class) + "}";
  }
  std::string calendar;
  for (int day = 0; day < calendar_days; ++day) {
    calendar += std::string(day == 0 ? "" : ",") +
                (day == 1 ? "{\"venues\": [14, -1, -1, -1]}" : "{\"venues\": [0, 1, 2, -1]}");
  }
  return parse_contest_tables(
      json::parse("{\"classes\": [{\"name\": \"OPEN\", \"fee\": 1000},"
                  " {\"name\": \"RENT\", \"fee\": 500, \"other\": 0},"
                  " {\"name\": \"MAKER\", \"fee\": 100, \"maker\": 1}], \"contests\": [" +
                  list + "], \"calendar\": [" + calendar + "]}"));
}

struct Fixture {
  RumblingRules rules = shipped_rules();
  PartTables parts;
  GarageTables garage;
  PartStatsTable stats;
  ContestTables tables = contests();

  Fixture() {
    parts.bodies.assign(1, ArmourRecord{});
    parts.legs.assign(1, ArmourRecord{});
    parts.chips.assign(1, ChipRecord{});
  }

  [[nodiscard]] RegulationCheck check() const {
    return {rules, parts, garage, stats};
  }
};

// A ridden hangar 0 with a body, legs and CHIP, and money in hand.
QuestSave riding_quest() {
  QuestSave quest;
  quest.money = 5000;
  quest.garage = new_game_garage(1);
  Hangar &bay = quest.garage.hangars[0];
  const auto fit = [&](const PartSlot slot, const PartKind kind) {
    bay.parts[static_cast<std::size_t>(slot)] = quest.garage.add_stock({kind, 0, 0, "", 1});
  };
  fit(PartSlot::body, PartKind::body);
  fit(PartSlot::legs, PartKind::legs);
  fit(PartSlot::chip, PartKind::chip);
  quest.garage.riding = 0;
  return quest;
}

InputFrame press(const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  return input;
}

void settle(Rumbling &rumbling, const double seconds = 1.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    rumbling.update(tick, InputFrame{});
  }
}

void tap(Rumbling &rumbling, const Buttons buttons) {
  rumbling.update(tick, press(buttons));
  rumbling.update(tick, InputFrame{});
}

} // namespace

TEST_CASE("rumbling.json parses with EXIT last and the sub-menu's six items") {
  const RumblingScript script = shipped_script();
  CHECK(script.areas.size() == 4);
  CHECK(script.areas.back().exit);
  CHECK(script.menu.items.size() == 6);
  CHECK(script.menu.items.back().action == RumblingAction::back);
  CHECK(script.messages.contains("regulation"));
  const RumblingBattle battle = parse_rumbling_battle(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"));
  CHECK(battle.return_to == "club_wired");
}

TEST_CASE("the regulation needs the fee, a ridden Scoobee and the class's maker") {
  const Fixture fixture;
  QuestSave quest = riding_quest();
  const ContestClass open = *fixture.tables.contest_class(0);
  CHECK(meets_regulation(fixture.check(), open, quest));
  CHECK(!meets_regulation(fixture.check(), *fixture.tables.contest_class(1), quest));
  CHECK(!meets_regulation(fixture.check(), *fixture.tables.contest_class(2), quest));
  quest.money = 999;
  CHECK(!meets_regulation(fixture.check(), open, quest));
  quest.money = 5000;
  quest.garage.riding = -1;
  CHECK(!meets_regulation(fixture.check(), open, quest));
}

TEST_CASE("an area ranker's contest opens once its leaders are won") {
  const RumblingRules rules = shipped_rules();
  CHECK(ranker_contest(rules, 0, 14) == RankerContest::area);
  CHECK(ranker_contest(rules, 1, 14) == RankerContest::none);
  CHECK(ranker_contest(rules, 2, 49) == RankerContest::first);
  CHECK(!ranker_open(rules, RankerContest::area, 0, {3, 4, 5}));
  CHECK(ranker_open(rules, RankerContest::area, 0, {3, 4, 5, 6, 7, 8, 9, 10}));
  CHECK(!ranker_open(rules, RankerContest::first, 0, {14, 31}));
  CHECK(ranker_open(rules, RankerContest::first, 0, {14, 31, 48}));
  CHECK(rental_allowed(rules, 0));
  CHECK(!rental_allowed(rules, 1));
}

TEST_CASE("cancel moves to EXIT and confirm there leaves for Club Wired") {
  const Fixture fixture;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), riding_quest());
  settle(rumbling);
  tap(rumbling, button::cancel);
  CHECK(rumbling.area() == 3);
  tap(rumbling, button::confirm);
  settle(rumbling);
  CHECK(rumbling.done());
  CHECK(rumbling.next() == "club_wired");
}

TEST_CASE("a poor player is told of the fee") {
  const Fixture fixture;
  QuestSave quest = riding_quest();
  quest.money = 10;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), quest);
  settle(rumbling);
  tap(rumbling, button::confirm);
  CHECK(rumbling.view() == RumblingView::notice);
  CHECK(rumbling.message() == "fee");
  tap(rumbling, button::confirm);
  CHECK(rumbling.view() == RumblingView::areas);
}

TEST_CASE("the area ranker's venue is shut until its leaders are beaten") {
  const Fixture fixture;
  QuestSave quest = riding_quest();
  quest.day = 1;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), quest);
  settle(rumbling);
  tap(rumbling, button::confirm);
  CHECK(rumbling.message() == "area_ranker");
}

TEST_CASE("ENTRY opens the gates and entering pays the fee for the battle") {
  const Fixture fixture;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), riding_quest());
  settle(rumbling);
  tap(rumbling, button::confirm);
  settle(rumbling);
  CHECK(rumbling.view() == RumblingView::menu);
  CHECK(rumbling.can_choose(RumblingAction::entry));
  CHECK(rumbling.can_choose(RumblingAction::rental));
  tap(rumbling, button::confirm);
  CHECK(rumbling.view() == RumblingView::gates);
  tap(rumbling, button::right);
  CHECK(rumbling.gate() == 1);
  tap(rumbling, button::confirm);
  settle(rumbling);
  CHECK(rumbling.done());
  CHECK(rumbling.next() == "rumbling_battle");
  CHECK(rumbling.quest().money == 4000);
  CHECK(rumbling.entry().has_value());
  CHECK(rumbling.entry()->gate == 1);
  CHECK(!rumbling.entry()->rental);
}

TEST_CASE("a rentals-only contest greys ENTRY and refuses RENTAL outside its classes") {
  const Fixture fixture;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), riding_quest());
  settle(rumbling);
  tap(rumbling, button::right);
  settle(rumbling);
  tap(rumbling, button::confirm);
  settle(rumbling);
  CHECK(!rumbling.can_choose(RumblingAction::entry));
  tap(rumbling, button::confirm);
  CHECK(rumbling.message() == "rentals_only");
  tap(rumbling, button::confirm);
  tap(rumbling, button::down);
  tap(rumbling, button::confirm);
  CHECK(rumbling.message() == "no_rentals");
}

TEST_CASE("RENTAL rides one of the rental SVs, and coming back resumes on SELECT SV") {
  const Fixture fixture;
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), riding_quest());
  settle(rumbling);
  tap(rumbling, button::confirm);
  settle(rumbling);
  tap(rumbling, button::down);
  tap(rumbling, button::confirm);
  tap(rumbling, button::confirm);
  settle(rumbling);
  CHECK(rumbling.entry().has_value());
  CHECK(rumbling.entry()->rental);
  CHECK(rumbling.entry()->rental_robot >= 84 && rumbling.entry()->rental_robot <= 87);
  const Rumbling resumed(shipped_script(), fixture.tables, fixture.check(), riding_quest(), 0);
  CHECK(resumed.view() == RumblingView::menu);
  CHECK(resumed.menu_item() == static_cast<int>(RumblingAction::select_sv));
}

TEST_CASE("a ranker's contest keeps one gate for the ranker") {
  const Fixture fixture;
  QuestSave quest = riding_quest();
  quest.day = 1;
  quest.won_contests = {3, 4, 5, 6, 7, 8, 9, 10};
  Rumbling rumbling(shipped_script(), fixture.tables, fixture.check(), quest);
  settle(rumbling);
  tap(rumbling, button::confirm);
  settle(rumbling);
  tap(rumbling, button::confirm);
  CHECK(rumbling.view() == RumblingView::gates);
  CHECK(rumbling.ranker_gate() == 1);
  CHECK(rumbling.gate() == 0);
  tap(rumbling, button::right);
  CHECK(rumbling.gate() == 2);
}

TEST_CASE("the quick rental restarts its contest while preserving the player's garage") {
  Fixture fixture;
  const QuickRental preset =
      parse_rumbling_battle(
          DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"))
          .quick_rentals.front();
  CHECK(preset.screen == "rumbling_d_rental");
  fixture.garage.robots.resize(static_cast<std::size_t>(preset.robot + 1));
  RobotRecord &rental = fixture.garage.robots[static_cast<std::size_t>(preset.robot)];
  for (const PartSlot slot : {PartSlot::body, PartSlot::legs, PartSlot::chip}) {
    rental.parts[static_cast<std::size_t>(slot)] = RobotPart{0, 0};
  }
  QuestSave quest = riding_quest();
  quest.player_name = "Pilot";
  quest.day = 12;
  quest.live_epoch = 123;
  quest.rosters[0].day = quest.day;
  const Garage owned = quest.garage;
  const auto entry =
      prepare_quick_rental(preset, fixture.tables, fixture.rules, fixture.garage, quest);
  CHECK(entry.has_value());
  CHECK(entry->rental);
  CHECK(entry->rental_robot == preset.robot);
  CHECK(entry->contest == contest_on(fixture.tables, preset.day, preset.area, false));
  CHECK(entry->area == preset.area);
  CHECK(quest.day == preset.day);
  CHECK(quest.money == 4000); // the fixture's normal 1000 NY entry fee
  CHECK(quest.player_name == "Pilot");
  CHECK(quest.garage == owned);
  CHECK(quest.live_epoch == 0);
  CHECK(quest.rosters[0].day == -1);

  quest.money = 0;
  const QuestSave poor = quest;
  CHECK(!prepare_quick_rental(preset, fixture.tables, fixture.rules, fixture.garage, quest));
  CHECK(quest == poor);
}

TEST_CASE("the level select offers a night rental match under its own screen") {
  const auto battle = parse_rumbling_battle(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"));
  REQUIRE(battle.quick_rentals.size() == 2);
  const QuickRental *night = find_quick_rental(battle, "rumbling_d_night_rental");
  REQUIRE(night != nullptr);
  CHECK(night == &battle.quick_rentals[1]);
  CHECK(night->day == 3); // Night Electric, Shinjuku's Class D night contest
  CHECK(night->area == 0);
  CHECK(find_quick_rental(battle, "rumbling_battle") == nullptr);
}

TEST_CASE("an unavailable quick rental leaves the saved quest untouched") {
  const Fixture fixture;
  const QuickRental preset =
      parse_rumbling_battle(
          DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("world/rumbling.json"))
          .quick_rentals.front();
  QuestSave quest = riding_quest();
  const QuestSave before = quest;
  CHECK(!prepare_quick_rental(preset, fixture.tables, fixture.rules, fixture.garage, quest));
  CHECK(quest == before);
}
