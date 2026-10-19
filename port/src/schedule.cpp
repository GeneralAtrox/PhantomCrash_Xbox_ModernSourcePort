// The contest schedule's cursor, scrolling and popup (0x363e7, 0x35a9f).

#include "phantomcrash/schedule.hpp"
#include "phantomcrash/shop_pointer.hpp"

#include <algorithm>
#include <utility>

namespace phantomcrash {
namespace {

// The scroll's steps, a day at a time (0x2fc).
constexpr int phase_still = 0;
constexpr int phase_fold = 1;
constexpr int phase_slide = 2;
constexpr int phase_unfold = 3;

int wrap_day(const int day) {
  return ((day % calendar_days) + calendar_days) % calendar_days;
}

bool contains(const std::vector<int> &values, const int value) {
  return std::ranges::find(values, value) != values.end();
}

// "*" stands for every place.
bool contains_place(const std::vector<std::string> &places, const std::string &place) {
  return !place.empty() && (std::ranges::find(places, place) != places.end() ||
                            std::ranges::find(places, "*") != places.end());
}

} // namespace

Schedule::Schedule(ScheduleScript script, const ContestTables &contests, const QuestSave &quest,
                   std::string back, const std::string &from, MessageBoxStyle box)
    : script_(std::move(script)), contests_(contests), won_(quest.won_contests),
      game_cleared_(quest.cleared), back_(std::move(back)),
      fade_(script_.fade_in, script_.fade_out),
      columns_repeat_(script_.repeat_delay, script_.repeat_interval),
      rows_repeat_(script_.repeat_delay, script_.repeat_interval), first_day_(wrap_day(quest.day)),
      popup_(script_.info.growth), quest_(quest),
      skip_offered_(script_.skip.enabled && contains_place(script_.skip.from, from)),
      box_(std::move(box)) {}

bool Schedule::won(const int contest) const {
  return contains(won_, contest);
}

int Schedule::contest_at(const int day, const int row) const {
  const int contest = contest_on(contests_, day, row, game_cleared_);
  if (contest != no_contest && contains(script_.hidden_once_won, contest) && won(contest)) {
    return no_contest;
  }
  return contest;
}

int Schedule::selected_contest() const {
  return row_ < exit_row ? contest_at(first_day_ + column_, row_) : no_contest;
}

// 0x35a9f: while a day scrolls, the days shown lag the window by the days
// still to come, and one column folds away, slides or unfolds.
ScheduleColumn Schedule::column_view(const int column) const {
  ScheduleColumn view;
  int first = first_day_ - pending_;
  if (phase_ == phase_slide || phase_ == phase_unfold) {
    first += scroll_direction();
  }
  view.day = wrap_day(first + column);
  if (phase_ == phase_still) {
    return view;
  }
  const int last = script_.columns - 1;
  const bool forward = pending_ > 0;
  const float left = static_cast<float>(std::max(phase_left_, 0.0) / script_.scroll_step_seconds);
  if (phase_ == phase_fold && column == (forward ? 0 : last)) {
    view.scale_x = left;
  } else if (phase_ == phase_slide) {
    view.shown = column != (forward ? last : 0);
    view.offset_x = script_.cell_step[0] * left * static_cast<float>(scroll_direction());
  } else if (phase_ == phase_unfold && column == (forward ? last : 0)) {
    view.scale_x = 1.0F - left;
  }
  return view;
}

void Schedule::scroll(const int days) {
  first_day_ = wrap_day(first_day_ + days);
  pending_ += days;
  phase_ = phase_fold;
  phase_left_ = script_.scroll_step_seconds;
  ticker_seconds_ = 0.0;
}

void Schedule::step_scroll(const double tick_seconds) {
  phase_left_ -= tick_seconds;
  if (phase_left_ > 0.0) {
    return;
  }
  phase_left_ = script_.scroll_step_seconds;
  if (phase_ != phase_unfold) {
    ++phase_;
    return;
  }
  pending_ -= scroll_direction();
  phase_ = pending_ != 0 ? phase_fold : phase_still;
}

void Schedule::update(const double tick_seconds, const InputFrame &input) {
  seconds_ += tick_seconds;
  ticker_seconds_ += tick_seconds;
  popup_.update(tick_seconds);
  box_.update(tick_seconds);
  const bool ready = fade_.update(tick_seconds);
  if (phase_ != phase_still) {
    step_scroll(tick_seconds);
    return;
  }
  if (ready && !fade_.leaving()) {
    if (box_.is_open()) {
      update_box(tick_seconds, input);
    } else {
      handle_input(tick_seconds, input);
    }
  }
}

void Schedule::handle_input(const double tick_seconds, const InputFrame &raw) {
  InputFrame input = raw;
  if (input.clicked && input.pointer) {
    if (popup_.is_open()) {
      if (popup_.grown()) {
        input.pressed |= script_.cancel_buttons;
      }
    } else if (skip_offered_ && shop_hit(input, script_.skip.button)) {
      row_ = exit_row;
      button_ = skip_button;
      input.pressed |= script_.confirm_buttons;
    } else if (shop_hit(input, script_.exit)) {
      row_ = exit_row;
      button_ = exit_button;
      input.pressed |= script_.confirm_buttons;
    } else {
      for (int column = 0; column < script_.columns; ++column) {
        for (int row = 0; row < exit_row; ++row) {
          Sprite tile;
          tile.rect = script_.closed;
          tile.position = {script_.cell_origin[0] + script_.cell_step[0] * column,
                           script_.cell_origin[1] + script_.cell_step[1] * row};
          if (shop_hit(input, tile)) {
            column_ = column;
            row_ = row;
            ticker_seconds_ = 0.0;
            input.pressed |= script_.confirm_buttons;
          }
        }
      }
    }
  }
  if (!popup_.is_open() && input.mouse_wheel != 0) {
    sounds_.play(script_.page_sound);
    scroll(input.mouse_wheel > 0 ? -1 : 1);
    return;
  }
  if (popup_.is_open()) {
    if (input.any_pressed(script_.cancel_buttons)) {
      sounds_.play(script_.close_sound);
      popup_.close();
    }
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    if (row_ == exit_row) {
      press_button();
    } else if (selected_contest() != no_contest) {
      sounds_.play(script_.select_sound);
      popup_.open();
    }
    return;
  }
  if (input.any_pressed(script_.cancel_buttons)) {
    if (row_ != exit_row || button_ != exit_button) {
      sounds_.play(script_.exit_sound);
    }
    row_ = exit_row;
    button_ = exit_button;
    return;
  }
  bool fresh = false;
  const int rows = exit_row + 1;
  if (row_ == exit_row && skip_offered_) {
    // Left and right go towards the button on that side.
    const int step = columns_repeat_.step(tick_seconds, input, script_.left, script_.right, fresh);
    const bool skip_left = script_.skip.button.position[0] < script_.exit.position[0];
    const int moved = std::clamp(button_ + (skip_left ? -step : step), exit_button, skip_button);
    if (moved != button_) {
      sounds_.play(script_.move_sound);
      button_ = moved;
      return;
    }
  } else if (row_ != exit_row) {
    const int step = columns_repeat_.step(tick_seconds, input, script_.left, script_.right, fresh);
    if (step != 0) {
      sounds_.play(script_.move_sound);
      const int last = script_.columns - 1;
      if ((step < 0 && column_ == 0) || (step > 0 && column_ == last)) {
        scroll(step); // past either end the calendar moves a day
      } else {
        column_ += step;
        ticker_seconds_ = 0.0;
      }
      return;
    }
  }
  if (const int step = rows_repeat_.step(tick_seconds, input, script_.up, script_.down, fresh)) {
    const int moved = move_cursor(row_, step, fresh, rows);
    if (moved != row_) {
      sounds_.play(script_.row_sound);
      row_ = moved;
      ticker_seconds_ = 0.0;
    }
    return;
  }
  if (row_ == exit_row) {
    return;
  }
  if (input.any_pressed(script_.page_forward)) {
    sounds_.play(script_.page_sound);
    column_ = 0;
    scroll(script_.columns);
  } else if (input.any_pressed(script_.page_back)) {
    sounds_.play(script_.page_sound);
    column_ = script_.columns - 1;
    scroll(-script_.columns);
  }
}

} // namespace phantomcrash
