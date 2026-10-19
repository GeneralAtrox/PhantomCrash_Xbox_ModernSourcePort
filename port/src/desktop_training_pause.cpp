#include "desktop_training.hpp"
#include "desktop_draw_list.hpp"

namespace phantomcrash {

void TrainingScreen::load_pause(const DataRoots &roots) {
  // Existing mods' battle exit binding opens the pause menu as well.
  pause_menu_.load(roots.document("ui/battle_pause.json"), script_.battle.buttons.exit);
}

bool TrainingScreen::tick_pause(const InputFrame &input) {
  if (online_exit_requested_) {
    return true;
  }
  if (!paused_) {
    if (done_ || !input.any_pressed(pause_menu_.pause_buttons())) {
      return false;
    }
    paused_ = true;
    if (live_world_) {
      pause_menu_.set_roster(live_world_->roster_status(live_area_));
    }
    pause_menu_.open();
    unplaced_.push_back({SoundCue::Action::play, "window"});
    SDL_Log("Battle pause menu: clock=%.3f play=%.3f; shared match continues=%d", clock_,
            play_seconds_, coop_match_ != nullptr);
    return true; // Opening input cannot also choose a button.
  }
  switch (pause_menu_.tick(input)) {
  case PauseMenu::Choice::resume:
    paused_ = false;
    jump_armed_ = false;
    SDL_Log("Battle resumed: clock=%.3f play=%.3f", clock_, play_seconds_);
    break;
  case PauseMenu::Choice::exit:
    paused_ = false;
    if (online()) {
      if (!online_exit_) {
        throw std::runtime_error("Online exit is not connected to its session");
      }
      online_exit_();
      online_exit_requested_ = true;
    } else {
      done_ = true;
    }
    SDL_Log("Battle pause EXIT: clock=%.3f play=%.3f destination=%s", clock_, play_seconds_,
            next().c_str());
    break;
  case PauseMenu::Choice::none:
    break;
  }
  return true; // Menu input cannot also fire or move the robot.
}

void TrainingScreen::draw_pause(const Viewport &viewport) {
  if (!paused_) {
    return;
  }
  // Port pause presentation; gameplay and the replay recording stay underneath.
  fill_black(textures_, pause_menu_.dim());
  DrawList list(textures_, font_, viewport, 0);
  if (live_world_) {
    pause_menu_.set_roster(live_world_->roster_status(live_area_));
  }
  pause_menu_.draw(list, font_);
  pause_menu_.draw_roster(list, font_, textures_.renderer(), viewport);
  list.flush();
}

} // namespace phantomcrash
