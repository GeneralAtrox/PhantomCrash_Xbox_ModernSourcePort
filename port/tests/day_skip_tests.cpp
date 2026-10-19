// The schedule's SKIP TO DAY: the nights it plans, its question and the
// skip itself.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/day_skip.hpp"
#include "phantomcrash/schedule.hpp"

#include <string>

using namespace phantomcrash;

namespace {

DataRoots shipped() {
  return DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT);
}

ContestTables no_contests() {
  std::string calendar;
  for (int day = 0; day < calendar_days; ++day) {
    calendar += std::string(day == 0 ? "" : ",") + R"({"venues": [-1, -1, -1, -1]})";
  }
  return parse_contest_tables(json::parse(R"({
    "classes": [{"name": "Open", "regulation": "Rules.", "fee": 5000}],
    "contests": [{"name": "Day cup", "class": 0}],
    "calendar": [)" + calendar + "]}"));
}

InputFrame press(const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  return input;
}

constexpr double tick = 1.0 / 60.0;

void settle(Schedule &schedule, const double seconds = 1.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    schedule.update(tick, InputFrame{});
  }
}

void tap(Schedule &schedule, const Buttons buttons) {
  schedule.update(tick, press(buttons));
  settle(schedule, 0.3);
}

Schedule shipped_schedule(const ContestTables &contests, const QuestSave &quest,
                          const std::string &from) {
  const DataRoots roots = shipped();
  return Schedule(parse_schedule(roots.document("ui/schedule.json")), contests, quest, "garage",
                  from, parse_message_box(roots.document("ui/message_box.json")));
}

} // namespace

TEST_CASE("a day skip counts the nights and warns of a lap on a cleared game") {
  const DaySkipScript script = parse_schedule(shipped().document("ui/schedule.json")).skip;
  REQUIRE(script.enabled);
  QuestSave quest;
  quest.day = 58;
  quest.cleared = true;
  quest.laps = 2;
  const DaySkipPlan lap = plan_day_skip(quest, 1, calendar_days);
  CHECK(lap.nights == 3);
  CHECK(lap.raises);
  const auto lines = day_skip_lines(script, lap);
  std::string all;
  for (const std::string &line : lines) {
    all += line + "\n";
  }
  CHECK(all.find("DAY-02") != std::string::npos);
  CHECK(all.find("3 days") != std::string::npos);
  CHECK(all.find("+2 -> +3") != std::string::npos);

  const DaySkipPlan one = plan_day_skip(quest, 59, calendar_days);
  CHECK(one.nights == 1);
  CHECK(!one.raises);
  CHECK(day_skip_lines(script, one)[1].starts_with("1 day "));

  CHECK(plan_day_skip(quest, 58, calendar_days).nights == 0);
  CHECK(day_skip_lines(script, plan_day_skip(quest, 58, calendar_days)) == script.nothing);

  quest.cleared = false;
  CHECK(!plan_day_skip(quest, 1, calendar_days).raises);
}

TEST_CASE("SKIP TO DAY sleeps to the night before the cursor's day and leaves to sleep") {
  const ContestTables contests = no_contests();
  QuestSave quest;
  quest.day = 1;
  Schedule schedule = shipped_schedule(contests, quest, "garage");
  settle(schedule);
  REQUIRE(schedule.skip_offered());
  for (int step = 0; step < 3; ++step) {
    tap(schedule, button::right);
  }
  CHECK(schedule.skip_day() == 4);
  tap(schedule, button::b);
  CHECK(schedule.row() == Schedule::exit_row);
  CHECK(schedule.button() == Schedule::exit_button);
  tap(schedule, button::left); // the shipped SKIP TO DAY sits left of EXIT
  CHECK(schedule.button() == Schedule::skip_button);
  tap(schedule, button::a);
  REQUIRE(schedule.box().is_open());
  CHECK(schedule.box().kind() == MessageBox::Kind::question);
  CHECK(schedule.box().choice() == 0);
  tap(schedule, button::a);
  CHECK(!schedule.box().is_open());
  settle(schedule);
  REQUIRE(schedule.done());
  CHECK(schedule.next() == "sleep");
  REQUIRE(schedule.skipped_quest() != nullptr);
  CHECK(schedule.skipped_quest()->day == 3);
  CHECK(schedule.skipped_quest()->total_days == quest.total_days + 2);
}

TEST_CASE("SKIP TO DAY asks with NO first across a lap and stays on NO") {
  const ContestTables contests = no_contests();
  QuestSave quest;
  quest.day = 58;
  quest.cleared = true;
  Schedule schedule = shipped_schedule(contests, quest, "garage");
  settle(schedule);
  tap(schedule, button::right);
  tap(schedule, button::right); // day 0, past the wrap
  tap(schedule, button::b);
  tap(schedule, button::left);
  tap(schedule, button::a);
  REQUIRE(schedule.box().is_open());
  CHECK(schedule.box().choice() == 1);
  tap(schedule, button::a);
  CHECK(!schedule.box().is_open());
  CHECK(!schedule.done());
  CHECK(schedule.skipped_quest() == nullptr);
}

TEST_CASE("SKIP TO DAY is offered from every place unless 'from' lists others, nor for today") {
  const ContestTables contests = no_contests();
  QuestSave quest;
  CHECK(shipped_schedule(contests, quest, "wild_machines_buy_chip").skip_offered());
  CHECK(shipped_schedule(contests, quest, "club_wired").skip_offered());

  const DataRoots roots = shipped();
  ScheduleScript garage_only = parse_schedule(roots.document("ui/schedule.json"));
  garage_only.skip.from = {"garage"};
  Schedule club(garage_only, contests, quest, "garage", "club_wired",
                parse_message_box(roots.document("ui/message_box.json")));
  settle(club);
  CHECK(!club.skip_offered());
  tap(club, button::b);
  tap(club, button::left);
  CHECK(club.button() == Schedule::exit_button);

  Schedule garage = shipped_schedule(contests, quest, "garage");
  settle(garage);
  tap(garage, button::b);
  tap(garage, button::left);
  tap(garage, button::a);
  REQUIRE(garage.box().is_open());
  CHECK(garage.box().kind() == MessageBox::Kind::notice);
  tap(garage, button::a);
  CHECK(!garage.box().is_open());
  CHECK(!garage.done());
}
