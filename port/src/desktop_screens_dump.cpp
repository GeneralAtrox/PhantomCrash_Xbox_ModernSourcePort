// The scene dump's description of the runner (docs/SCENE_DUMP.md): the
// screen showing, how it was reached, the save it runs on and the files
// data/debug/scene_dump.json names for it.

#include "desktop_screens.hpp"

#include <algorithm>
#include <exception>

namespace phantomcrash {
namespace {

using Object = json::JsonValue::Object;
using Array = json::JsonValue::Array;

json::JsonValue text(std::string value) {
  return {std::move(value)};
}

json::JsonValue whole(const std::int64_t value) {
  return {value};
}

std::vector<std::string> strings(const json::JsonValue *value) {
  std::vector<std::string> out;
  if (value != nullptr && value->is_array()) {
    for (const json::JsonValue &entry : value->array()) {
      if (entry.is_string()) {
        out.push_back(entry.string());
      }
    }
  }
  return out;
}

// A pattern matches exactly, or as a prefix when it ends in '*'.
bool matches(const std::string &pattern, const std::string &name) {
  if (pattern.ends_with('*')) {
    return name.starts_with(std::string_view(pattern).substr(0, pattern.size() - 1));
  }
  return pattern == name;
}

bool any_match(const std::vector<std::string> &patterns, const std::vector<std::string> &names) {
  return std::ranges::any_of(patterns, [&](const std::string &pattern) {
    return std::ranges::any_of(names,
                               [&](const std::string &name) { return matches(pattern, name); });
  });
}

void add_unique(Array &list, const std::vector<std::string> &files) {
  for (const std::string &file : files) {
    if (std::ranges::none_of(list, [&](const json::JsonValue &v) { return v.string() == file; })) {
      list.push_back(text(file));
    }
  }
}

// The read and edit lists of every rule whose screens or kinds match.
json::JsonValue scene_files(const DataRoots &roots, const std::vector<std::string> &screens,
                            const std::vector<std::string> &kinds) {
  Array read;
  Array edit;
  try {
    const json::JsonValue map = roots.document("debug/scene_dump.json");
    add_unique(read, strings(map.find("read_always")));
    if (const json::JsonValue *rules = map.find("rules")) {
      for (const json::JsonValue &rule : rules->array()) {
        if (any_match(strings(rule.find("screens")), screens) ||
            any_match(strings(rule.find("kinds")), kinds)) {
          add_unique(read, strings(rule.find("read")));
          add_unique(edit, strings(rule.find("edit")));
        }
      }
    }
  } catch (const std::exception &error) {
    read.push_back(text(std::string("data/debug/scene_dump.json: ") + error.what()));
  }
  return {Object{{"read", {std::move(read)}}, {"edit", {std::move(edit)}}}};
}

std::string joined(const std::vector<std::string> &names, const std::string &separator) {
  std::string out;
  for (const std::string &name : names) {
    out += (out.empty() ? "" : separator) + name;
  }
  return out;
}

} // namespace

json::JsonValue ScreenRunner::scene_state() const {
  Object scene{{"screen", text(name_)}, {"place_before", text(place_before_)}};
  Array trail;
  for (const std::string &name : trail_) {
    trail.push_back(text(name));
  }
  scene.push_back({"trail", {std::move(trail)}});
  if (cooperative()) {
    scene.push_back({"coop_player", whole(coop_slot_ + 1)});
  }
  if (name_.starts_with("event_")) {
    scene.push_back({"event", {Object{{"from", text(event_from_)}, {"next", text(event_next_)}}}});
  }
  if (story_match_.area >= 0) {
    scene.push_back(
        {"story_match",
         {Object{{"area", whole(story_match_.area)}, {"contest", whole(story_match_.contest)}}}});
  }
  if (active_match_) {
    scene.push_back({"rumbling_entry",
                     {Object{{"area", whole(active_match_->area)},
                             {"contest", whole(active_match_->contest)},
                             {"gate", whole(active_match_->gate)},
                             {"rental", {active_match_->rental}}}}});
  }
  const QuestSave &quest = quest_save();
  scene.push_back({"quest",
                   {Object{{"player", text(quest.player_name)},
                           {"day", whole(quest.day + 1)},
                           {"money", whole(quest.money)},
                           {"riding", text(quest.riding)},
                           {"cleared", {quest.cleared}}}}});
  if (const auto playing = scene_progress()) {
    scene.push_back({"playing",
                     {Object{{"label", text(playing->label)},
                             {playing->unit, whole(playing->frame)},
                             {"total", whole(playing->total)}}}});
  }
  if (screen_) {
    if (json::JsonValue details = screen_->scene_details(); !details.is_null()) {
      scene.push_back({"details", std::move(details)});
    }
  }
  return {std::move(scene)};
}

json::JsonValue ScreenRunner::scene_dump(const std::string &launch) const {
  const json::JsonValue scene = scene_state();
  const json::JsonValue *details = scene.find("details");
  std::vector<std::string> kinds = strings(details ? details->find("kinds") : nullptr);
  std::vector<std::string> screens = strings(details ? details->find("screens") : nullptr);
  screens.insert(screens.begin(), name_);
  if (training_.lessons.contains(name_) && std::ranges::find(kinds, "free_play") == kinds.end()) {
    kinds.push_back("lesson");
  }
  if (name_ == "test_drive") {
    kinds.push_back("test_drive");
  }

  // Where to start the port, and what to do there to come back here.
  std::string start = name_;
  std::vector<std::string> steps;
  if (start == "match_result") {
    steps.push_back("Finish the battle (" + place_before_ + ") to reach RESULT");
    start = place_before_;
  }
  if (start == "rumbling_battle" && active_match_) {
    steps.insert(steps.begin(), "In RUMBLING enter area " + std::to_string(active_match_->area) +
                                    ", contest " + std::to_string(active_match_->contest) +
                                    ", gate " + std::to_string(active_match_->gate) +
                                    (active_match_->rental ? " with a rental" : ""));
    start = "rumbling";
  } else if (start == "vs_battle") {
    steps.insert(steps.begin(), "In VS MODE enter the same robots and start the battle");
    start = "vs_mode";
  } else if (cooperative()) {
    steps.insert(steps.begin(), "Join and load player " + std::to_string(coop_slot_ + 1) +
                                    "'s co-op profile, then reach " + name_);
    start = "coop";
  }
  steps.insert(steps.begin(), launch + " --start " + start);
  if (!trail_.empty()) {
    steps.push_back("Screens shown, oldest first: " + joined(trail_, " > "));
  }
  Array how;
  for (std::string &step : steps) {
    how.push_back(text(std::move(step)));
  }

  std::string task = "Screen " + name_;
  if (details != nullptr) {
    if (const std::string summary = details->string_or("summary", ""); !summary.empty()) {
      task += ": " + summary;
    }
  }
  if (!place_before_.empty()) {
    task += " (entered from " + place_before_ + ")";
  }
  if (const auto playing = scene_progress()) {
    task += "; playing " + playing->label + ", " + playing->unit + " " +
            std::to_string(playing->frame) + " / " + std::to_string(playing->total);
  }
  Array kind_list;
  for (const std::string &kind : kinds) {
    kind_list.push_back(text(kind));
  }
  return {Object{{"task", text(task)},
                 {"problem", text("")},
                 {"how_to_reach", {std::move(how)}},
                 {"kinds", {std::move(kind_list)}},
                 {"scene", scene},
                 {"files", scene_files(roots_, screens, kinds)}}};
}

} // namespace phantomcrash
