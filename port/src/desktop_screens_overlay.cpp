#include "desktop_screens.hpp"
#include "desktop_game_screens.hpp"

namespace phantomcrash {

// A click on the day opens the schedule's SKIP TO DAY; one on the HUD's SAVE
// button opens SAVE with the game as the screen has it now. Either runs over
// the screen, which waits suspended underneath.
bool ScreenRunner::open_overlay(const InputFrame &input) {
  if (screen_->done()) {
    return false;
  }
  // Every HUD screen is asked each tick, so its button lights under the mouse.
  // Both take the game as the screen has it, a shop's purchases included.
  const bool save = screen_->save_clicked(input) && !cooperative();
  const QuestSave *pending = screen_->pending_quest();
  const QuestSave &live = pending != nullptr ? *pending : quest_;
  if (save) {
    overlay_ = make_save_load("save_game", name_, &live);
    overlay_name_ = "save_game";
  } else if (name_ != "schedule" && screen_->day_clicked(input)) {
    overlay_ = game_->make("schedule", name_, place_before_, live);
    overlay_name_ = "schedule";
  } else {
    return false;
  }
  overlay_->set_music(told_music_);
  screen_->set_paused(true);
  rumble_.stop();
  SDL_Log("Overlay: open %s from=%s day=%d; origin suspended", overlay_name_.c_str(), name_.c_str(),
          quest_.day + 1);
  return true; // Consume the opening click; never forward it to either screen.
}

bool ScreenRunner::tick_overlay(const InputFrame &input, const double seconds) {
  if (!overlay_) {
    return open_overlay(input);
  }

  hear_music(seconds);
  overlay_->tick(input, seconds);
  for (const auto &sound : overlay_->take_sounds()) {
    audio_.play_sound(sound, {}, overlay_->sound_bus());
  }
  if (const auto song = radio_->update(name_, seconds, !audio_.playing())) {
    play_song(*song);
  }
  tell_music(false);
  if (overlay_->done()) {
    // SKIP TO DAY leaves the origin for the sleep screen, with the nights
    // before the last already slept.
    const QuestSave *skipped = overlay_name_ == "schedule" ? overlay_->quest_save() : nullptr;
    if (skipped != nullptr) {
      quest_ = *skipped;
      const std::string next = overlay_->next();
      overlay_.reset();
      screen_->set_paused(false);
      SDL_Log("Schedule shortcut: skipped to day %d; %s passes the last night", quest_.day + 1,
              next.c_str());
      show(next);
      return true;
    }
    // SAVE's EXIT, and the schedule closed, come back to the origin.
    overlay_.reset();
    screen_->set_paused(false);
    SDL_Log("Overlay: close %s to=%s day=%d; same origin resumed", overlay_name_.c_str(),
            name_.c_str(), quest_.day + 1);
  }
  return true; // Closing input must not activate anything on the restored screen.
}

} // namespace phantomcrash
