// RECORDS: its values, rows and menus.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/records.hpp"

#include <cmath>
#include <string>

using namespace phantomcrash;

namespace {

bool near(const double a, const double b) {
  return std::abs(a - b) < 1e-4;
}

RecordsScript shipped_records() {
  return parse_records(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/records.json"));
}

ContestTables no_contests() {
  std::string calendar;
  for (int day = 0; day < calendar_days; ++day) {
    calendar += std::string(day == 0 ? "" : ",") + R"({"venues": [-1, -1, -1, -1]})";
  }
  return parse_contest_tables(
      json::parse(R"({"classes": [], "contests": [], "calendar": [)" + calendar + "]}"));
}

InputFrame press(const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  return input;
}

constexpr double tick = 1.0 / 60.0;

void settle(Records &records, const double seconds = 1.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    records.update(tick, InputFrame{});
  }
}

void tap(Records &records, const Buttons buttons) {
  records.update(tick, press(buttons));
  records.update(tick, InputFrame{});
}

} // namespace

TEST_CASE("record values read with commas, units and hh:mm:ss") {
  const RecordNumbers numbers;
  CHECK(record_text(RecordFormat::count, 1234567, numbers).digits == "1,234,567");
  CHECK(record_text(RecordFormat::money, 5000, numbers).unit == " NY");
  CHECK(record_text(RecordFormat::percent, 50, numbers).unit == "%");
  CHECK(record_text(RecordFormat::time, 3725, numbers).digits == "01:02:05");
  CHECK(record_text(RecordFormat::time, 500 * 3600, numbers).digits == "99:00:00");
}

TEST_CASE("a return rate is the returns over the rumblings, and the rate of none is 0") {
  const RecordsScript script = shipped_records();
  RecordRow rate;
  rate.stat = "returns";
  rate.ratio_of = "rumblings";
  QuestSave quest;
  CHECK(record_value(rate, script, quest, {}) == 0);
  quest.records["rumblings"] = 8;
  quest.records["returns"] = 2;
  CHECK(record_value(rate, script, quest, {}) == 25);
}

TEST_CASE("the achievement rate counts contests won and story events played") {
  const RecordsScript script = shipped_records();
  RecordRow rate;
  rate.completion = true;
  QuestSave quest;
  quest.won_contests = {6, 5};
  const auto contests = static_cast<std::int64_t>(script.completion_contests.size());
  CHECK(record_value(rate, script, quest, {10, 3}) == (2 + 3) * 100 / (contests + 10));
  RecordRow days;
  days.stat = "days_passed";
  quest.total_days = 12;
  CHECK(record_value(days, script, quest, {}) == 12);
}

TEST_CASE("a page's rows appear in turn, fading in behind a light bar") {
  const RowReveal reveal;
  CHECK(!row_look(reveal, 1, 0.45).shown);
  const RowLook start = row_look(reveal, 1, 0.55);
  CHECK(start.shown);
  CHECK(start.bar);
  CHECK(start.alpha < 0.1F);
  const RowLook later = row_look(reveal, 1, 0.5 + 5.0);
  CHECK(!later.bar);
  CHECK(near(later.alpha, 1.0));
  CHECK(later.colour == 0xffffff);
}

TEST_CASE("CONVENTION RECORD's items slide in, the lowest furthest") {
  const RecordsScript script = shipped_records();
  const ConventionStyle &style = script.convention;
  CHECK(convention_slide(style, 4, 0.0) < convention_slide(style, 0, 0.0));
  CHECK(near(convention_slide(style, 4, style.slide_seconds), 0.0));
}

TEST_CASE("the menu opens a page, and cancel returns to it") {
  const ContestTables contests = no_contests();
  Records records(shipped_records(), contests, {}, {}, "garage");
  settle(records);
  tap(records, button::a);
  CHECK(records.view() == RecordsView::page);
  CHECK(records.page() != nullptr);
  settle(records, 0.5);
  tap(records, button::b);
  CHECK(records.view() == RecordsView::menu);
}

TEST_CASE("a page plays a sound as each row appears") {
  const ContestTables contests = no_contests();
  Records records(shipped_records(), contests, {}, {}, "garage");
  settle(records);
  static_cast<void>(records.take_sounds());
  tap(records, button::a);
  settle(records, 0.35);
  const auto sounds = records.take_sounds();
  CHECK(std::ranges::count(sounds, std::string("record_row")) == 1);
}

TEST_CASE("a venue's grid names its contests, the area ranker in the last column") {
  const ContestTables contests = no_contests();
  Records records(shipped_records(), contests, {}, {}, "garage");
  settle(records);
  tap(records, button::down);
  tap(records, button::a);
  CHECK(records.view() == RecordsView::convention);
  settle(records, 0.6);
  tap(records, button::a);
  CHECK(records.view() == RecordsView::venue);
  settle(records, 0.4);
  CHECK(records.selected_contest() == 6);
  tap(records, button::left);
  CHECK(records.grid_column() == 4);
  CHECK(records.selected_contest() == 14);
  tap(records, button::b);
  CHECK(records.view() == RecordsView::convention);
}

TEST_CASE("REGULATION turns its pages with the triggers") {
  const ContestTables contests = no_contests();
  Records records(shipped_records(), contests, {}, {}, "garage");
  settle(records);
  tap(records, button::down);
  tap(records, button::a);
  settle(records, 0.6);
  for (int item = 0; item < 3; ++item) {
    tap(records, button::down);
  }
  tap(records, button::a);
  CHECK(records.view() == RecordsView::regulation);
  settle(records, 0.4);
  CHECK(records.selected_contest() == 1);
  tap(records, button::right_trigger);
  CHECK(records.regulation_page() == 1);
  CHECK(records.selected_contest() == 33);
}

TEST_CASE("EXIT leaves for the place RECORDS was opened from") {
  const ContestTables contests = no_contests();
  Records records(shipped_records(), contests, {}, {}, "club_wired");
  settle(records);
  tap(records, button::b);
  tap(records, button::a);
  settle(records);
  CHECK(records.done());
  CHECK(records.next() == "club_wired");
}
