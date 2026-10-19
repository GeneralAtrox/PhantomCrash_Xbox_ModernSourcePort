#include "desktop_screens.hpp"
#include "desktop_game_screens.hpp"
#include "desktop_talk.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

namespace phantomcrash {
namespace {
// The screens the scene dump lists as the way here (docs/SCENE_DUMP.md).
constexpr std::size_t scene_trail_length = 16;
} // namespace

const StoryEvent *ScreenRunner::story_event(const std::string &from, const std::string &to) const {
  const auto from_scene =
      scene_ids_.find(find_quick_rental(rumbling_battle_, from) ? "rumbling_battle" : from);
  const auto to_scene = scene_ids_.find(to);
  if (from_scene == scene_ids_.end() || to_scene == scene_ids_.end()) {
    return nullptr;
  }
  const StoryConditionContext context{quest_, game_->contests(), game_->regulation_check().rules,
                                      story_rules_, story_match_};
  return event_between(events_, from_scene->second, to_scene->second, quest_.seen_events, &context);
}

// "event_<scene>": a talk scene, leading on to the screen it interrupted
// (or, started on its own, the screen of its destination scene; the screen
// it was opened from for the return scene).
const StoryEvent *ScreenRunner::named_event(const std::string &name) const {
  if (!name.starts_with("event_")) {
    return nullptr;
  }
  int scene = -1;
  const std::string_view digits = std::string_view(name).substr(6);
  std::from_chars(digits.data(), digits.data() + digits.size(), scene);
  const auto event =
      std::ranges::find_if(events_, [&](const StoryEvent &item) { return item.scene == scene; });
  return event == events_.end() ? nullptr : &*event;
}

std::string ScreenRunner::event_destination(const StoryEvent &event,
                                            const std::string &from) const {
  std::string next = event_next_;
  if (next.empty() && event.to_scene == return_scene_ && !from.empty()) {
    next = from;
  }
  if (next.empty()) {
    const auto found = std::ranges::find_if(
        scene_ids_, [&](const auto &entry) { return entry.second == event.to_scene; });
    next = found == scene_ids_.end() ? "title" : found->first;
  }
  return next;
}

std::unique_ptr<Screen> ScreenRunner::make_event(const std::string &name, const std::string &from) {
  const StoryEvent *event = named_event(name);
  if (event == nullptr) {
    return nullptr;
  }
  return std::make_unique<TalkScreen>(textures_, font_, *event, talk_, talk_tuning_,
                                      event_destination(*event, from), quest_.player_name, frame_,
                                      credits_, quest_);
}

void ScreenRunner::show(const std::string &name, const bool events) {
  apply_pending_cheats();
  if (screen_ && screen_->gameplay_paused()) {
    audio_.set_gameplay_paused(false); // Tools > Level Select may replace a paused battle.
  }
  overlay_.reset();
  std::string target = name;
  if (target == "new_game" || target == "title" || name_ == "sleep") {
    story_match_ = {};
    pending_entry_.reset();
    pending_result_.reset();
  }
  if (target == "rumbling_battle" && !name_.starts_with("event_") && screen_) {
    if (const auto *entry = screen_->rumbling_entry()) {
      pending_entry_ = *entry;
      story_match_ = {entry->area, entry->contest};
    }
  }
  if (target == "match_result" && !pending_result_) {
    pending_result_ = make_match_result();
    if (!pending_result_ && !clear_route_.empty()) {
      show(std::exchange(clear_route_, {}), false);
      return;
    }
  }
  // A conversation a screen leads to by name is passed over too, but not one
  // asked for that returns to its screen (Pepper's explanations).
  if (const StoryEvent *named = skip_conversations_ ? named_event(name) : nullptr;
      named && named->to_scene != return_scene_ && event_from_.empty()) {
    if (std::ranges::find(quest_.seen_events, named->scene) == quest_.seen_events.end()) {
      quest_.seen_events.push_back(named->scene);
    }
    SDL_Log("Story event skipped: scene=%d from=%s", named->scene, name_.c_str());
    show(event_destination(*named, name_), events);
    return;
  }
  const std::string source = event_from_.empty() ? name_ : event_from_;
  const StoryEvent *event = events ? story_event(source, name) : nullptr;
  // Skipped conversations play out at once: seen, in the order they would
  // have played, then the chain ends as it would after the last.
  for (std::size_t skipped = 0; event && skip_conversations_ && skipped < events_.size();
       ++skipped) {
    quest_.seen_events.push_back(event->scene);
    event_from_ = source;
    SDL_Log("Story event skipped: scene=%d from=%s to=%s", event->scene, source.c_str(),
            name.c_str());
    event = story_event(source, name);
  }
  if (event && !skip_conversations_) {
    quest_.seen_events.push_back(event->scene);
    event_from_ = source;
    event_next_ = name;
    SDL_Log("Story event: scene=%d from=%s to=%s day=%d contest=%d area=%d", event->scene,
            source.c_str(), name.c_str(), quest_.day + 1, story_match_.contest, story_match_.area);
    target = "event_" + std::to_string(event->scene);
  } else if (!event_from_.empty()) {
    // The original rechecks the interrupted transition after each conversation.
    // On completing that chain it clears only the transient (+0x0e == -1) flags.
    std::erase_if(quest_.seen_events, [&](const int scene) {
      return std::ranges::any_of(events_, [&](const StoryEvent &event) {
        return event.scene == scene && event.conditions[0] == -1;
      });
    });
    SDL_Log("Story chain complete: %s -> %s", event_from_.c_str(), name.c_str());
    event_from_.clear();
    event_next_.clear();
  }
  if (!name_.starts_with("event_")) {
    place_before_ = name_;
  }
  rumble_.stop();
  if (!cooperative() && rumbling_world_ && rumbling_world_->lent() >= 0) {
    rumbling_world_->give_back(quest_); // the player has left the area's battle
  }
  if (screen_) {
    for (const std::string &sound : screen_->held_sounds()) {
      audio_.stop_sound(sound);
    }
    audio_.stop_keyed();
  }
  remember_coop_menu(target);
  auto next = target == "match_result" && pending_result_ ? std::move(pending_result_)
                                                          : make(target, name_);
  SDL_Log("Screen: %s -> %s", name_.c_str(), target.c_str());
  if (next == nullptr) {
    SDL_Log("No screen named \"%s\"; showing the title", target.c_str());
    next = make("title", name_);
    name_ = "title";
  } else {
    name_ = target;
  }
  trail_.push_back(name_);
  if (trail_.size() > scene_trail_length) {
    trail_.erase(trail_.begin());
  }
  // The battle being left goes behind the receipt, shown now or after a
  // conversation.
  adopt_backdrop(pending_result_           ? pending_result_.get()
                 : name_ == "match_result" ? next.get()
                                           : nullptr);
  screen_ = std::move(next);
  if (cooperative()) {
    for (const auto &place : town_map_.places) {
      if (place.next == name_) {
        coop_location_ = place.next;
      }
    }
    SDL_Log("Co-op navigation: slot=%d screen=%s location=%s day=%d", coop_slot_ + 1, name_.c_str(),
            coop_location_.c_str(), quest_.day + 1);
  }
  reset_music_program();
  music_silence_ = 0.0;
  for (const std::string &sound : screen_->sound_preloads()) {
    audio_.preload(sound); // not loaded mid-battle, on its first play
  }
  screen_->set_movie_gain(audio_options_.gain(AudioBus::movies));
  play_music();
  if (const auto song = radio_->open(name_)) {
    play_song(*song);
  }
  tell_music(true);
}

} // namespace phantomcrash
