#pragma once

#include "phantomcrash/rumbling_rules.hpp"
#include "phantomcrash/story_event.hpp"

#include <map>

namespace phantomcrash {

struct StoryPredicate {
  std::string kind;
  int area{-1};
  int minimum{};
};

struct StoryProgressionRules {
  std::map<int, StoryPredicate> predicates;
  int area_ranker_class{};
  int first_ranker_class{};
  std::string clear_exit;
  // The winning blow of that contest stands this long before the battle
  // ends (0x089861: robot state 0x17's timer), in 60 Hz frames.
  float clear_hold_frames{};
  // Each ranker's contest's theme, by contest (0x09e24c, 0x09e6b1).
  std::map<int, std::string> ranker_music;
};

// Match-local conditions compare the clear flags before and after this match.
// Permanent progression remains in QuestSave::won_contests and seen_events.
struct StoryMatchState {
  int area{-1};
  int contest{-1};
  bool new_class_win{};
  bool new_area_win{};
};

struct StoryConditionContext {
  const QuestSave &quest;
  const ContestTables &contests;
  const RumblingRules &rumbling;
  const StoryProgressionRules &rules;
  const StoryMatchState &match;
};

[[nodiscard]] StoryProgressionRules parse_story_progression(const json::JsonValue &document);
[[nodiscard]] bool story_conditions_pass(const StoryEvent &event,
                                         const StoryConditionContext &context);

} // namespace phantomcrash
