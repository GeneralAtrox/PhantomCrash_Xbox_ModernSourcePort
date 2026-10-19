// Sleeping in the garage (scene 24) and the night passing.

#include "phantomcrash/sleep.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {

using json::required;

SleepScript parse_sleep_script(const json::JsonValue &document) {
  SleepScript script;
  script.seconds = required(document, "seconds").number();
  script.skip = buttons_from_json(required(document, "skip"));
  script.sound = document.string_or("sound", "");
  script.exit_to = required(document, "exit_to").string();
  script.days_in_cycle = static_cast<int>(required(document, "days_in_cycle").number());
  script.total_days_max = static_cast<int>(required(document, "total_days_max").number());
  if (script.seconds < 0.0 || script.days_in_cycle <= 0 || script.total_days_max <= 0) {
    throw std::runtime_error("seconds, days_in_cycle and total_days_max must not be negative");
  }
  return script;
}

namespace {

constexpr int night_seed_step = 7919;

} // namespace

void sleep_one_night(QuestSave &quest, const SleepScript &script) {
  quest.day = std::max(quest.day, 0) + 1;
  if (quest.day >= script.days_in_cycle) {
    quest.day = 0;
    if (quest.cleared) {
      ++quest.laps; // + 0x604c, only once the game is cleared (0x6dfc1)
    }
  }
  quest.total_days = std::min(quest.total_days + 1, script.total_days_max);
  quest.spin_paid = 0;
  // Sonic Amp's chart changes overnight (0x2e47c); each night draws its own.
  roll_chart(quest.music,
             static_cast<std::uint32_t>(quest.total_days * night_seed_step + quest.laps));
}

Sleep::Sleep(SleepScript script, QuestSave quest)
    : script_(std::move(script)), quest_(std::move(quest)), left_(script_.seconds) {}

void Sleep::tick(const InputFrame &input, const double tick_seconds) {
  if (done_) {
    return;
  }
  if (!started_) {
    started_ = true;
    if (!script_.sound.empty()) {
      sounds_.push_back(script_.sound);
    }
  }
  if (input.any_pressed(script_.skip)) {
    left_ = 0.0;
  }
  left_ -= tick_seconds;
  if (left_ < 0.0) {
    sleep_one_night(quest_, script_);
    done_ = true;
  }
}

std::vector<std::string> Sleep::take_sounds() {
  return std::exchange(sounds_, {});
}

} // namespace phantomcrash
