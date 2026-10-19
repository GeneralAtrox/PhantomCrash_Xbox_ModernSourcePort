// The town screens' shared pieces, and the contest schedule.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/schedule.hpp"
#include "phantomcrash/town_screen.hpp"

#include <cmath>
#include <string>

using namespace phantomcrash;

namespace {

bool near(const double a, const double b) {
  return std::abs(a - b) < 1e-4;
}

// Contest 0 (day) on day 0's first row, contest 1 (night) on day 6's
// second, nothing else.
ContestTables tables() {
  std::string calendar;
  for (int day = 0; day < calendar_days; ++day) {
    const char *venues = day == 0   ? "[0, -1, -1, -1]"
                         : day == 6 ? "[-1, 1, -1, -1]"
                                    : "[-1, -1, -1, -1]";
    calendar += std::string(day == 0 ? "" : ",") + R"({"venues": )" + venues + "}";
  }
  return parse_contest_tables(json::parse(R"({
    "classes": [{"name": "Open", "regulation": "Rules.", "fee": 5000}],
    "contests": [{"name": "Day cup", "class": 0}, {"name": "Night cup", "class": 0, "night": true}],
    "calendar": [)" + calendar + "]}"));
}

ScheduleScript shipped_schedule() {
  return parse_schedule(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/schedule.json"));
}

InputFrame press(const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  return input;
}

constexpr double tick = 1.0 / 60.0;

// Runs the schedule past its fade in, pressing nothing.
void settle(Schedule &schedule, const double seconds = 1.0) {
  for (double time = 0.0; time < seconds; time += tick) {
    schedule.update(tick, InputFrame{});
  }
}

} // namespace

TEST_CASE("a town screen plays the music of the screen it was opened from") {
  TownBasics basics;
  basics.music = "garage";
  basics.music_from["club_wired"] = "keep";
  CHECK(town_music(basics, "club_wired") == "keep");
  CHECK(town_music(basics, "garage") == "garage");
}

TEST_CASE("the sound queue skips unnamed sounds and empties when taken") {
  SoundQueue sounds;
  sounds.play("confirm");
  sounds.play("");
  CHECK(sounds.take() == std::vector<std::string>{"confirm"});
  CHECK(sounds.take().empty());
}

TEST_CASE("a popup grows from the item before its contents show") {
  GrowingPopup popup({0.2, 0.3F});
  popup.open();
  CHECK(popup.growing() == 1.0F);
  CHECK(!popup.grown());
  Sprite panel;
  panel.rect = {0.0F, 0.0F, 100.0F, 50.0F};
  panel.position = {200.0F, 100.0F};
  const Sprite start = grown_panel(panel, popup, {40.0F, 40.0F});
  CHECK(near(start.size->at(0), 30.0F));
  CHECK(near(start.position[0], 40.0F - 15.0F));
  popup.update(0.25);
  CHECK(popup.grown());
  const Sprite end = grown_panel(panel, popup, {40.0F, 40.0F});
  CHECK(near(end.position[0], 200.0F));
  popup.close();
  CHECK(!popup.is_open());
}

TEST_CASE("the shipped schedule reads, with three venues and five days") {
  const ScheduleScript script = shipped_schedule();
  CHECK(script.scene == 32);
  CHECK(script.columns == 5);
  CHECK(script.venues.size() == schedule_venues);
  CHECK(script.music_from.at("club_wired") == "keep");
  CHECK(near(script.info.growth.seconds, 0.2));
}

TEST_CASE("the schedule opens a contest's details and closes them") {
  const ContestTables contests = tables();
  QuestSave quest;
  Schedule schedule(shipped_schedule(), contests, quest, "garage");
  settle(schedule);
  CHECK(schedule.selected_contest() == 0);
  schedule.update(tick, press(button::a));
  CHECK(schedule.popup().is_open());
  schedule.update(tick, InputFrame{});
  schedule.update(tick, press(button::b));
  CHECK(!schedule.popup().is_open());
}

TEST_CASE("moving right past the last day scrolls the calendar a day") {
  const ContestTables contests = tables();
  QuestSave quest;
  Schedule schedule(shipped_schedule(), contests, quest, "garage");
  settle(schedule);
  for (int step = 0; step < 6; ++step) {
    schedule.update(tick, press(button::right));
    schedule.update(tick, InputFrame{});
    settle(schedule, 0.3);
  }
  CHECK(schedule.column() == 4);
  CHECK(schedule.first_day() == 2);
  CHECK(!schedule.scrolling());
  // Day 6 is now the last column; its night contest sits on the second row.
  schedule.update(tick, press(button::down));
  CHECK(schedule.selected_contest() == 1);
}

TEST_CASE("cancel moves to EXIT and confirm there leaves for the place before") {
  const ContestTables contests = tables();
  QuestSave quest;
  Schedule schedule(shipped_schedule(), contests, quest, "club_wired");
  settle(schedule);
  schedule.update(tick, press(button::b));
  CHECK(schedule.row() == Schedule::exit_row);
  schedule.update(tick, InputFrame{});
  schedule.update(tick, press(button::a));
  settle(schedule);
  CHECK(schedule.done());
  CHECK(schedule.next() == "club_wired");
}

TEST_CASE("a won contest closed once won shows as none") {
  const ContestTables contests = tables();
  QuestSave quest;
  quest.won_contests = {0};
  ScheduleScript script = shipped_schedule();
  script.hidden_once_won = {0};
  Schedule schedule(script, contests, quest, "garage");
  CHECK(schedule.won(0));
  CHECK(schedule.contest_at(0, 0) == no_contest);
}
