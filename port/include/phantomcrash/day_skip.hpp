#pragma once

// The port's SKIP TO DAY beside the schedule's EXIT (not in the original):
// the nights to a day on the calendar slept at once, after a question that
// names them and warns when the calendar starts over on a cleared game,
// where each lap raises every opponent's level (sleep_one_night). See
// data/ui/schedule.json's "skip".

#include "phantomcrash/json.hpp"
#include "phantomcrash/settings.hpp"
#include "phantomcrash/sleep.hpp"
#include "phantomcrash/sprite_layer.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

// The opponents' level raise, shown once the game is cleared.
struct EnemyLevelTag {
  std::string text; // "{level}" is the raise
  std::string font;
  std::array<float, 4> box{}; // canvas left, top, right, bottom
  std::uint32_t colour{0xffffff};
  int layer{0x104};
  std::optional<TextShadow> shadow;
  float shadow_blur{};
};

struct DaySkipScript {
  bool enabled{};                // false without a "skip" entry: no button
  std::vector<std::string> from; // the places the schedule offers it from; "*" for all
  std::string to;                // the screen that sleeps the last night
  Sprite button, button_selected;
  // Lines of the question; "{day}" is the day skipped to, "{days}" the
  // days passing ("1 day", "5 days"), "{level}" and "{next}" the raise
  // before and after a lap.
  std::vector<std::string> question;
  std::vector<std::string> penalty; // the question when a lap raises it
  std::vector<std::string> nothing; // the notice when no day passes
  std::array<std::string, 2> day_words{"day", "days"};
  EnemyLevelTag tag;
  SleepScript sleep; // the night, from data/ui/sleep.json
};

// Throws std::runtime_error naming the member at fault.
[[nodiscard]] DaySkipScript parse_day_skip(const json::JsonValue &value);

struct DaySkipPlan {
  int day{};     // the calendar day woken on, counted from 0
  int nights{};  // 0: nothing to skip
  bool raises{}; // the calendar starts over on a cleared game
  int level{};   // the raise now
};

[[nodiscard]] DaySkipPlan plan_day_skip(const QuestSave &quest, int day, int days_in_cycle);
// The question's (or the notice's) lines for `plan`.
[[nodiscard]] std::vector<std::string> day_skip_lines(const DaySkipScript &script,
                                                      const DaySkipPlan &plan);
// `text` with "{level}" filled, as the tag reads.
[[nodiscard]] std::string enemy_level_text(const EnemyLevelTag &tag, int level);
// Every night but the last, which the sleep screen then passes.
void sleep_until_last_night(QuestSave &quest, const DaySkipPlan &plan, const SleepScript &sleep);

} // namespace phantomcrash
