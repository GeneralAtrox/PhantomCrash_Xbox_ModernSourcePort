// The schedule's bottom row: EXIT, and the port's SKIP TO DAY beside it
// with its question (phantomcrash/day_skip.hpp).

#include "phantomcrash/schedule.hpp"
#include "phantomcrash/shop_pointer.hpp"

namespace phantomcrash {

void Schedule::press_button() {
  sounds_.play(script_.select_sound);
  if (button_ == exit_button) {
    fade_.leave(back_);
    return;
  }
  plan_ = plan_day_skip(quest_, skip_day(), script_.skip.sleep.days_in_cycle);
  // A lap that raises the opponents asks with NO chosen first.
  const bool question = plan_.nights > 0;
  box_.open(question ? MessageBox::Kind::question : MessageBox::Kind::notice,
            day_skip_lines(script_.skip, plan_), plan_.raises ? 1 : 0);
  sounds_.play(box_.style().open_sound);
}

// YES sleeps every night but the last and leaves for the sleep screen,
// which passes that one; NO, or the notice's OK, closes the box. Cancel
// moves the choice to NO, as the game's other questions do.
void Schedule::update_box(const double tick_seconds, const InputFrame &input) {
  const MessageBoxStyle &style = box_.style();
  const bool question = box_.kind() == MessageBox::Kind::question;
  bool clicked = false;
  if (question) {
    bool fresh = false;
    const int step = columns_repeat_.step(tick_seconds, input, script_.left, script_.right, fresh);
    int moved = move_cursor(box_.choice(), step, fresh, 2);
    if (input.clicked && box_.grown()) {
      if (shop_hit(input, style.yes.normal)) {
        moved = 0;
        clicked = true;
      } else if (shop_hit(input, style.no.normal)) {
        moved = 1;
        clicked = true;
      }
    }
    if (moved != box_.choice()) {
      box_.set_choice(moved);
      sounds_.play(script_.move_sound);
    }
  } else {
    clicked = input.clicked && box_.grown() && shop_hit(input, style.ok.normal);
  }
  if (!box_.grown()) {
    return;
  }
  if (input.any_pressed(script_.confirm_buttons) || clicked) {
    sounds_.play(script_.select_sound);
    const bool yes = question && box_.choice() == 0;
    box_.close();
    if (yes) {
      sleep_until_last_night(quest_, plan_, script_.skip.sleep);
      skipped_ = true;
      fade_.leave(script_.skip.to);
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (!question) {
      sounds_.play(script_.close_sound);
      box_.close();
    } else if (box_.choice() != 1) {
      box_.set_choice(1);
      sounds_.play(script_.move_sound);
    }
  }
}

} // namespace phantomcrash
