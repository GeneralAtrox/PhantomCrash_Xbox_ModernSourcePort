// The garage's screens: sleep.

#include "test_support.hpp"

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sleep.hpp"

using namespace phantomcrash;

namespace {

SleepScript sleep_script() {
  return parse_sleep_script(json::parse(R"({
    "seconds": 4.0, "skip": "confirm", "sound": "sleep", "exit_to": "garage",
    "days_in_cycle": 60, "total_days_max": 9999
  })"));
}

InputFrame press(const Buttons buttons) {
  InputFrame input;
  input.pressed = buttons;
  input.held = buttons;
  return input;
}

} // namespace

TEST_CASE("a night moves the day on, counts it and clears the day's spin payments") {
  QuestSave quest;
  quest.day = 5;
  quest.spin_paid = 40;
  sleep_one_night(quest, sleep_script());
  CHECK(quest.day == 6);
  CHECK(quest.total_days == 1);
  CHECK(quest.spin_paid == 0);
  CHECK(quest.laps == 0);
}

TEST_CASE("the calendar wraps after 60 days, and a cleared game counts the lap") {
  QuestSave quest;
  quest.day = 59;
  sleep_one_night(quest, sleep_script());
  CHECK(quest.day == 0);
  CHECK(quest.laps == 0);
  quest.day = 59;
  quest.cleared = true;
  quest.total_days = 9999;
  sleep_one_night(quest, sleep_script());
  CHECK(quest.day == 0);
  CHECK(quest.laps == 1);
  CHECK(quest.total_days == 9999);
}

TEST_CASE("sleep waits four seconds with its sound, or less on a confirm press") {
  Sleep sleep(sleep_script(), QuestSave{});
  sleep.tick({}, 1.0);
  CHECK(sleep.take_sounds() == std::vector<std::string>{"sleep"});
  sleep.tick({}, 2.5);
  CHECK(!sleep.done());
  sleep.tick({}, 1.0);
  CHECK(sleep.done());
  CHECK(sleep.quest().day == 1);

  Sleep skipped(sleep_script(), QuestSave{});
  skipped.tick({}, 0.1);
  skipped.tick(press(button::a), 0.1);
  CHECK(skipped.done());
  CHECK(skipped.quest().total_days == 1);
}

TEST_CASE("the shipped sleep script waits four seconds and returns to the garage") {
  const SleepScript script =
      parse_sleep_script(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("ui/sleep.json"));
  CHECK(script.seconds == 4.0);
  CHECK(script.exit_to == "garage");
  CHECK(script.sound == "sleep");
  CHECK(script.days_in_cycle == 60);
}
