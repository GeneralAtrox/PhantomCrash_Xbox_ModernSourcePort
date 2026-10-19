// SKIP TO DAY's plan, question and nights (phantomcrash/day_skip.hpp).

#include "phantomcrash/day_skip.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

std::vector<std::string> lines_of(const json::JsonValue &value) {
  std::vector<std::string> lines;
  for (const auto &line : value.array()) {
    lines.push_back(line.string());
  }
  return lines;
}

EnemyLevelTag parse_tag(const json::JsonValue &value) {
  EnemyLevelTag tag;
  tag.text = required(value, "text").string();
  tag.font = required(value, "font").string();
  tag.box = numbers<4>(required(value, "box"), "box");
  tag.colour = parse_colour(required(value, "colour").string());
  tag.layer = static_cast<int>(value.number_or("layer", tag.layer));
  if (const auto *shadow = value.find("shadow")) {
    tag.shadow = parse_text_shadow(*shadow);
    tag.shadow_blur = static_cast<float>(shadow->number_or("blur", 0.0));
  }
  return tag;
}

// Every "{name}" in `text` replaced by `value`.
void fill(std::string &text, const std::string &name, const std::string &value) {
  const std::string key = "{" + name + "}";
  for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
    text.replace(at, key.size(), value);
  }
}

std::string two_digits(const int value) {
  return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

} // namespace

DaySkipScript parse_day_skip(const json::JsonValue &value) {
  DaySkipScript script;
  script.enabled = true;
  script.from = lines_of(required(value, "from"));
  script.to = required(value, "to").string();
  script.button = member(value, "normal", parse_sprite);
  script.button_selected = member(value, "selected", parse_sprite);
  script.question = member(value, "question", lines_of);
  script.penalty = member(value, "penalty", lines_of);
  script.nothing = member(value, "nothing", lines_of);
  const auto words = member(value, "day_words", lines_of);
  if (words.size() != 2) {
    throw std::runtime_error("day_words needs the word for one day and for several");
  }
  script.day_words = {words[0], words[1]};
  script.tag = member(value, "enemy_level", parse_tag);
  return script;
}

DaySkipPlan plan_day_skip(const QuestSave &quest, const int day, const int days_in_cycle) {
  DaySkipPlan plan;
  if (days_in_cycle <= 0) {
    return plan;
  }
  const int today = ((quest.day % days_in_cycle) + days_in_cycle) % days_in_cycle;
  plan.day = ((day % days_in_cycle) + days_in_cycle) % days_in_cycle;
  plan.nights = ((plan.day - today) % days_in_cycle + days_in_cycle) % days_in_cycle;
  plan.raises = quest.cleared && today + plan.nights >= days_in_cycle;
  plan.level = quest.cleared ? quest.laps : 0;
  return plan;
}

std::vector<std::string> day_skip_lines(const DaySkipScript &script, const DaySkipPlan &plan) {
  if (plan.nights <= 0) {
    return script.nothing;
  }
  std::vector<std::string> lines = plan.raises ? script.penalty : script.question;
  const std::string days =
      std::to_string(plan.nights) + " " + script.day_words[plan.nights == 1 ? 0 : 1];
  for (std::string &line : lines) {
    fill(line, "day", two_digits(plan.day + 1));
    fill(line, "days", days);
    fill(line, "level", std::to_string(plan.level));
    fill(line, "next", std::to_string(plan.level + 1));
  }
  return lines;
}

std::string enemy_level_text(const EnemyLevelTag &tag, const int level) {
  std::string text = tag.text;
  fill(text, "level", std::to_string(level));
  return text;
}

void sleep_until_last_night(QuestSave &quest, const DaySkipPlan &plan, const SleepScript &sleep) {
  for (int night = 1; night < plan.nights; ++night) {
    sleep_one_night(quest, sleep);
  }
}

} // namespace phantomcrash
