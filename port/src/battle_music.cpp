#include "phantomcrash/battle_music.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace phantomcrash {
namespace {

std::string replaced(std::string text, const std::string &key, const std::string &value) {
  if (const auto at = text.find(key); at != std::string::npos) {
    text.replace(at, key.size(), value);
  }
  return text;
}

} // namespace

BattleMusic::BattleMusic(BattleMusicScript script, MusicLibrary music,
                         std::vector<MusicCredit> credits, const std::string &from)
    : TownLogic(script), script_(std::move(script)), music_(std::move(music)),
      credits_(std::move(credits)), repeat_(script_.repeat_delay, script_.repeat_interval),
      side_(script_.repeat_delay, script_.repeat_interval) {
  own_music_ = town_music(script_, from);
  // Only tracks with a credit can be picked (0x2a0e6).
  for (const int track : music_.owned_tracks()) {
    if (track >= 0 && track < static_cast<int>(credits_.size())) {
      owned_.push_back(track);
    }
  }
  browsed_ = music_.program;
  mode_choice_ = music_.random ? 1 : 0;
  restart_help();
}

std::string BattleMusic::music_track() const {
  if (preview_ >= 0) {
    return credits_[static_cast<std::size_t>(preview_)].file;
  }
  return own_music_;
}

int BattleMusic::pages() const {
  const int per_page = script_.stock.columns * script_.stock.rows;
  return std::max(1, (static_cast<int>(owned_.size()) + per_page - 1) / per_page);
}

int BattleMusic::stock_track(const int page, const int slot) const {
  const int index = page * script_.stock.columns * script_.stock.rows + slot;
  return index >= 0 && index < static_cast<int>(owned_.size())
             ? owned_[static_cast<std::size_t>(index)]
             : -1;
}

int BattleMusic::cursor_track() const {
  if (view_ != BattleMusicView::stock || on_ok()) {
    return -1;
  }
  return stock_track(page_, row_ * script_.stock.columns + column_);
}

std::string BattleMusic::help_text() const {
  return help_;
}

void BattleMusic::restart_help() {
  const int track = cursor_track();
  std::string text = script_.help.text;
  if (track >= 0) {
    const MusicCredit &credit = credits_[static_cast<std::size_t>(track)];
    text = replaced(replaced(script_.track_format, "{title}", credit.title), "{artist}",
                    credit.artist);
  }
  if (text != help_) {
    help_ = text;
    help_seconds_ = 0.0;
  }
}

void BattleMusic::open(const BattleMusicView view) {
  before_ = view_;
  view_ = view;
  view_seconds_ = 0.0;
  repeat_.reset();
  side_.reset();
  restart_help();
}

void BattleMusic::update(const double tick_seconds, const InputFrame &input) {
  if (done()) {
    return;
  }
  view_seconds_ += tick_seconds;
  arrive_seconds_ += tick_seconds;
  ok_seconds_ += tick_seconds;
  help_seconds_ += tick_seconds;
  if (!advance(tick_seconds)) {
    return;
  }
  // Each panel takes input once it has slid in.
  if (view_seconds_ < script_.slide_seconds) {
    return;
  }
  switch (view_) {
  case BattleMusicView::menu:
    menu_input(tick_seconds, input);
    break;
  case BattleMusicView::choose:
    choose_input(tick_seconds, input);
    break;
  case BattleMusicView::confirm_set:
    confirm_input(input);
    break;
  case BattleMusicView::stock:
    stock_input(tick_seconds, input);
    break;
  case BattleMusicView::play_mode:
    mode_input(tick_seconds, input);
    break;
  }
}

void BattleMusic::menu_input(const double tick_seconds, const InputFrame &input) {
  const int count = static_cast<int>(script_.items.size()) + 1; // and EXIT
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(cursor_, step, fresh, count); moved != cursor_) {
    arrived_up_ = moved < cursor_;
    // Wrapping round the ends, or coming from EXIT, jumps without the
    // overshoot (0x37d6f).
    arrive_seconds_ = std::abs(moved - cursor_) > 1 || on_exit() ? 1.0e9 : 0.0;
    cursor_ = moved;
    sound(script_.move_sound);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (on_exit()) {
      // Leaving marks every new track seen (0x369bd).
      music_.see_all();
      leave(script_.exit_to);
      return;
    }
    const std::string &item = script_.items[static_cast<std::size_t>(cursor_)];
    browsed_ = music_.program;
    if (item == "program_set" || item == "program_edit") {
      editing_ = item == "program_edit";
      open(BattleMusicView::choose);
    } else if (item == "play_mode") {
      mode_choice_ = music_.random ? 1 : 0;
      open(BattleMusicView::play_mode);
    } else if (item == "program_name") {
      leave(script_.name_entry);
    }
    return;
  }
  // Cancel moves the cursor to EXIT (0x38607).
  if (input.any_pressed(script_.cancel_buttons) && !on_exit()) {
    arrived_up_ = false;
    cursor_ = count - 1;
    arrive_seconds_ = 0.0;
    sound(script_.jump_sound);
  }
}

