#include "phantomcrash/story_progression.hpp"

#include "phantomcrash/json_fields.hpp"

#include <algorithm>
#include <stdexcept>

namespace phantomcrash {
namespace {

bool in_range(const int value, const int count) {
  return value >= 0 && value < count;
}

bool won(const QuestSave &quest, const int contest) {
  return contest >= 0 && std::ranges::find(quest.won_contests, contest) != quest.won_contests.end();
}

int class_wins(const StoryConditionContext &context, const int area) {
  if (!in_range(area, static_cast<int>(context.rumbling.area_rankers.size()))) {
    return 0;
  }
  return static_cast<int>(
      std::ranges::count_if(context.rumbling.area_rankers[area].leaders,
                            [&](const int contest) { return won(context.quest, contest); }));
}

bool area_won(const StoryConditionContext &context, const int area) {
  return in_range(area, static_cast<int>(context.rumbling.area_rankers.size())) &&
         won(context.quest, context.rumbling.area_rankers[area].contest);
}

bool predicate_passes(const StoryPredicate &predicate, const StoryConditionContext &context) {
  const auto *contest = context.contests.contest(context.match.contest);
  const bool area_ranker = contest && contest->contest_class == context.rules.area_ranker_class;
  if (predicate.kind == "class_wins") {
    return class_wins(context, predicate.area) >= predicate.minimum;
  }
  if (predicate.kind == "total_ranker_wins") {
    int total = 0;
    for (int area = 0; area < static_cast<int>(context.rumbling.area_rankers.size()); ++area) {
      total += class_wins(context, area) + static_cast<int>(area_won(context, area));
    }
    return total >= predicate.minimum;
  }
  if (predicate.kind == "area_ranker_today") {
    const auto *today = context.contests.contest(
        contest_on(context.contests, context.quest.day, predicate.area, context.quest.cleared));
    return today && today->contest_class == context.rules.area_ranker_class &&
           !area_won(context, predicate.area);
  }
  if (predicate.kind == "new_area_win") {
    return area_ranker && context.match.new_area_win;
  }
  if (predicate.kind == "area_loss") {
    return area_ranker && !context.match.new_area_win && !area_won(context, context.match.area);
  }
  if (predicate.kind == "first_ranker") {
    return contest && contest->contest_class == context.rules.first_ranker_class;
  }
  if (predicate.kind == "new_class_win") {
    return context.match.new_class_win;
  }
  if (predicate.kind == "owned_music") {
    return static_cast<int>(context.quest.music.owned.size()) >= predicate.minimum;
  }
  return false;
}

} // namespace

StoryProgressionRules parse_story_progression(const json::JsonValue &document) {
  StoryProgressionRules rules;
  rules.clear_exit = json::required(document, "clear_exit").string();
  if (rules.clear_exit.empty()) {
    throw std::runtime_error("Story progression clear_exit must name a screen");
  }
  rules.clear_hold_frames = static_cast<float>(document.number_or("clear_hold_frames", 0.0));
  if (const json::JsonValue *themes = document.find("ranker_music")) {
    for (const auto &[contest, music] : themes->object()) {
      rules.ranker_music[std::stoi(contest)] = music.string();
    }
  }
  rules.area_ranker_class =
      static_cast<int>(json::required(document, "area_ranker_class").number());
  rules.first_ranker_class =
      static_cast<int>(json::required(document, "first_ranker_class").number());
  for (const auto &entry : json::required(document, "predicates").array()) {
    const int id = static_cast<int>(json::required(entry, "id").number());
    StoryPredicate predicate{json::required(entry, "kind").string(),
                             static_cast<int>(entry.number_or("area", -1)),
                             static_cast<int>(entry.number_or("minimum", 0))};
    const std::vector<std::string> kinds{"class_wins",    "total_ranker_wins", "area_ranker_today",
                                         "new_area_win",  "area_loss",         "first_ranker",
                                         "new_class_win", "owned_music"};
    if (!in_range(id, 75) || predicate.minimum < 0 ||
        std::ranges::find(kinds, predicate.kind) == kinds.end() ||
        ((predicate.kind == "class_wins" || predicate.kind == "area_ranker_today") &&
         !in_range(predicate.area, schedule_venues)) ||
        !rules.predicates.emplace(id, std::move(predicate)).second) {
      throw std::runtime_error("Invalid or duplicate story progression predicate");
    }
  }
  return rules;
}

bool story_conditions_pass(const StoryEvent &event, const StoryConditionContext &context) {
  const auto &bytes = event.conditions;
  if (in_range(bytes[1], 75)) {
    const auto predicate = context.rules.predicates.find(bytes[1]);
    if (predicate == context.rules.predicates.end() ||
        !predicate_passes(predicate->second, context)) {
      return false;
    }
  }
  if (in_range(bytes[2], 56) && context.match.contest != bytes[2]) {
    return false;
  }
  if (in_range(bytes[3], 4) && context.match.area != bytes[3]) {
    return false;
  }
  const auto *contest = context.contests.contest(context.match.contest);
  if (in_range(bytes[4], 28) && (!contest || contest->contest_class != bytes[4])) {
    return false;
  }
  if (in_range(bytes[5], 2) && (!contest || static_cast<int>(contest->night) != bytes[5])) {
    return false;
  }
  return true;
}

} // namespace phantomcrash
