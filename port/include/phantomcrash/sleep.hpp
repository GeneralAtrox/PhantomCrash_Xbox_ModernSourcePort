#pragma once

// Sleeping in the garage (the original's scene 24: create 0x2e47c, run
// 0x2e3ad, destroy 0x2e39d). The screen stays black while the sleep sound
// plays; after four seconds, or at once on a confirm press, the night
// passes and the garage opens again. See docs/GARAGE.md.

#include "phantomcrash/input.hpp"
#include "phantomcrash/json.hpp"
#include "phantomcrash/settings.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

struct SleepScript {
  double seconds{4.0};           // 0x2e47c: the frame rate times four
  Buttons skip{button::confirm}; // 0x2e3ad: A or START (0xdf310) cuts the wait short
  std::string sound;             // sys entry 0x2b, stopped as the screen closes
  std::string exit_to;           // result 0: the garage (scene 13)
  int days_in_cycle{60};         // the calendar day wraps at 60 (0x3c)
  int total_days_max{9999};      // + 0x5ffc stops counting at 0x270f
};

// Throws std::runtime_error when a value is missing or wrong.
[[nodiscard]] SleepScript parse_sleep_script(const json::JsonValue &document);

// The night passing (0x2e3ad): the calendar day moves on and wraps, a
// cleared game counts a lap of the calendar as it wraps, the days slept
// are counted, and the town map's spin payments start again.
void sleep_one_night(QuestSave &quest, const SleepScript &script);

class Sleep final {
public:
  explicit Sleep(SleepScript script, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds);
  [[nodiscard]] bool done() const noexcept {
    return done_;
  }
  [[nodiscard]] const SleepScript &script() const noexcept {
    return script_;
  }
  [[nodiscard]] const QuestSave &quest() const noexcept {
    return quest_;
  }
  [[nodiscard]] std::vector<std::string> take_sounds();

private:
  SleepScript script_;
  QuestSave quest_;
  double left_{};
  bool started_{};
  bool done_{};
  std::vector<std::string> sounds_;
};

} // namespace phantomcrash