void BattleMusic::choose_input(const double tick_seconds, const InputFrame &input) {
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.cancel_sound);
    browsed_ = music_.program;
    open(BattleMusicView::menu);
    return;
  }
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(browsed_, step, fresh, music_program_count);
      moved != browsed_) {
    browsed_ = moved;
    sound(script_.move_sound);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    if (editing_) {
      page_ = column_ = row_ = 0;
      open(BattleMusicView::stock);
    } else {
      open(BattleMusicView::confirm_set);
    }
  }
}

void BattleMusic::confirm_input(const InputFrame &input) {
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.cancel_sound);
    before_ = view_;
    view_ = BattleMusicView::choose;
    view_seconds_ = script_.slide_seconds; // back without sliding
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    music_.program = browsed_;
    ok_seconds_ = 0.0;
    open(BattleMusicView::menu);
  }
}

void BattleMusic::turn_page(const int step) {
  if (pages() < 2) {
    return;
  }
  page_ = (page_ + step + pages()) % pages();
  sound(script_.jump_sound);
}

void BattleMusic::stock_input(const double tick_seconds, const InputFrame &input) {
  auto &program = music_.programs[static_cast<std::size_t>(browsed_)];
  const MusicStock &stock = script_.stock;
  const int track = cursor_track();
  if (!on_ok()) {
    if (input.any_pressed(script_.page_back)) {
      turn_page(-1);
    } else if (input.any_pressed(script_.page_forward)) {
      turn_page(1);
    } else if (input.any_pressed(script_.preview)) {
      // Y plays the track under the cursor (0x38f8c).
      if (track >= 0) {
        preview_ = preview_ == track ? -1 : track;
      }
    } else if (input.any_pressed(script_.cancel_buttons)) {
      row_ = stock.rows;
      sound(script_.jump_sound);
    } else if (input.any_pressed(script_.confirm_buttons)) {
      if (track >= 0 && std::cmp_less(program.size(), music_program_length)) {
        program.push_back(track);
        sound(script_.add_sound);
      }
    } else if (input.any_pressed(script_.back_space)) {
      if (!program.empty()) {
        program.pop_back();
        sound(script_.remove_sound);
      }
    } else {
      bool fresh = false;
      const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
      const int moved = move_cursor(column_, step, fresh, stock.columns);
      if (moved != column_) {
        // Wrapping off either edge turns the page.
        if (fresh && step < 0 && moved > column_) {
          turn_page(-1);
        } else if (fresh && step > 0 && moved < column_) {
          turn_page(1);
        } else {
          sound(script_.move_sound);
        }
        column_ = moved;
      }
    }
  } else if (input.any_pressed(script_.confirm_buttons)) {
    // OK goes back to the programs (0x38aaa).
    sound(script_.select_sound);
    preview_ = -1;
    open(BattleMusicView::choose);
    return;
  }
  bool fresh = false;
  const int step = repeat_.step(tick_seconds, input, script_.up, script_.down, fresh);
  if (const int moved = move_cursor(row_, step, fresh, stock.rows + 1); moved != row_) {
    row_ = moved;
    sound(script_.move_sound);
  }
  restart_help();
}

void BattleMusic::mode_input(const double tick_seconds, const InputFrame &input) {
  if (input.any_pressed(script_.cancel_buttons)) {
    sound(script_.cancel_sound);
    open(BattleMusicView::menu);
    return;
  }
  if (input.any_pressed(script_.confirm_buttons)) {
    sound(script_.select_sound);
    music_.random = mode_choice_ == 1;
    open(BattleMusicView::menu);
    return;
  }
  bool fresh = false;
  const int step = side_.step(tick_seconds, input, script_.left, script_.right, fresh);
  if (const int moved = move_cursor(mode_choice_, step, fresh, 2); moved != mode_choice_) {
    mode_choice_ = moved;
    sound(script_.jump_sound);
  }
}

} // namespace phantomcrash
